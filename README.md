# Orestes

<img width="250" height="250" alt="image" src="https://github.com/user-attachments/assets/56edf997-232e-47e6-969c-d31d36156f5a" />
</br>

> A Nintendo DS–themed cybersecurity toolkit for the Waveshare ESP32-S3-Touch-LCD-2. Scan WiFi networks, track BLE devices, map signal locations, and connect with other Orestes users via StreetPass — all from a 2-inch touchscreen in your pocket.

---

## What it does

Orestes is a portable RF recon tool built around passive WiFi and Bluetooth scanning. It runs entirely on-device with no cloud, no app, no phone required.

**WiFi Scanner** — Lists nearby networks with SSID, BSSID, signal strength, channel, encryption type, and estimated distance. Tap any network for a full detail view.

**BLE Scanner** — Discovers Bluetooth Low Energy devices in range. Shows device name, MAC address, signal strength, device type, and whether the device is connectable. Flags other Orestes users with a `[CS]` tag.

**Scan Map** — Plots scan locations on a grid using WiFi signal triangulation. Color-coded dots show signal density. Tap any dot to see what was found there.

**StreetPass** — When two Orestes devices come within range, both users get an alert. Tap Connect on both devices to complete a handshake. Points are awarded based on proximity. Collect encounter cards, unlock badges, and track everyone you've met.

---

## Hardware

| Component | Details |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-2 |
| Display | ST7789, 240×320, SPI |
| Touch | CST816S, I2C |
| MCU | ESP32-S3, dual core 240MHz, 8MB PSRAM |

---

## Dependencies

Install these libraries through Arduino IDE's Library Manager before compiling:

| Library | Source |
|---|---|
| `GFX Library for Arduino` | Waveshare bundled version (see below) |
| `bsp_cst816` | Waveshare bundled version (see below) |

The Waveshare demo package includes specific versions of both libraries tuned for this board. Do not use the generic Arduino Library Manager versions — they will not work with the correct pin configuration.

---

## Setup

### 1. Install Arduino IDE

Download from [arduino.cc](https://www.arduino.cc/en/software). On Linux, if the AppImage fails to launch:

```bash
sudo apt install libfuse2
```

### 2. Add ESP32 board support

Open **File → Preferences** and paste this into Additional Boards Manager URLs:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then go to **Tools → Board → Boards Manager**, search `esp32`, and install **esp32 by Espressif Systems** (version 3.0.0 or later).

### 3. Download the Waveshare demo package

The official demo zip contains the correct library versions. Download it from the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2) or fetch it directly:

```bash
cd ~/Downloads
wget https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2-Demo.zip
unzip ESP32-S3-Touch-LCD-2-Demo.zip
```

### 4. Install the bundled libraries

```bash
# GFX library
mkdir -p ~/Documents/Arduino/libraries/GFX_Library_for_Arduino
cp -r ~/Downloads/Arduino/libraries/GFX_Library_for_Arduino/* \
      ~/Documents/Arduino/libraries/GFX_Library_for_Arduino/

# Touch library
cp -r ~/Downloads/Arduino/libraries/bsp_cst816 \
      ~/Documents/Arduino/libraries/

# Add library.properties so Arduino IDE recognizes it
cat > ~/Documents/Arduino/libraries/bsp_cst816/library.properties << EOF
name=bsp_cst816
version=1.0.0
author=Waveshare
sentence=CST816 touch driver
paragraph=CST816 touch driver for Waveshare ESP32-S3-Touch-LCD-2
category=Other
url=https://www.waveshare.com
architectures=esp32
EOF
```

Restart Arduino IDE after copying the libraries.

### 5. Clone this repo

```bash
git clone https://github.com/yourusername/orestes.git
cd orestes
```

Or download the ZIP and extract it.

### 6. Open the sketch

In Arduino IDE: **File → Open** → navigate to `orestes/orestes.ino`

### 7. Copy the touch driver into the sketch folder

```bash
cp ~/Documents/Arduino/libraries/bsp_cst816/bsp_cst816.h orestes/
cp ~/Documents/Arduino/libraries/bsp_cst816/bsp_cst816.cpp orestes/
```

### 8. Configure the board

In Arduino IDE set the following under **Tools**:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Port | /dev/ttyACM0 (Linux) or COMx (Windows) |
| USB Mode | USB-OTG (TinyUSB) or Hardware CDC |

### 9. Fix serial port permissions (Linux only)

```bash
sudo usermod -a -G dialout $USER
```

Log out and back in for this to take effect. Or as a temporary fix:

```bash
sudo chmod 666 /dev/ttyACM0
```

### 10. Flash

Hold **BOOT**, tap **RESET**, release **BOOT** to enter download mode. Then click **Upload** in Arduino IDE.

When upload completes, tap **RESET** once more. The Orestes boot screen will appear.

---

## First boot

On first launch you'll be prompted to enter a nickname. This is shown to other Orestes users during StreetPass encounters. Use the on-screen keyboard and tap **OK** to confirm. Your nickname is saved to flash and won't be asked again.

---

## StreetPass

StreetPass uses BLE advertising with a shared service UUID. When two Orestes devices are in range, both screens pop up a handshake prompt. Both users must tap **Connect** for the encounter to register.

Points awarded per encounter:

| Signal strength | Points |
|---|---|
| −50 dBm or better | 200 |
| −51 to −70 dBm | 150 |
| −71 dBm or worse | 100 |

Encounters are saved across reboots. Cards and points persist in ESP32 flash memory.

---

## Badges

| Badge | Requirement |
|---|---|
| First Contact | Meet 1 user |
| Social | Meet 3 users |
| Networker | Meet 5 users |
| Ghost | Scan 20+ WiFi networks |
| BLE Hunter | Find 10+ BLE devices |
| Explorer | Log 5+ map locations |
| Elite | Earn 200+ points |
| Legend | Meet 8 users |

---

## Disclaimer

Orestes performs **passive scanning only**. It reads publicly broadcast radio signals the same way any phone or laptop does. It does not connect to networks without permission, capture handshakes, inject packets, or perform any active attacks.

Use responsibly and in accordance with the laws in your jurisdiction.

---

## License

MIT
