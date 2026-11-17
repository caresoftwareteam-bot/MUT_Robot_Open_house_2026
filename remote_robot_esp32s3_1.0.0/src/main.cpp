/**
 * @file           : main.cpp
 * @brief          : ESP32-S3 Robot Wi-Fi Web Controller
 * @version        : 1.0.0
 * @author         : Pakprom Naennoi (Peace), Electronics Engineer (Embedded Systems)
 * @date           : 2026-11-16
 *
 * @note           : Developed for MUT Robot Open House 2026 (18-Nov-2026).
 *
 * @details
 * - ESP32-S3 Wi-Fi Access Point (AP Mode)
 * - Wi-Fi AP Channel 1, Maximum 1 Connected Device
 * - Single Active WebSocket Controller (1 Connection)
 * - Reject Additional WebSocket Controllers (BUSY)
 * - HTTP Web Server (Port 80)
 * - WebSocket Communication Server (Port 81)
 * - LittleFS Web Application Hosting (HTML/CSS/JavaScript)
 * - Web-Based D-Pad and Virtual Joystick Control
 * - Landscape Web Application Interface
 * - Joystick Command Parsing [X,Y]
 * - Differential Drive Motor Control
 * - UART Communication with STM32F446RCTx
 * - UART CRC-8 Communication Protocol
 * - Motor Stop on Controller Disconnection
 * - Joystick Command Timeout Failsafe (350 ms)
 * - Periodic Communication Heartbeat (100 ms)
 * - NeoPixel RGB Status Indicator
 */

#define Firmware_Version "1.0.0"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============ HARDWARE ============
#define STM32_TX_PIN 17
#define STM32_RX_PIN 18
#define STM32_BAUD 115200
#define NEOPIXEL_PIN 48
#define LED_BRIGHTNESS 40

HardwareSerial STM32Serial(1);
Adafruit_NeoPixel rgb(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ============ COMMUNICATION ============
#define PROTOCOL_HEADER 0xA5U
#define CMD_SET_MOTOR 0x01U
#define CMD_STOP 0x02U
#define CMD_HEARTBEAT 0x03U
#define MOTOR_COAST 0U
#define MOTOR_FORWARD 1U
#define MOTOR_REVERSE 2U
#define PACKET_SIZE 8U
#define COMMAND_TIMEOUT_MS 350U
#define REPEAT_INTERVAL_MS 100U
#define TCP_BUFFER_SIZE 64U
#define HTTP_PORT 80U
#define WS_PORT 81U

#define WIFI_AP_CHANNEL 1U
#define WIFI_MAX_CLIENTS 1U

const static char *get_serial_number()
{
  static char serial_number[20]; // MUT_robot_xxxxxx + null terminator

  if (serial_number[0] != '\0')
    return serial_number;

  uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
  snprintf(serial_number, sizeof(serial_number), "MUT_robot_%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);

  return serial_number;
}

static const char *ssid = get_serial_number();
static const char *password = "12345678";
WebServer http(HTTP_PORT);
WebSocketsServer ws(WS_PORT);
static uint8_t controllerId = 0xFF;

static bool clientActive = false;
static bool motorActive = false;
static uint32_t lastCommand = 0;
static uint32_t lastPeriodicTx = 0;

enum LED_Status
{
  LED_WIFI_READY,
  LED_CONNECTED,
  LED_RUNNING,
  LED_FAILSAFE
};
static LED_Status ledStatus = LED_WIFI_READY;
static uint32_t ledLastBlink = 0;
static bool ledBlinkState = false;

static void LED_SetStatus(LED_Status status)
{
  if (ledStatus == status)
    return;
  ledStatus = status;
  ledLastBlink = millis();
  ledBlinkState = false;
}

static void LED_Process()
{
  static uint32_t lastColor = UINT32_MAX;
  uint32_t color = 0;
  switch (ledStatus)
  {
  case LED_WIFI_READY:
    color = rgb.Color(0, 0, 200);
    break;
  case LED_CONNECTED:
    color = rgb.Color(0, 200, 0);
    break;
  case LED_RUNNING:
    color = rgb.Color(0, 200, 200);
    break;
  case LED_FAILSAFE:
    if (millis() - ledLastBlink >= 200U)
    {
      ledLastBlink = millis();
      ledBlinkState = !ledBlinkState;
    }
    color = ledBlinkState ? rgb.Color(200, 0, 0) : 0;
    break;
  }
  if (color != lastColor)
  {
    rgb.setPixelColor(0, color);
    rgb.show();
    lastColor = color;
  }
}

static uint8_t CRC8_Calculate(const uint8_t *data, uint8_t len)
{
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; ++i)
  {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; ++j)
      crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x07U) : (uint8_t)(crc << 1);
  }
  return crc;
}

