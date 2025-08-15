#pragma once
/**
 * @file node_interface.hpp
 * @brief ViaText Node: high-level command handling and node state.
 *
 * Responsibilities
 * ---------------
 * - Own the node state (e.g., device ID, alias, radio/behavior settings) and persist it (ESP32 NVS).
 * - Decode incoming frames (verb, seq, TLVs) and perform the requested action.
 * - Build and send response frames (via node_protocol::protocol_send()).
 * - Notify the display layer when user-visible state changes (ID, alias, messages).
 *
 * Supported verbs
 * ---------------
 * - GET_ID / SET_ID (legacy ID handling)
 * - PING (simple alive check)
 * - MSG (short text message display/log)
 * - GET_PARAM / SET_PARAM (read/write specific tags)
 * - GET_ALL (read all tags, may return multiple frames)
 *
 * Supported tags
 * --------------
 * See node_protocol.hpp Tag enum for complete list:
 *   - Identity/system: ID, ALIAS, FW_VERSION, UPTIME_S, BOOT_TIME
 *   - Radio: FREQ_HZ, SF, BW_HZ, CR, TX_PWR_DBM, CHAN
 *   - Behavior: MODE, HOPS, BEACON_SEC, BUF_SIZE, ACK_MODE
 *   - Diagnostics (read-only): RSSI_DBM, SNR_DB, VBAT_MV, TEMP_C10, FREE_MEM, FREE_FLASH, LOG_COUNT
 *
 * What this is NOT
 * ----------------
 * - It does not own SLIP/serial I/O (that’s node_protocol).
 * - It does not do OLED setup/drawing (that’s node_display).
 *
 * Extending commands
 * ------------------
 * 1) Add new tags/verbs to node_protocol.hpp (shared, single source of truth).
 * 2) In node_interface.cpp, add parsing/handling to the switch in node_interface_on_packet().
 * 3) Build RESP_OK frames with the new TLVs, or RESP_ERR on failure.
 * 4) (Optional) update node_display if the state is user-visible.
 */

#include <cstddef>
#include <cstdint>

/// Initialize interface (loads persisted settings from NVS; safe to call once at boot).
void        node_interface_begin();

/// Return the current node ID (NUL-terminated). Lifetime: static.
const char* node_interface_id();

/// Protocol callback: handle one complete inner frame (not SLIP-encoded).
/// Called by node_protocol when a full frame is received.
void        node_interface_on_packet(const uint8_t* frame, size_t len);

/// Send an unsolicited hello (RESP_OK, seq=0) including current ID.
void        node_interface_send_hello();

/// Returns last received MSG text (NUL if none). Lifetime: static.
const char* node_interface_last_text();
