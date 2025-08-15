#pragma once
/**
 * @file node_protocol.hpp
 * @brief Transport + protocol glue (SLIP over UART, frame boundaries, verbs/tags).
 *
 * What this layer does
 * --------------------
 * - Owns the PacketSerial (SLIP) transport on top of Serial.
 * - Emits complete *inner* frames to a callback: on_packet(const uint8_t*, size_t).
 * - Provides protocol_send() to send a complete *inner* frame (already TLV-encoded).
 *
 * What this layer does NOT do
 * ---------------------------
 * - It does not interpret TLVs or update state (that’s node_interface).
 * - It does not draw to OLED (that’s node_display).
 *
 * Default callback
 * ----------------
 * If you don’t install a handler, frames are delivered to node_interface_on_packet().
 * You can override with node_protocol_set_handler().
 */

#include "node_protocol.hpp"
#include "node_interface.hpp"
#include <Arduino.h>
#include <PacketSerial.h>
#include <cstring>   

// --------- Verbs (aligned with host commands.hpp) ---------
enum Verb : uint8_t {
  // Legacy / simple
  GET_ID    = 0x01,
  SET_ID    = 0x02,
  PING      = 0x03,

  // Messaging (move out of 0x10 range to avoid collision)
  MSG       = 0x20,

  // Generic parameterized ops (match host exactly)
  GET_PARAM = 0x10,
  SET_PARAM = 0x11,
  GET_ALL   = 0x12,

  // Responses
  RESP_OK   = 0x90,
  RESP_ERR  = 0x91
};


// --------- TLV Tags (aligned with host commands.hpp) ---------

// Identity / System
enum Tag : uint8_t {
  TAG_ID          = 0x01, // string: node id (<=31 recommended)
  TAG_ALIAS       = 0x02, // string: friendly name
  TAG_FW_VERSION  = 0x03, // string: semver
  TAG_UPTIME_S    = 0x04, // u32: seconds since boot
  TAG_BOOT_TIME   = 0x05, // u32: unix epoch seconds

  // Radio (SX127x-ish)
  TAG_FREQ_HZ     = 0x10, // u32: RF frequency in Hz
  TAG_SF          = 0x11, // u8 : spreading factor (7..12)
  TAG_BW_HZ       = 0x12, // u32: bandwidth Hz (e.g. 125000)
  TAG_CR          = 0x13, // u8 : coding rate code (5..8 => 4/5..4/8)
  TAG_TX_PWR_DBM  = 0x14, // i8 : TX power dBm
  TAG_CHAN        = 0x15, // u8 : abstract channel index

  // Behavior / Routing
  TAG_MODE        = 0x20, // u8 : 0=relay, 1=direct, 2=gateway (example)
  TAG_HOPS        = 0x21, // u8 : max hops
  TAG_BEACON_SEC  = 0x22, // u32: beacon interval seconds
  TAG_BUF_SIZE    = 0x23, // u16: outbound queue size
  TAG_ACK_MODE    = 0x24, // u8 : 0/1

  // Diagnostics (read-only)
  TAG_RSSI_DBM    = 0x30, // i16: last RX RSSI dBm
  TAG_SNR_DB      = 0x31, // i8 : last RX SNR dB
  TAG_VBAT_MV     = 0x32, // u16: supply/battery mV
  TAG_TEMP_C10    = 0x33, // i16: temperature 0.1°C units
  TAG_FREE_MEM    = 0x34, // u32: heap/free mem bytes
  TAG_FREE_FLASH  = 0x35, // u32: free flash bytes
  TAG_LOG_COUNT   = 0x36  // u16: log entries count
};

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

/// Initialize SLIP transport on Serial and install default handler.
void node_protocol_begin(unsigned long baud = 115200);

/// Pump the PacketSerial state machine (call from loop()).
void node_protocol_update();

/// Set/replace the packet handler; pass nullptr to restore default (node_interface).
void node_protocol_set_handler(void (*handler)(const uint8_t* frame, size_t len));

/// Send one complete *inner* frame (NOT SLIP-encoded by caller).
/// This function SLIP-encodes and writes to Serial.
void protocol_send(const uint8_t* frame, size_t len);

/// Convenience: send a MSG frame containing a short text string.
void node_protocol_send_text(const char* s);
