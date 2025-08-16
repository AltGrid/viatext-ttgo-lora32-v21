#pragma once
/**
 * @page vt-node-protocol ViaText Node Protocol Transport (SLIP over Serial)
 * @file node_protocol.hpp
 * @brief Transport and frame glue: SLIP framing on UART, verb/tag boundaries, and handler dispatch.
 *
 * Overview
 * --------
 * This module is the narrow waist between raw bytes and actionable frames.
 * It owns the serial link (USB CDC) with SLIP framing, converts byte streams
 * into complete inner frames, and forwards them to a single packet handler.
 * In the other direction, it SLIP-encodes caller-supplied frames and writes
 * them to the wire. It does not interpret tags or mutate node state.
 *
 * Design Objectives
 * -----------------
 * - Simplicity: a thin, reliable pipe. No TLV parsing here.
 * - Portability: Arduino + PacketSerial today, swappable tomorrow. Keep the
 *   boundary clean so alternative transports can slot in with the same API.
 * - Autonomy: the serial path must stay hot. The update pump is fast, non-
 *   blocking, and safe to call in every loop() tick.
 *
 * Where It Sits
 * -------------
 * - Below: hardware UART/USB CDC and SLIP (PacketSerial).
 * - Above: node_interface.* (verb/TLV interpretation, persistence, responses).
 *
 * Inner Frame Format (post-SLIP)
 * ------------------------------
 * Bytes delivered to handlers are "inner frames" with a fixed 4-byte header:
 *
 *   [0] verb      : uint8   operation code (GET_ID, SET_PARAM, etc.)
 *   [1] flags     : uint8   reserved for future use (0 for now)
 *   [2] seq       : uint8   sequence number (host chosen; 0 allowed for unsolicited)
 *   [3] tlv_len   : uint8   number of bytes that follow as TLV payload
 *   [4..] TLVs    : sequence of Tag/Len/Value triplets (for verbs that use TLV)
 *
 * TLV Encoding Rules (used upstream, documented here for clarity)
 * --------------------------------------------------------------
 *   +--------+--------+-------------+
 *   | tag(1) | len(1) | value(len) |
 *   +--------+--------+-------------+
 * Numeric values are little-endian. Strings are raw bytes (no trailing NUL).
 *
 * Callback Model
 * --------------
 * - node_protocol_set_handler(cb) installs a function that receives complete
 *   inner frames. If no handler is set, frames are delivered to
 *   node_interface_on_packet() by default.
 * - node_protocol_update() pumps PacketSerial. Call it from loop() to process
 *   incoming bytes and fire the handler when a full frame is assembled.
 *
 * Outbound Path
 * -------------
 * - protocol_send(frame, len) writes a prebuilt inner frame. This function
 *   handles SLIP encoding and serial write. The caller is responsible for
 *   building the 4-byte header and any TLVs correctly.
 * - node_protocol_send_text("...") is a convenience to emit a quick MSG
 *   frame with raw payload for demos/status. For production, prefer building
 *   explicit TLVs with seq/flags semantics.
 *
 * Verbs and Tags
 * --------------
 * - The Verb and Tag enums declared in this header are the shared contract
 *   with the host-side command layer. Keep them synchronized with the host
 *   repo. Adding new verbs/tags is allowed so long as values do not collide.
 *
 * Default Limits and Behavior
 * ---------------------------
 * - Baud rate: 115200 by default (configurable at begin()).
 * - Handler: single function pointer. Use your own multiplexer if you need
 *   to fan out by verb.
 * - MTU: bounded by PacketSerial buffers and your stack. Keep frames compact.
 * - Backpressure: PacketSerial buffers writes; callers should avoid long
 *   bursts without pacing. Consider small sleeps/yield on host side.
 *
 * Error Handling Philosophy
 * -------------------------
 * - All input is untrusted. Framing errors are isolated by SLIP; inner
 *   frames are passed only when they parse cleanly within buffer bounds.
 * - This layer never throws or halt-loops on malformed data. It drops or
 *   ignores and moves on. Higher layers can respond with RESP_ERR.
 *
 * Typical Usage
 * -------------
 * @code
 * // setup()
 * node_protocol_begin(115200);
 * node_protocol_set_handler(node_interface_on_packet);
 *
 * // loop()
 * node_protocol_update(); // fires handler on complete frames
 *
 * // send a complete frame you built elsewhere:
 * uint8_t out[64];
 * size_t  i = 0;
 * out[i++] = Verb::GET_PARAM;  // verb
 * out[i++] = 0;                // flags
 * out[i++] = 42;               // seq
 * out[i++] = 0;                // tlv_len (will fill after TLVs)
 * // ... append TLVs ...
 * out[3] = static_cast<uint8_t>(i - 4); // tlv_len
 * protocol_send(out, i);
 * @endcode
 *
 * Extending the Transport
 * -----------------------
 * - If you replace PacketSerial or move off USB CDC, preserve the same API:
 *   begin(), update(), set_handler(), protocol_send(). Keep the inner frame
 *   contract intact so node_interface remains unchanged.
 *
 * Field Notes
 * -----------
 * - Keep loop() light. If you block, you lose frames. If you must work, slice
 *   it into short non-blocking steps and share time with node_protocol_update().
 * - When debugging on Linux, use `pio device monitor --baud 115200` and
 *   confirm that unsolicited hello frames appear on boot.
 *
 * Security Note
 * -------------
 * - This layer offers no authentication. It is a link-level transport. If
 *   you route it beyond a trusted cable, add a secure wrapper or gate verbs
 *   in node_interface.
 *
 * @author Leo
 * @author ChatGPT
 */

