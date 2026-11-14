# 🤖 MUT Robot 2026 — ESP32-S3 Communication Controller

![Version](https://img.shields.io/badge/version-1.0.0-blue) ![Framework](https://img.shields.io/badge/framework-Arduino-00979D) ![MCU](https://img.shields.io/badge/MCU-ESP32--S3-555555)

เฟิร์มแวร์สำหรับ **ESP32-S3 N16R8** ทำหน้าที่เป็นตัวกลางสื่อสารระหว่างแอปควบคุมหุ่นยนต์ผ่าน Wi-Fi กับบอร์ด **STM32F446RCTx Motor Controller** โดยรับคำสั่ง Joystick ทาง TCP แปลงเป็นคำสั่งมอเตอร์แบบ Differential Drive และส่ง Packet 8 ไบต์ผ่าน UART พร้อม CRC-8

พัฒนาสำหรับงาน **MUT Robot Open House 2026 (18 November 2026)**

> **Architecture:** ESP32-S3 รับคำสั่งและคำนวณความเร็วของมอเตอร์; STM32F446RCTx เป็นผู้สร้าง PWM และขับมอเตอร์ผ่าน DRV8833PWR โดยตรง

## ✨ Features / คุณสมบัติ

- Wi-Fi **Access Point (AP)** โดยตั้ง SSID เป็น `MUT_robot_XXXX` จาก 16 บิตล่างของ eFuse MAC
- TCP Socket Server ที่ **Port 8080** สำหรับรับคำสั่ง Joystick
- รับข้อความรูปแบบ `[X,Y]\n` โดยแต่ละแกนอยู่ในช่วง `-100` ถึง `100`
- Differential Drive (Tank Mixing) สำหรับมอเตอร์ซ้าย/ขวา พร้อม Normalize ไม่เกิน 100%
- สื่อสารกับ STM32 ผ่าน UART ที่ **115200 baud, 8N1**
- Protocol ขนาด **8 bytes** ตรวจสอบด้วย **CRC-8 (poly 0x07)**
- ส่ง STOP ทันทีเมื่อ Joystick อยู่ที่ศูนย์
- Joystick Command Timeout **350 ms** เพื่อสั่งหยุดเมื่อขาดคำสั่ง
- ส่ง Heartbeat ขณะรถกำลังเคลื่อนที่ หรือส่ง STOP ซ้ำขณะหยุด ทุก **100 ms** เมื่อ TCP Client เชื่อมต่อ
- NeoPixel RGB แสดงสถานะการเชื่อมต่อ การเคลื่อนที่ และ Failsafe
- รองรับ TCP Client ที่ใช้งานอยู่หนึ่งราย; ปฏิเสธการเชื่อมต่อเพิ่มเติม

## 🧠 System Architecture

```text
   Android App / TCP Joystick Client
                 |
            Wi-Fi (AP)
                 |
          TCP Port 8080
                 |
        +----------------+
        | ESP32-S3 N16R8 |
        | Joystick Parse |
        | Motor Mixing   |
        | CRC-8 Encode   |
        +----------------+
                 |
         UART1 TX GPIO17
         115200 baud, 8N1
                 |
         STM32 USART1 RX
                 |
       +------------------+
       | STM32F446RCTx    |
       | PWM TIM3 / TIM4  |
       +------------------+
                 |
              DRV8833
                 |
            DC Motors x2
```

## 🛠 Hardware / ฮาร์ดแวร์

| Component | Description |
|---|---|
| Main MCU | ESP32-S3 N16R8 |
| Motor Controller | STM32F446RCTx (separate board) |
| Motor Driver | DRV8833PWR (connected to STM32) |
| Status LED | On-board NeoPixel (GPIO48) |
| Programming / Debug | USB Serial, 115200 baud |

### 🔌 UART Wiring

| ESP32-S3 | STM32F446RCTx | Purpose |
|---|---|---|
| GPIO17 (UART1 TX) | PA10 (USART1 RX) | Motor control packets — required |
| GPIO18 (UART1 RX) | PA9 (USART1 TX) | Optional reverse UART path; not used by current control logic |
| GND | GND | Common signal ground — required |

**Important:** ใช้สัญญาณ UART ระดับ **3.3V TTL** และต้องต่อ GND ร่วมกัน ห้ามต่อ UART เข้ากับแรงดัน RS-232 โดยตรง

## 📶 Wi-Fi & TCP Configuration

| Parameter | Value |
|---|---|
| Wi-Fi mode | `WIFI_AP` |
| SSID | `MUT_robot_XXXX` (derived from ESP32 eFuse MAC) |
| Password | `12345678` |
| TCP Port | `8080` |
| TCP message termination | `\n` (accepts `\r\n`) |
| Receive buffer | 64 bytes |
| Maximum active controlling client | 1 |
| Joystick timeout | 350 ms |
| Periodic UART transmission | 100 ms |

> ตรวจสอบ IP Address ของ AP จาก Serial Monitor บรรทัด `[WIFI] SSID: ... IP: ...` แทนการสมมติ IP ในแอป

## 🎮 Joystick Command Format

แอปส่งข้อความ ASCII / UTF-8 ในรูปแบบ:

```text
[X,Y]\n
```

ตัวอย่าง:

```text
[100,0]
[-100,0]
[0,100]
[0,-100]
[0,0]
```

- `X` คือคำสั่งเดินหน้า / ถอยหลัง
- `Y` คือคำสั่งเลี้ยว
- ค่าแต่ละแกนต้องเป็นจำนวนเต็มระหว่าง `-100` และ `100`
- `[0,0]` จะส่งคำสั่ง **STOP** ทันที

### Differential Drive Calculation

```cpp
left  = x + y;
right = x - y;

peak = max(abs(left), abs(right));
if (peak > 100)
{
  left  = left  * 100 / peak;
  right = right * 100 / peak;
}
```

ใช้เครื่องหมายของผลลัพธ์เป็นทิศทาง (`FORWARD` / `REVERSE`) และค่าสัมบูรณ์เป็น Speed (0–100%)

## 🔄 ESP32 ↔ STM32 UART Protocol

Packet มีขนาด **8 bytes** และ **ไม่ได้ส่งค่า X/Y ต้นฉบับ** ไปยัง STM32 แต่ส่งค่าความเร็วและทิศทางมอเตอร์หลังคำนวณแล้ว

| Byte | Field | Description |
|---|---|---|
| 0 | Header | `0xA5` |
| 1 | Command | `0x01` MOTOR, `0x02` STOP, `0x03` HEARTBEAT |
| 2 | M1 Speed | `0–100` |
| 3 | M1 Direction | `0` Coast, `1` Forward, `2` Reverse |
| 4 | M2 Speed | `0–100` |
| 5 | M2 Direction | `0` Coast, `1` Forward, `2` Reverse |
| 6 | Reserved | `0x00` |
| 7 | CRC-8 | Polynomial `0x07`, init `0x00`, over bytes 0–6 |

**CRC-8:** ไม่สะท้อนบิต (non-reflected), ไม่มี final XOR

| Command | Hex | Behavior |
|---|---|---|
| `CMD_SET_MOTOR` | `0x01` | ส่งความเร็วและทิศทางมอเตอร์ทั้งสอง |
| `CMD_STOP` | `0x02` | สั่งหยุดมอเตอร์ทั้งสอง |
| `CMD_HEARTBEAT` | `0x03` | แสดงการสื่อสารขณะเคลื่อนที่ ไม่ใช่คำสั่งต่ออายุ Motor Timeout ฝั่ง STM32 |

ไม่มีคำสั่ง `0x04` ใน Firmware เวอร์ชันนี้

## 🛡 Safety & Failsafe

1. เมื่อรับ `[0,0]` จะส่ง `CMD_STOP` ทันที
2. หากไม่มีคำสั่ง Joystick ใหม่ภายใน **350 ms** ขณะที่มอเตอร์ทำงาน ESP32 จะส่ง STOP และเข้า Failsafe LED State
3. เมื่อ TCP Client หลุดการเชื่อมต่อ จะสั่ง STOP และคืนสถานะ Wi-Fi Ready
4. ระหว่าง Client เชื่อมต่อ หากมอเตอร์หยุดอยู่ จะส่ง `CMD_STOP` ซ้ำทุก **100 ms**
5. เมื่อมอเตอร์ทำงาน ESP32 จะส่ง `CMD_HEARTBEAT` ทุก **100 ms**; STM32 ยังใช้เวลาได้รับ `CMD_SET_MOTOR` ล่าสุดเพื่อตรวจ Motor Timeout ของตัวเอง

> Failsafe ฝั่ง STM32 ทำงานแยกจาก ESP32; ดู README ของ STM32 Motor Controller

## 🌈 NeoPixel Status (GPIO48)

| State | Color / Pattern | Meaning |
|---|---|---|
| `LED_WIFI_READY` | Blue | AP พร้อมรับการเชื่อมต่อ |
| `LED_CONNECTED` | Green | TCP Client เชื่อมต่อ / มอเตอร์หยุด |
| `LED_RUNNING` | Cyan | มีคำสั่งให้มอเตอร์ทำงาน |
| `LED_FAILSAFE` | Red blinking | Joystick Command Timeout |

LED Brightness กำหนดไว้ที่ `40` ตามโค้ด

## 💻 Development & Dependencies

- Language: **C++**
- Framework: **Arduino (ESP32)**
- Libraries: `WiFi.h`, `Adafruit_NeoPixel.h`, `Arduino.h`
- UART class: `HardwareSerial(1)`
- USB Serial Debug: **115200 baud**


## 🧰 Development Tools & Environment

This firmware was developed using **Visual Studio Code and PlatformIO**, with the **Arduino Framework for ESP32**.

PlatformIO is used for project management, dependency handling, firmware compilation, and uploading firmware to the ESP32-S3.

### Development Tools

| Tool | Purpose |
|---|---|
| **Visual Studio Code** | Main IDE for firmware development and Git integration |
| **PlatformIO IDE** | Project management, building, uploading, and serial monitoring |
| **Arduino Framework** | Application framework for ESP32-S3 |
| **ESP32 Arduino Core** | Hardware support, Wi-Fi, UART, and system APIs |
| **Adafruit NeoPixel Library** | RGB LED status indication |
| **USB Serial Monitor** | Firmware debugging and runtime logs |

### Recommended VS Code Extensions

| Extension | Purpose |
|---|---|
| **PlatformIO IDE** | ESP32 development, build, upload, and serial monitoring |
| **C/C++ (Microsoft)** | C/C++ IntelliSense and code navigation |

### Development Workflow

```text
Visual Studio Code
       |
       v
PlatformIO IDE
       |
       v
Arduino Framework
       |
       v
Firmware Development
(Wi-Fi / TCP / UART / NeoPixel)
       |
       v
PlatformIO Build
       |
       v
Upload to ESP32-S3
       |
       v
Serial Monitor
       |
       v
Hardware Testing
(ESP32-S3 <--> STM32F446RCTx)
```

### Build & Upload Using PlatformIO

Open the project folder in Visual Studio Code and ensure that PlatformIO IDE is installed.

Build firmware:

```bash
pio run
```

Upload firmware:

```bash
pio run -t upload
```

Open Serial Monitor:

```bash
pio device monitor -b 115200
```

### PlatformIO Project Configuration

The project uses a `platformio.ini` file to define its target board, framework, dependencies, and build settings.

Example configuration structure:

```ini
[env:esp32s3]
platform = espressif32
board = <your_actual_board_id>
framework = arduino
monitor_speed = 115200

lib_deps =
    adafruit/Adafruit NeoPixel
```

> **Note:** Replace `<your_actual_board_id>` with the board identifier used in the project's existing `platformio.ini`. The configuration above is an example, not a replacement for the tested project settings.

### Project Structure

```text
ESP32_S3/
├── include/
├── lib/
├── src/
│   └── main.cpp
├── test/
├── platformio.ini
└── README.md
```

---

### Getting Started

1. เปิดโปรเจกต์ ESP32-S3 ด้วย Arduino IDE หรือ PlatformIO ที่ตั้งค่าสำหรับบอร์ด ESP32-S3 N16R8 ให้ตรงกับฮาร์ดแวร์
2. ติดตั้งไลบรารี **Adafruit NeoPixel**
3. ตรวจสอบการต่อ GPIO17 → STM32 PA10 และ GND ร่วมกัน
4. Build และ Upload Firmware ลง ESP32-S3
5. เปิด Serial Monitor 115200 baud เพื่ออ่าน SSID / IP / TCP Port
6. เชื่อมต่อมือถือหรือ TCP Client เข้ากับ AP `MUT_robot_XXXX`
7. ส่งข้อความ Joystick ตามรูปแบบ `[X,Y]\n` ไปที่ TCP Port 8080

ตัวอย่าง Debug Log:

```text
[WIFI] SSID: MUT_robot_XXXX IP: <AP_IP>
[TCP] Port 8080
[TCP] Client connected
[APP] X:100 Y:0
[APP] X:0 Y:0
[MOTOR] STOP -> STM32
```

> ตัวอย่างข้างต้นเป็นรูปแบบข้อความที่ Firmware พิมพ์ออกมา โดย `<AP_IP>` และค่า Joystick ขึ้นอยู่กับอุปกรณ์/คำสั่งจริง

## 🔗 Related Project

- **STM32F446RCTx Motor Controller:** โปรเจกต์คู่กัน ทำหน้าที่รับ UART Packet, ตรวจ CRC และขับมอเตอร์ผ่าน DRV8833
- **Previous project (2025):** [MUT_Robot_L298N](https://github.com/peacepaul1999/MUT_Robot_L298N) — โครงการเดิมที่ใช้ L298N ไม่ใช่ Motor Driver ของเวอร์ชัน 2026

## 👨‍🔧 Developer

**Pakprom Naennoi (Peace)**  
Electronics Engineer (Embedded Systems)

**Event:** MUT Robot Open House 2026 — **18 November 2026**  
**Firmware Version:** 1.0.0  
**Date:** 2026-11-14

## 📄 License

โปรดระบุ License ตามที่เลือกใช้จริงสำหรับ Repository ใหม่ ไม่อนุมานจาก License ที่ README ของโครงการ L298N ปี 2025 ระบุไว้
