# 🤖 MUT Robot 2026 — ESP32-S3 Web Communication Controller

![Version](https://img.shields.io/badge/version-1.0.0-blue) ![Framework](https://img.shields.io/badge/framework-Arduino-00979D) ![MCU](https://img.shields.io/badge/MCU-ESP32--S3-555555)

เฟิร์มแวร์สำหรับ **ESP32-S3 N16R8** ทำหน้าที่เป็น Wi-Fi Access Point และ Web Controller สำหรับหุ่นยนต์ โดยให้ผู้ใช้ควบคุมผ่าน Web App (HTML/CSS/JavaScript) ที่เก็บใน LittleFS ส่งคำสั่งผ่าน WebSocket ไปยัง ESP32-S3 เพื่อคำนวณ Differential Drive และส่ง UART Packet พร้อม CRC-8 ไปยัง **STM32F446RCTx Motor Controller**

พัฒนาสำหรับ **MUT Robot Open House 2026 (18 November 2026)**

> **Architecture:** ESP32-S3 รับคำสั่งและคำนวณความเร็ว/ทิศทางมอเตอร์ ส่วน STM32F446RCTx สร้าง PWM และขับมอเตอร์ผ่าน DRV8833PWR

## ✨ Features / คุณสมบัติ

- Wi-Fi **Access Point (AP)** ชื่อ `MUT_robot_XXXX` โดยใช้ 16 บิตล่างของ eFuse MAC
- จำกัด Wi-Fi AP สูงสุด **1 Device (Single Station)** บน Channel 1
- จำกัด WebSocket **1 Active Controller** เพื่อป้องกันการควบคุมซ้อนกัน

- **HTTP Web Server (Port 80)** สำหรับให้บริการ Web App จาก **LittleFS**

- **WebSocket Server (Port 81)** สำหรับรับคำสั่ง `[X,Y]` จาก D-Pad และ Virtual Joystick

- Web App ออกแบบให้ใช้งานในแนวนอน (**Landscape**) พร้อมไฟล์ PWA Manifest สำหรับอุปกรณ์/เบราว์เซอร์ที่รองรับ

- ปรับ Speed ได้ตั้งแต่ 0–100% และส่งคำสั่งซ้ำทุก **100 ms** ระหว่างควบคุม

- Differential Drive (Tank Mixing) พร้อม Normalize ความเร็วไม่เกิน 100%

- UART ไป STM32 ที่ **115200 baud, 8N1** และ Packet **8 bytes** พร้อม **CRC-8 polynomial `0x07`**

- ส่ง STOP เมื่อปล่อยปุ่ม/Joystick, รับ `[0,0]`, หรือ WebSocket Client ตัดการเชื่อมต่อ

- **Command Timeout Failsafe 350 ms** ขณะมอเตอร์กำลังทำงาน

- NeoPixel RGB แสดงสถานะ Wi-Fi, Client, Motor และ Failsafe

- อนุญาต WebSocket Controller ที่ใช้งานอยู่ **1 Client**; ปฏิเสธ Client เพิ่มด้วยข้อความ `BUSY`

## 🧠 System Architecture

```text

Mobile / PC Browser

  └── Robot Web App (HTML / CSS / JavaScript)

        ├── D-Pad / Virtual Joystick / Speed

        └── WebSocket [X,Y] (Port 81)

                    |

             Wi-Fi Access Point (Channel 1 / Max 1 Device)

                    |

              ESP32-S3 N16R8

              ├── HTTP :80 / LittleFS

              ├── WebSocket :81

              ├── Joystick Parser

              ├── Differential Drive

              ├── Safety / Failsafe

              └── UART Packet + CRC-8

                    |

            UART1 TX GPIO17

            115200 baud, 8N1

                    |

            STM32 USART1 RX

                    |

             STM32F446RCTx

             PWM TIM3 / TIM4

                    |

               DRV8833PWR

                    |

               DC Motors ×2

```

## 🛠 Hardware / ฮาร์ดแวร์

| Component | Description |
|---|---|
| Main MCU | ESP32-S3 N16R8 |
| Motor Controller | STM32F446RCTx (separate board) |
| Motor Driver | DRV8833PWR (controlled by STM32) |
| Status LED | On-board NeoPixel, GPIO48 |
| Debug | USB Serial Monitor, 115200 baud |

### 🔌 UART Wiring

| ESP32-S3 | STM32F446RCTx | Purpose |
|---|---|---|
| GPIO17 (UART1 TX) | STM32 USART1 RX (verify board pin assignment) | Motor control packets |
| GPIO18 (UART1 RX) | STM32 USART1 TX (verify board pin assignment) | Optional reverse path; not used by current logic |
| GND | GND | Common signal ground |

> ใช้ UART ระดับ **3.3 V TTL** และต่อ GND ร่วมกัน ห้ามต่อกับสัญญาณ RS-232 โดยตรง ตรวจสอบตำแหน่งขา USART1 บนบอร์ด STM32 จริงก่อนต่อสาย (สำหรับ STM32F446 โดยทั่วไป PA9 เป็น USART1_TX และ PA10 เป็น USART1_RX)

## 📶 Wi-Fi & Web Configuration

| Parameter | Value |
|---|---|
| Wi-Fi mode | `WIFI_AP` |
| Wi-Fi Channel | `1` |
| Maximum AP Clients | `1` Device (Single Station) |
| SSID | `MUT_robot_XXXX` (derived from eFuse MAC) |
| Password | `12345678` |
| Default AP IP used by Web App | `192.168.4.1` (verify in Serial Monitor) |
| HTTP Port | `80` |
| WebSocket Port | `81` |
| WebSocket Command | `[X,Y]` (text message, no newline required) |
| Command Range | Integer `-100` to `100` per axis |
| Maximum active WebSocket Controller | `1` Connection |
| Command Timeout | 350 ms |
| Web App Repeat Interval | 100 ms |
| Periodic UART Heartbeat / STOP | 100 ms while client connected |

หลังเชื่อมต่อ Wi-Fi ของหุ่นยนต์ ให้เปิด `http://192.168.4.1/` หรือใช้ IP ที่แสดงใน Serial Monitor จากนั้นระบุ IP และ **WebSocket Port 81** ใน Web App แล้วกด **Connect Robot**

> แอป Flutter เดิมใช้ TCP Socket Port 8080 แต่ **Firmware ปัจจุบันใช้ WebSocket Port 81** จึงไม่สามารถใช้ TCP Client เดิมเชื่อมต่อแทน WebSocket ได้โดยตรง

### 🔒 Single Device Connection Policy

ระบบจำกัดการเชื่อมต่อ **Wi-Fi AP สูงสุด 1 Device** ด้วย `WIFI_MAX_CLIENTS = 1` และใช้ `WIFI_AP_CHANNEL = 1` เพื่อป้องกันอุปกรณ์หลายเครื่องเชื่อมต่อ AP พร้อมกัน ขณะที่ WebSocket Server ยังคงอนุญาต **Active Controller สูงสุด 1 Connection** โดยปฏิเสธ Connection เพิ่มด้วยข้อความ `BUSY`

- เมื่อ Device A เชื่อมต่อ Wi-Fi AP แล้ว Device B จะไม่สามารถเชื่อมต่อ AP เพิ่มได้ในขณะที่ช่องเชื่อมต่อเต็ม
- เมื่อ Device A ตัด Wi-Fi และ AP ตรวจพบการตัดการเชื่อมต่อ Device อื่นจึงสามารถเชื่อมต่อได้
- เมื่อ Active WebSocket Controller ตัดการเชื่อมต่อ Firmware จะส่งคำสั่ง STOP ตาม Logic เดิมและคืนสิทธิ์ให้ WebSocket Connection ใหม่
- การจำกัดจำนวน Device **ไม่ใช่การยืนยันตัวตนหรือการผูกสิทธิ์กับผู้ใช้ถาวร** ผู้ที่ทราบรหัสผ่านยังสามารถเชื่อมต่อได้เมื่อ AP ว่าง
- การตัด Wi-Fi ไม่จำเป็นต้องทำให้ WebSocket Disconnect ถูกตรวจพบทันที จึงยังต้องอาศัย Failsafe เดิมและ Motor Timeout ของ STM32

**การตั้งค่าใน `main.cpp`:**

```cpp
#define WIFI_AP_CHANNEL 1U
#define WIFI_MAX_CLIENTS 1U

WiFi.mode(WIFI_AP);
if (!WiFi.softAP(ssid, password, WIFI_AP_CHANNEL, 0, WIFI_MAX_CLIENTS)) {
    Serial.println("[WIFI] AP failed");
    // ใช้ Error Handler เดิมของ Firmware
}
```

> ตัวอย่างด้านบนแสดงเฉพาะการกำหนด AP Parameters; ให้คง Error Handler และ LED Blink ของ Firmware เดิมไว้

### 📱 Landscape / PWA

Web App ออกแบบสำหรับการใช้งานแนวนอน (Landscape) โดยกำหนด `"orientation": "landscape"` และ `"display": "fullscreen"` ใน `manifest.webmanifest` สำหรับอุปกรณ์ที่รองรับ PWA ทั้งนี้ **การเปิดผ่าน Browser ปกติไม่รับประกันว่าจะล็อกแนวนอนได้** และ HTTP ของ ESP32 อาจมีข้อจำกัดด้านการติดตั้ง PWA หากจำเป็นให้หมุนหน้าจอด้วยตนเอง

## 🎮 Joystick Command Format

WebSocket text message:

```text

[X,Y]

```

**X = เดินหน้า/ถอยหลัง**, **Y = เลี้ยวขวา/ซ้าย** โดยใช้ Mapping ตามแอป Flutter ต้นฉบับ

| Control | X | Y | Message at 100% Speed |
|---|---:|---:|---|
| เดินหน้า | +100 | 0 | `[100,0]` |
| ถอยหลัง | -100 | 0 | `[-100,0]` |
| เลี้ยวซ้าย | 0 | -100 | `[0,-100]` |
| เลี้ยวขวา | 0 | +100 | `[0,100]` |
| STOP | 0 | 0 | `[0,0]` |

Web App ปรับค่าคำสั่งตาม Speed Slider โดย D-Pad และ Virtual Joystick ใช้รูปแบบเดียวกัน ตารางนี้แสดง **Mapping ของคำสั่ง** ส่วนทิศทางล้อจริงขึ้นอยู่กับการตั้งค่า STM32 และการต่อสายมอเตอร์ด้วย

### Differential Drive Calculation

```cpp

int left  = x + y;

int right = x - y;

int peak  = max(abs(left), abs(right));

if (peak > 100) {

    left  = left  * 100 / peak;

    right = right * 100 / peak;

}

```

เครื่องหมายของผลลัพธ์กำหนดทิศทาง `FORWARD`/`REVERSE` และค่าสัมบูรณ์กำหนดความเร็ว 0–100% โดย ESP32 ส่ง **คำสั่งมอเตอร์ที่คำนวณแล้ว** ไปยัง STM32 ไม่ได้ส่งค่า X/Y ต้นฉบับ

## 🔄 ESP32 ↔ STM32 UART Protocol

UART Packet มีขนาด **8 ไบต์** ดังนี้

| Byte | Field | Description |
|---|---|---|
| 0 | Header | `0xA5` |
| 1 | Command | `0x01` MOTOR, `0x02` STOP, `0x03` HEARTBEAT |
| 2 | M1 Speed | `0–100` |
| 3 | M1 Direction | `0` Coast, `1` Forward, `2` Reverse |
| 4 | M2 Speed | `0–100` |
| 5 | M2 Direction | `0` Coast, `1` Forward, `2` Reverse |
| 6 | Reserved | `0x00` |
| 7 | CRC-8 | Poly `0x07`, init `0x00`, bytes 0–6 |

CRC-8 ใช้รูปแบบ Non-reflected และไม่มี Final XOR

| Command | Hex | Behavior |
|---|---|---|
| `CMD_SET_MOTOR` | `0x01` | Motor speed and direction |
| `CMD_STOP` | `0x02` | Stop both motors |
| `CMD_HEARTBEAT` | `0x03` | Periodic communication indicator while running |

> Motor Timeout ฝั่ง STM32 อาจพิจารณาจากเวลาที่ได้รับ `CMD_SET_MOTOR` ล่าสุด ไม่ใช่ `CMD_HEARTBEAT` ให้ตรวจสอบเงื่อนไขจริงจาก Firmware ของ STM32 Motor Controller

## 🛡 ระบบความปลอดภัยและ Failsafe

1. เมื่อรับ `[0,0]` จะส่ง `CMD_STOP` ทันที

2. Web App ส่ง `[0,0]` เมื่อปล่อย D-Pad/Joystick หรือเมื่อหน้าเว็บสูญเสีย Focus หรือถูกซ่อนขณะเชื่อมต่อ

3. เมื่อ WebSocket หลุดการเชื่อมต่อ ESP32 จะสั่งหยุดมอเตอร์

4. หากมอเตอร์กำลังทำงานและไม่ได้รับคำสั่งใหม่ภายใน **350 ms** ESP32 จะส่ง STOP และเข้าสถานะ Failsafe LED

5. ระหว่างมี Controller เชื่อมต่อ ESP32 ส่ง `CMD_HEARTBEAT` ทุก **100 ms** เมื่อมอเตอร์ทำงาน หรือส่ง `CMD_STOP` ซ้ำทุก **100 ms** เมื่อหยุด

6. ระบบความปลอดภัยฝั่ง STM32 ทำงานแยกจาก ESP32 และควรตรวจสอบกับ Firmware ของ STM32 โดยตรง

> เพื่อความปลอดภัย ควรยกล้อขึ้นทดสอบทิศทางก่อนนำหุ่นยนต์ลงพื้น การปิด Browser หรือสัญญาณ Wi-Fi หลุดอาจทำให้ส่ง STOP ครั้งสุดท้ายไม่สำเร็จ จึงต้องอาศัย Timeout/Disconnect Failsafe ของ ESP32 เป็นระบบสำรอง

## 🌈 NeoPixel Status (GPIO48)

| State | Color / Pattern | Meaning |
|---|---|---|
| `LED_WIFI_READY` | Blue | Wi-Fi AP พร้อมใช้งาน |
| `LED_CONNECTED` | Green | Controller เชื่อมต่อ มอเตอร์หยุด |
| `LED_RUNNING` | สีฟ้า | มอเตอร์ทำงาน |
| `LED_FAILSAFE` | สีแดงกะพริบ | คำสั่ง Joystick หมดเวลา |

กำหนดความสว่าง LED ไว้ที่ `40`

## 💻 เครื่องมือพัฒนาและไลบรารี

- **Language:** C++

- **Framework:** Arduino Framework for ESP32

- **IDE:** Visual Studio Code + PlatformIO

- **ESP32 libraries:** `Arduino.h`, `WiFi.h`, `WebServer.h`, `WebSocketsServer.h`, `LittleFS.h`, `Adafruit_NeoPixel.h`

- **Web App:** HTML, CSS, JavaScript (stored in LittleFS)

- **UART:** `HardwareSerial(1)`

- **Debug:** USB Serial Monitor, 115200 baud

โปรเจกต์ใช้ `platformio.ini` สำหรับกำหนดบอร์ด ไลบรารี Partition และ LittleFS โดย **ควรคงค่าที่ทดสอบกับ ESP32-S3 N16R8 แล้ว** ไม่เปลี่ยนเป็นค่าตัวอย่างทั่วไป

## 📂 โครงสร้างโปรเจกต์

```text

ESP32_S3/

├── data/

│   ├── index.html

│   ├── style.css

│   ├── app.js

│   └── manifest.webmanifest

├── include/

├── lib/

├── src/

│   └── main.cpp

├── test/

├── platformio.ini

└── README.md

```

ไฟล์ `manifest.webmanifest` ใช้สำหรับ PWA โดย Firmware ต้องมี HTTP Route สำหรับให้บริการไฟล์นี้

## 🚀 การ Build, Upload และใช้งาน (PlatformIO)

เปิดโฟลเดอร์โปรเจกต์ใน VS Code ที่ติดตั้ง PlatformIO แล้ว

**1. Build Firmware**

```bash

pio run

```

**2. Upload Firmware** (เมื่อแก้ไข `main.cpp` หรือการตั้งค่า Firmware)

```bash

pio run -t upload

```

**3. Upload LittleFS Web App** (เมื่อแก้ไขไฟล์ใน `data/`)

```bash

pio run -t uploadfs

```

**4. เปิด Serial Monitor**

```bash

pio device monitor -b 115200

```

**5. เชื่อมต่อและควบคุมหุ่นยนต์**

1. เชื่อมต่อมือถือหรือคอมพิวเตอร์ **เพียง 1 Device** กับ Wi-Fi `MUT_robot_XXXX` รหัสผ่าน `12345678`

2. เปิด `http://192.168.4.1/` (หรือ IP จริงของ AP)

3. หมุนหน้าจอเป็นแนวนอนหากจำเป็น

4. ตรวจสอบ WebSocket Port `81` แล้วกด **Connect Robot**

5. ใช้ D-Pad/Joystick และ Speed Slider เพื่อควบคุม เมื่อปล่อยจะส่ง STOP

ตัวอย่าง Serial Monitor Log (เพื่ออธิบายรูปแบบ ไม่ใช่ Log ที่ยืนยันจากการทดสอบ):

```text

[FS] LittleFS mounted OK

[WIFI] Max AP Clients: 1
[WIFI] SSID: MUT_robot_XXXX IP: 192.168.4.1

[HTTP] Port 80 / [WebSocket] Port 81

[WS] Controller 0 connected

[APP] X:100 Y:0

[APP] X:0 Y:0

[MOTOR] STOP -> STM32

```

> หาก Web App ไม่แสดงหรือยังเป็นเวอร์ชันเก่า ให้ตรวจสอบการ Mount LittleFS และรัน `pio run -t uploadfs` หาก Browser Cache ค้าง ให้ Reload หน้าเว็บ

## 🔗 โปรเจกต์ที่เกี่ยวข้อง

- **STM32F446RCTx Motor Controller:** รับ UART Packet ตรวจสอบ CRC และควบคุม DRV8833PWR

- **โครงการก่อนหน้า (2025):** [MUT_Robot_L298N](https://github.com/peacepaul1999/MUT_Robot_L298N) — โปรเจกต์เดิมที่ใช้ L298N ซึ่งแตกต่างจาก Motor Driver ของเวอร์ชัน 2026

## 👨‍🔧 Developer

**Pakprom Naennoi (Peace)**  

Electronics Engineer (Embedded Systems)

**Event:** MUT Robot Open House 2026 — **18 November 2026**  

**Firmware Version:** 1.0.0  

**README Updated:** 2026-11-16

## 📄 License / สัญญาอนุญาต

ให้ระบุ License ตามที่เลือกใช้จริงสำหรับ Repository นี้ โดยไม่อ้างอิง License จากโปรเจกต์ก่อนหน้าโดยอัตโนมัติ