#include "node_protocol.hpp"   // Core protocol state machine: handles ticks, updates, dispatch
#include "node_interface.hpp"  // Hardware-specific I/O hooks: radio, serial, display integration
#include <Arduino.h>           // Arduino framework core (pins, millis, Serial, etc.)
#include <PacketSerial.h>      // Lightweight SLIP-style packet framing over serial
#include <cstring>             // Standard C string helpers (memcpy, strlen, etc.)


// -----------------------------------------------------------------------------
// Verbs (command opcodes shared between host and node)
// -----------------------------------------------------------------------------
// Each "verb" is the first byte in a frame, telling the node what to do.
// These values must stay in sync with the host-side commands.hpp.
// -----------------------------------------------------------------------------

/**
 * @enum Verb
 * @brief Operation codes for ViaText inner frames.
 *
 * Verbs select the action the node should perform. They occupy byte [0]
 * of the inner frame and must match the host-side contract.
 *
 * Typical flow:
 *  - Host sends a verb (optionally with TLVs)
 *  - Node replies with RESP_OK or RESP_ERR (and TLVs if applicable)
 */
enum Verb : uint8_t {
  /** @brief Ask the node for its current ID. */
  GET_ID    = 0x01,

  /** @brief Set/replace the node ID (expects TAG_ID string). */
  SET_ID    = 0x02,

  /** @brief Reachability check. Node responds with RESP_OK (+TAG_ID). */
  PING      = 0x03,

  // Messaging (kept out of 0x10 param range to avoid collisions)
  /** @brief Carry a short text message payload to the node. */
  MSG       = 0x20,

  // Generic parameterized ops (read/write TLVs)
  /** @brief Read specific tags (send tags with len=0 to request values). */
  GET_PARAM = 0x10,

  /** @brief Write specific tags (send TLVs with values). */
  SET_PARAM = 0x11,

  /** @brief Read a broad set of tags; used for initial sync/diagnostics. */
  GET_ALL   = 0x12,

  // Standard response codes
  /** @brief Success response (payload may include returned TLVs). */
  RESP_OK   = 0x90,

  /** @brief Error response (malformed/unsupported/denied). */
  RESP_ERR  = 0x91
};


// -----------------------------------------------------------------------------
// TLV Tags (aligned with host commands.hpp)
// -----------------------------------------------------------------------------
// Tags identify specific pieces of state inside TLV payloads.
// These must remain in sync with the host-side definitions.
// -----------------------------------------------------------------------------

/**
 * @enum Tag
 * @brief TLV identifiers for ViaText node state (Identity, Radio, Behavior, Diagnostics).
 */
enum Tag : uint8_t {
  // ---------------- Identity / System ----------------

  /** Node ID string (<=31 characters recommended). */
  TAG_ID          = 0x01,

  /** Friendly name / human-readable alias string. */
  TAG_ALIAS       = 0x02,

  /** Firmware version string (semantic version). */
  TAG_FW_VERSION  = 0x03,

  /** Uptime in seconds since boot (unsigned 32-bit). */
  TAG_UPTIME_S    = 0x04,

  /** Boot time as Unix epoch seconds (unsigned 32-bit). */
  TAG_BOOT_TIME   = 0x05,

  // ---------------- Radio (SX127x-ish) ----------------

  /** RF frequency in Hz (unsigned 32-bit). */
  TAG_FREQ_HZ     = 0x10,

  /** LoRa spreading factor (valid range: 7..12). */
  TAG_SF          = 0x11,

  /** Bandwidth in Hz (e.g., 125000). */
  TAG_BW_HZ       = 0x12,