static void STM32_SendPacket(uint8_t cmd, uint8_t s1, uint8_t d1, uint8_t s2, uint8_t d2)
{
  uint8_t pkt[PACKET_SIZE] = {PROTOCOL_HEADER, cmd, s1, d1, s2, d2, 0, 0};
  pkt[7] = CRC8_Calculate(pkt, 7);
  STM32Serial.write(pkt, sizeof(pkt));
}

static void Motor_Stop()
{
  STM32_SendPacket(CMD_STOP, 0, MOTOR_COAST, 0, MOTOR_COAST);
  lastPeriodicTx = millis();

  if (motorActive)
    Serial.println("[MOTOR] STOP -> STM32");

  motorActive = false;
  LED_SetStatus(clientActive ? LED_CONNECTED : LED_WIFI_READY);
}

static uint8_t Motor_Direction(int value)
{
  return value > 0 ? MOTOR_FORWARD : value < 0 ? MOTOR_REVERSE
                                               : MOTOR_COAST;
}

static void Motor_Control(int x, int y)
{
  if (x == 0 && y == 0)
  {
    Motor_Stop(); // Immediately send STOP on joystick release.
    return;
  }

  int left = x - y;
  int right = x + y;
  int peak = max(abs(left), abs(right));
  if (peak > 100)
  {
    left = left * 100 / peak;
    right = right * 100 / peak;
  }

  STM32_SendPacket(CMD_SET_MOTOR, (uint8_t)abs(left), Motor_Direction(left), (uint8_t)abs(right), Motor_Direction(right));
  motorActive = (left != 0 || right != 0);
  LED_SetStatus(motorActive ? LED_RUNNING : LED_CONNECTED);
}

static bool Parse_Joystick(const char *input, int *x, int *y)
{
  const char *p = input;
  while (isspace((unsigned char)*p))
    ++p;
  if (*p++ != '[')
    return false;

  char *end;
  long xx = strtol(p, &end, 10);
  if (end == p || xx < -100 || xx > 100)
    return false;

  p = end;

  while (isspace((unsigned char)*p))
    ++p;

  if (*p++ != ',')
    return false;

  long yy = strtol(p, &end, 10);
  if (end == p || yy < -100 || yy > 100)
    return false;

  p = end;

  while (isspace((unsigned char)*p))
    ++p;

  if (*p++ != ']')
    return false;

  while (isspace((unsigned char)*p))
    ++p;

  if (*p)
    return false;

  *x = (int)xx;
  *y = (int)yy;
  return true;
}

static void Process_Command(const char *text)
{
  int x = 0, y = 0;
  if (!Parse_Joystick(text, &x, &y))
  {
    Serial.printf("[ERROR] Invalid joystick packet: %s\r\n", text);
    Motor_Stop();
    return;
  }
  lastCommand = millis();
  Motor_Control(x, y);
  Serial.printf("[APP] X:%d Y:%d\r\n", x, y);
}

static void WS_Event(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_CONNECTED:
    if (clientActive)
    {
      ws.sendTXT(num, "BUSY");
      ws.disconnect(num);
      return;
    }
    controllerId = num;
    clientActive = true;
    lastCommand = millis();
    Motor_Stop();
    Serial.printf("[WS] Controller %u connected\r\n", num);
    break;
  case WStype_DISCONNECTED:
    if (clientActive && num == controllerId)
    {
      Motor_Stop();
      clientActive = false;
      controllerId = 0xFF;
      LED_SetStatus(LED_WIFI_READY);
      Serial.println("[WS] Controller disconnected");
    }
    break;
  case WStype_TEXT:
    if (!clientActive || num != controllerId)
      return;
    if (length == 0 || length >= TCP_BUFFER_SIZE || memchr(payload, 0, length))
    {
      Motor_Stop();
      Serial.println("[WS] Invalid message length/content");
      return;
    }
    {
      char command[TCP_BUFFER_SIZE];
      memcpy(command, payload, length);
      command[length] = '\0';
      Process_Command(command);
    }
    break;
  default:
    break;
  }
}

