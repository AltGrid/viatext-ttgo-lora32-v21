// src/main.cpp
// ViaText Node (ESP32 TTGO LoRa32 V2.1 / 1.6.x)
// Entry-only: init subsystems, then pump the protocol loop.

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
