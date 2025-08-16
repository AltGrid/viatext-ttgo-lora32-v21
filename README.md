# ViaText TTGO LoRa32 Node

ViaText is an **off-grid, Linux-first messaging system** developed under the [AltGrid](https://github.com/AltGrid) project family.  
This repository provides the **firmware for ESP32 TTGO LoRa32 V2.1 boards** with optional OLED display. It implements a minimal,
plaintext-based protocol designed for resilient communication without reliance on phones or cloud infrastructure.

It is used directly with https://github.com/AltGrid/viatext-core. 

Viatext Core (the communications manager, Linux) is used to manage the node for flexible human messaging and IoT applications. NOTE: This node is useless without ViaText core. Just sayin'. 

> Build like it’s 1986. Communicate like it’s 2086.  
> **Simplicity · Portability · Autonomy**

---

## Features

- **Simplicity**: Human-readable messages with minimal dependencies.  
- **Portability**: Runs on ESP32 TTGO LoRa32 V2.1 hardware (battery, USB, OLED).  
- **Autonomy**: Works in mesh only; no phone or cloud required.  
- **Linux Compatibility**: Full integration with `/dev/ttyUSB*`, scriptable from CLI.  

### Node Components

- **node_protocol**: SLIP/Serial transport over USB (framing, encoding, handler dispatch).  
- **node_interface**: High-level node brain (persistent state, ID, parameter handling, TLV I/O).  
- **node_display**: Minimal OLED UI helpers (optional 0.96" SSD1306 screen).  

### Supported Operations (Verbs)

- `GET_ID` / `PING` – Check identity or reachability  
- `SET_ID` – Assign a new node ID (persisted in NVS)  
- `GET_PARAM` / `SET_PARAM` – Read/write parameters (freq, SF, CR, TX power, etc.)  
- `GET_ALL` – Bulk read of node state and diagnostics  
- `MSG` – Transmit a short text message  

---

## Project Setup: PlatformIO on Linux

This firmware is built using **PlatformIO** from the command line.

### Prerequisites

- Linux system (Debian, Ubuntu, Fedora, Arch)  
- Python 3.7+  
- USB serial support (usually built into Linux)  
- `git` installed  

Verify:

```bash
python3 --version
git --version
```

### Install PlatformIO CLI

```bash
python3 -m pip install --user platformio
echo 'export PATH=$PATH:$HOME/.local/bin' >> ~/.bashrc
source ~/.bashrc
pio --version
```

### Clone and Build

```bash
git clone https://github.com/AltGrid/viatext-ttgo-lora32-v21.git
cd viatext-ttgo-lora32-v21
pio run
```

### Upload to Device

Connect TTGO LoRa32 via USB:

```bash
pio run --target upload
```

Or specify a port:

```bash
pio run --target upload --upload-port /dev/ttyUSB0
```

### Monitor Serial Output

```bash
pio device monitor --baud 115200
```

Exit monitor with **Ctrl+]**.

---

## Typical Usage

```cpp
if (node_display_begin(21, 22, 0x3C)) {
    node_display_draw_boot("ViaText Booting...");
    node_display_draw_id(node_interface_id());
} else {
    // Run headless; node still operates without a display
}
```

Linux host can interact via serial (`/dev/ttyUSB*`), using CLI tools from the
parent [viatext-core](https://github.com/AltGrid/viatext-core) project.

---

## License

MIT License. Written for clarity, portability, and long-term resilience.  