static void Safety_Process()
{
  uint32_t now = millis();
  if (clientActive && motorActive && (uint32_t)(now - lastCommand) >= COMMAND_TIMEOUT_MS)
  {
    Motor_Stop();
    LED_SetStatus(LED_FAILSAFE);
    Serial.println("[FAILSAFE] Joystick timeout");
  }

  if (clientActive && (uint32_t)(now - lastPeriodicTx) >= REPEAT_INTERVAL_MS)
  {
    lastPeriodicTx = now;
    if (motorActive)
      STM32_SendPacket(CMD_HEARTBEAT, 0, 0, 0, 0);
    else
      STM32_SendPacket(CMD_STOP, 0, MOTOR_COAST, 0, MOTOR_COAST);
  }
}

void setup()
{
  Serial.begin(115200);
  rgb.begin();
  rgb.setBrightness(LED_BRIGHTNESS);
  rgb.clear();
  rgb.show();
  STM32Serial.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);

  Serial.printf("[INFO] START MUT REMODELED ROBOT CONTROLLER FIRMWARE\r\n");
  Serial.printf("[INFO] Firmware Version: %s\r\n", Firmware_Version);
  Serial.printf("[INFO] Build Date: %s %s\r\n", __DATE__, __TIME__);
  Serial.printf("[INFO] Serial Number: %s\r\n", get_serial_number());

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(ssid, password, WIFI_AP_CHANNEL, 0, WIFI_MAX_CLIENTS))
  {
    Serial.println("[WIFI] AP failed");
    while (true)
    {
      rgb.setPixelColor(0, rgb.Color(200, 0, 0));
      rgb.show();
      delay(200);
      rgb.clear();
      rgb.show();
      delay(200);
    }
  }

  Serial.printf("[WIFI] Max AP Clients: %u\r\n", WIFI_MAX_CLIENTS);

  LED_SetStatus(LED_WIFI_READY);

  if (LittleFS.begin(false))
  {
    Serial.println("[FS] LittleFS mounted OK");

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (file)
    {
      Serial.printf("[FS] %s (%u bytes)\n",
                    file.name(),
                    (unsigned)file.size());

      file = root.openNextFile();
    }
  }
  else
  {
    Serial.println("[FS] LittleFS mount FAILED");
  }

  http.on("/", HTTP_GET, []()
          {
    File f = LittleFS.open("/index.html", "r");

    if (!f)
    {
        http.send(500, "text/plain",
                  "Missing index.html in LittleFS");
        return;
    }

    http.streamFile(f, "text/html; charset=utf-8");
    f.close(); });

  http.on("/style.css", HTTP_GET, []()
          {
    File f = LittleFS.open("/style.css", "r");

    if (!f)
    {
        http.send(404, "text/plain", "Missing CSS");
        return;
    }

    http.streamFile(f, "text/css; charset=utf-8");
    f.close(); });

  http.on("/app.js", HTTP_GET, []()
          {
    File f = LittleFS.open("/app.js", "r");

    if (!f)
    {
        http.send(404, "text/plain", "Missing JS");
        return;
    }

    http.streamFile(f, "application/javascript; charset=utf-8");
    f.close(); });

  http.on("/manifest.webmanifest", HTTP_GET, []()
          {
  File f = LittleFS.open("/manifest.webmanifest", "r");

  if (!f) {
    http.send(404, "text/plain", "Manifest not found");
    return;
  }

  http.streamFile(f, "application/manifest+json");
  f.close(); });

  http.onNotFound([]()
                  { http.send(404, "text/plain", "Not Found"); });

  http.begin();
  ws.begin();
  ws.onEvent(WS_Event);

  Serial.printf("[WIFI] SSID: %s IP: %s\r\n", ssid, WiFi.softAPIP().toString().c_str());
  Serial.println("[HTTP] Port 80 / [WebSocket] Port 81");

  Motor_Stop();
}

void loop()
{
  http.handleClient();
  ws.loop();
  Safety_Process();
  LED_Process();
}
