### Project Setup: PlatformIO on Linux

*Documented for: ViaText LoRa Node builds*  
*Audience: Anyone with a Linux shell, a microcontroller, and the need to
build firmware.*

-----

## Purpose

This document explains how to set up **PlatformIO** on a **common Linux
environment** so you can build and flash ViaText firmware for LoRa nodes
(ESP32 TTGO LoRa32 V2.1 and similar).  
This guide focuses on using the command-line interface (CLI) to get code compiled, uploaded, and running efficiently.

-----

## Prerequisites

  -   A working Linux distribution (Debian, Ubuntu, Fedora, Arch all
        supported).\\
  -   Python 3.7 or newer.\\
  -   USB drivers for your board (on Linux, usually `cdc_acm` or `ch341`
        kernel modules are already present).\\
  -   Basic shell tools (`git`).

Verify:

```bash
python3 --version
git --version
```

-----

## Step 1. Install PlatformIO Core (CLI)

PlatformIO can run entirely from the shell, without a heavyweight IDE.

```bash
# Recommended: install in user-local pip
python3 -m pip install --user platformio
```

Then add pip's local bin path to your shell:

```bash
echo 'export PATH=$PATH:$HOME/.local/bin' >> ~/.bashrc
source ~/.bashrc
```

Verify:

```bash
pio --version
```

-----

## Step 2. Grant USB Device Permissions

A common issue is not having the correct permissions to access the USB serial port. Add your user to the `dialout` group to fix this.

```bash
sudo usermod -a -G dialout $USER
```

*Note: You may need to log out and log back in for this change to take effect.*

-----

## Step 3. Clone the Firmware Project

```bash
git clone https://github.com/AltGrid/viatext-ttgo-lora32-v21.git
cd viatext-ttgo-lora32-v21
```

The project root should contain `platformio.ini`, which defines the build environment and dependencies.

-----

## Step 4. Build the Firmware

```bash
pio run
```

PlatformIO will automatically download and cache the necessary toolchains and libraries based on the `platformio.ini` file.

-----

## Step 5. Upload to Device

Connect the TTGO LoRa32 via USB. Then:

```bash
pio run --target upload
```

If multiple devices are connected, specify the port:

```bash
pio run --target upload --upload-port /dev/ttyUSB0
```

-----

## Step 6. Monitor Serial Output

```bash
pio device monitor --baud 115200
```

This opens a serial terminal to view boot logs, debug output, and other serial communications.

Exit with **Ctrl+]**.

-----

## Step 7. Useful Commands

  -   **Clean build artifacts**

    `bash     pio run --target clean    `

  -   **List devices**

    `bash     pio device list    `

  -   **Rebuild everything**

    `bash     pio run -t clean -t build    `

-----

*Filed for long-term operability.*  
