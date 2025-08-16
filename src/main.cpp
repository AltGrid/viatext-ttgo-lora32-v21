/**
 * @page vt-node-main ViaText Node Entry (TTGO LoRa32 V2.1 / 1.6.x)
 * @file main.cpp
 * @brief Minimal entry point: boot subsystems, then run the protocol pump.
 *
 * Purpose
 * -------
 * This file is intentionally boring. It wires up transport, command/state,
 * and an optional OLED status panel, then hands control to a cooperative
 * update loop. All heavy lifting lives in modules that can be tested or
 * swapped without touching main(). Keep this file clean so field debugging
 * is obvious and rebuilds are low-risk.
 *
 * What This File Does
 * -------------------
 * 1) Brings up SLIP over USB CDC at a fixed baud and registers the packet
 *    handler (node_protocol.*).
 * 2) Initializes node state, persistent settings, and command handlers
 *    (node_interface.*).
 * 3) Tries to initialize the optional 0.96" SSD1306 OLED and renders a
 *    boot banner and current Node ID (node_display.*).
 * 4) Emits an unsolicited hello (seq=0) so the host immediately knows the
 *    node is online.
 * 5) Enters a cooperative loop that services the SLIP state machine.
 *
 * Why It Is Structured This Way
 * -----------------------------
 * - Simplicity: main() should tell the story at a glance. No business logic,
 *   no TLV parsing, no display layout. If you need to explain it to someone
 *   over a radio, this file gives you the script.
 * - Portability: board-specific details (I2C pins, OLED address) are simple
 *   constants here; protocol, state, and rendering sit behind stable APIs.
 * - Autonomy: if the OLED is missing or fails, the node still boots and
 *   answers over serial. Display helpers no-op safely on failure.
 *
 * Hardware Context: LilyGO TTGO LoRa32 V1.6.x / V2.1
 * --------------------------------------------------
 * MCU:             ESP32 (Wi-Fi + Bluetooth 4.2)
 * Flash:           4 MB
 * USB-UART:        CH9102 / CH9102F
 * Display:         0.96" SSD1306 OLED, 128x64, I2C (typ 0x3C; fallback 0x3D)
 * I2C Pins:        SDA=21, SCL=22 (defaults used here)
 * Storage:         microSD (TF) slot present on many variants
 * Power:           USB Micro and/or Li-Po (JST-GH 2-pin 1.25 mm). USB can power the battery.
 * LoRa RF:         SX1278 (433 MHz) or SX1276 (868/915 MHz) depending on SKU
 * Antenna:         External LoRa antenna supported; some variants require a resistor jumper
 * Useful RF bits:  Digital RSSI, AFC, AGC, fast wake, frequency hopping,
 *                  configurable packet handler
 *
 * Transport Details
 * -----------------
 * - Host link: USB CDC serial with SLIP framing (PacketSerial).
 * - Default baud: 115200.
 * - Inner frame: [verb][flags][seq][len][TLVs...] handled upstream.
 *
 * Function Map
 * ------------
 * void setup():
 *   - node_protocol_begin(115200)
 *       Initialize USB CDC + SLIP transport.
 *   - node_protocol_set_handler(node_interface_on_packet)
 *       Route complete inner frames to the interface layer.
 *   - node_interface_begin()
 *       Load persisted settings (ID, radio params, behavior) and arm handlers.
 *   - node_display_begin(21, 22, 0x3C)
 *       Attempt OLED init (probes 0x3C, then 0x3D). On success, draw a simple
 *       boot banner and show the current Node ID.
 *   - node_interface_send_hello()
 *       Fire a seq=0 RESP_OK with ID so the host can register presence without
 *       waiting for a poll.
 *
 * void loop():
 *   - node_protocol_update()
 *       Pump SLIP; on complete frames the registered callback runs.
 *   - (optional) yield() or other non-blocking tasks
 *       Add cooperative work here. Never block; keep the radio/serial path hot.
 *
 * Operational Notes
 * -----------------
 * - Headless operation: If the display init fails, the node still runs. All
 *   display calls are guarded and become no-ops.
 * - Serial monitoring: `pio device monitor --baud 115200` to watch logs and
 *   confirm boot/hello traffic.
 * - Ports: On Linux, boards usually enumerate as /dev/ttyACM* or /dev/ttyUSB*.
 * - Upload tips: If upload stalls, tap EN/BOOT per ESP32 convention and retry.
 *
 * Extending Behavior
 * ------------------
 * - Add new verbs/tags in node_protocol.hpp, implement handling in
 *   node_interface.cpp, and consider a minimal display hook if it is truly
 *   user-visible. Do not expand main(); keep linkage here limited to wiring.
 *
 * Safety and Failure Mode Bias
 * ----------------------------
 * - Default to operating with minimal peripherals. Lack of OLED, SD, or RTC
 *   must not prevent serial bring-up or command handling.
 * - Prefer small static buffers and defensive bounds checks in leaf modules.
 *
 * Example Bring-Up (Host)
 * -----------------------
 *   pio run -t upload
 *   pio device monitor --baud 115200
 *   # Expect: boot banner, Node ID on OLED (if present), and an unsolicited hello on serial
 *
 * @author Leo
 * @author ChatGPT
 */

#include <Arduino.h>
#include "node_protocol.hpp"   // node_protocol_begin, node_protocol_set_handler, node_protocol_update
#include "node_interface.hpp"  // node_interface_begin, node_interface_on_packet, node_interface_send_hello, node_interface_id
#include "node_display.hpp"    // node_display_begin, node_display_draw_boot, node_display_draw_id

// TTGO LoRa32 I2C pins
static constexpr int I2C_SDA_PIN = 21;
static constexpr int I2C_SCL_PIN = 22;

void setup() {
  // 1) Transport (SLIP over USB CDC)
  node_protocol_begin(115200);
  node_protocol_set_handler(node_interface_on_packet);   // deliver frames to interface

  // 2) Interface (persistent state, command handlers)
  node_interface_begin();

  // 3) Display (node_display_begin tries addr, then 0x3D as fallback)
  if (node_display_begin(I2C_SDA_PIN, I2C_SCL_PIN, 0x3C)) {
    node_display_draw_boot("ViaText Booting...");
    node_display_draw_id(node_interface_id());
  }

  // 4) Unsolicited hello (seq=0) so the host knows we're up
  node_interface_send_hello();
}

void loop() {
  // Pump SLIP; will call node_interface_on_packet() on full frames
  node_protocol_update();

  // add other cooperative (non-blocking) tasks here if needed
  // yield();  // optional
}