  /** Coding rate code (5..8 => 4/5..4/8). */
  TAG_CR          = 0x13,

  /** Transmit power in dBm (signed 8-bit). */
  TAG_TX_PWR_DBM  = 0x14,

  /** Abstract channel index (unsigned 8-bit). */
  TAG_CHAN        = 0x15,

  // ---------------- Behavior / Routing ----------------

  /** Node mode: 0=relay, 1=direct, 2=gateway (example). */
  TAG_MODE        = 0x20,

  /** Maximum hop count allowed (unsigned 8-bit). */
  TAG_HOPS        = 0x21,

  /** Beacon interval in seconds (unsigned 32-bit). */
  TAG_BEACON_SEC  = 0x22,

  /** Outbound queue size (unsigned 16-bit). */
  TAG_BUF_SIZE    = 0x23,

  /** ACK behavior flag: 0=disabled, 1=enabled. */
  TAG_ACK_MODE    = 0x24,

  // ---------------- Diagnostics (read-only) ----------------

  /** Last received RSSI in dBm (signed 16-bit). */
  TAG_RSSI_DBM    = 0x30,

  /** Last received SNR in dB (signed 8-bit). */
  TAG_SNR_DB      = 0x31,

  /** Supply or battery voltage in millivolts (unsigned 16-bit). */
  TAG_VBAT_MV     = 0x32,

  /** Temperature in 0.1 °C units (signed 16-bit). */
  TAG_TEMP_C10    = 0x33,

  /** Free heap memory in bytes (unsigned 32-bit). */
  TAG_FREE_MEM    = 0x34,

  /** Free flash storage in bytes (unsigned 32-bit). */
  TAG_FREE_FLASH  = 0x35,

  /** Log entry count (unsigned 16-bit). */
  TAG_LOG_COUNT   = 0x36
};


// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

/**
 * @brief Initialize the SLIP/Serial transport layer.
 *
 * Sets up the UART (USB CDC) at the requested baud rate, attaches
 * PacketSerial for SLIP framing, and installs the internal packet
 * handler (`on_slip_packet()`).
 *
 * Call this once from `setup()` before using any protocol functions.
 *
 * @param baud Baud rate for the Serial link (default = 115200).
 *
 * @note This only sets up the transport. Actual frame processing
 *       happens when you call node_protocol_update() in loop().
 */
void node_protocol_begin(unsigned long baud = 115200);

/**
 * @brief Advances the PacketSerial protocol handler.
 *
 * This function should be called from the main Arduino `loop()` to
 * service the PacketSerial state machine. It processes incoming bytes
 * from the serial buffer, assembles complete packets, and dispatches
 * them to the registered handler.
 *
 * @note Non-blocking. Safe to call frequently.
 */
void node_protocol_update();


/**
 * @brief Set or replace the inbound packet handler.
 *
 * This installs a callback that will be invoked whenever a complete frame
 * is received and decoded by the protocol layer. The handler is responsible
 * for interpreting the raw frame bytes and taking action.
 *
 * @param handler Function pointer to the callback with signature:
 *        `void handler(const uint8_t* frame, size_t len)`.
 *        - Pass a valid function pointer to override the default handler.
 *        - Pass `nullptr` to restore the built-in default (delegates to
 *          `node_interface`).
 *
 * @note Typically, the default handler is sufficient. Override only if
 *       custom processing of frames is required.
 */
void node_protocol_set_handler(void (*handler)(const uint8_t* frame, size_t len));

/**
 * @brief Send one complete *inner* frame over Serial.
 *
 * This function accepts a raw, unencoded frame from the caller,
 * applies SLIP (Serial Line Internet Protocol) encoding, and writes
 * the resulting byte stream to the Serial port.
 *
 * @param frame Pointer to the raw (inner) frame data to be transmitted.
 * @param len   Length of the raw frame in bytes.
 *
 * @note The caller should pass the un-encoded frame. SLIP framing
 *       (start/end delimiters, escaping) is applied automatically here.
 */
void protocol_send(const uint8_t* frame, size_t len);

/**
 * @brief Convenience wrapper to send a text message frame.
 *
 * This function constructs a ViaText `MSG` frame containing the provided
 * null-terminated string and sends it through the node protocol.
 *
 * @param s Pointer to a null-terminated C-string to be transmitted.
 *          The string should be short enough to fit within the 
 *          underlying frame buffer.
 *
 * @note This is a simplified helper intended for quick text-based
 *       messaging and testing. For full control, use the lower-level
 *       frame construction APIs.
 */
void node_protocol_send_text(const char* s);


