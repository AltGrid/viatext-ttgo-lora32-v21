#pragma once
/**
 * @page vt-node-interface ViaText Node Interface (State + Command Handlers)
 * @file node_interface.hpp
 * @brief High-level node brain: persistent state, verb dispatch, TLV I/O.
 *
 * Overview
 * --------
 * This module is the operational core of an embedded ViaText node. It owns the
 * device's persistent state (ID, radio parameters, behavior flags), interprets
 * inbound frames (verbs + TLVs), and emits responses. Think of it as the
 * "control desk" between transport (node_protocol) and presentation
 * (node_display). It keeps the logic readable and the borders clean.
 *
 * Where It Sits
 * -------------
 * - Below: node_protocol.* (SLIP over USB CDC). Delivers complete inner frames
 *   and handles writes. node_interface does not see SLIP bytes, only framed
 *   payloads.
 * - Above: node_display.* (optional OLED). Called when user-visible state
 *   should update (show ID, last message, etc.). The node remains headless
 *   if the display is absent.
 *
 * Responsibilities
 * ----------------
 * - Persist and expose node identity and configuration (ESP32 NVS).
 * - Parse verbs and their TLVs; validate inputs; mutate state.
 * - Build and send RESP_OK or RESP_ERR frames with appropriate TLVs.
 * - Issue an unsolicited "hello" on boot to announce presence.
 *
 * Non-Goals
 * ---------
 * - Byte transport, framing, SLIP, or serial buffer management (that is
 *   node_protocol).
 * - Long-running or blocking work. All handlers complete quickly.
 * - UI composition beyond minimal notifications to node_display.
 *
 * Frame and TLV Conventions
 * -------------------------
 * Inner frame layout (as delivered by node_protocol):
 *   [0]=verb, [1]=flags, [2]=seq, [3]=len, [4..]=TLVs
 *
 * TLVs follow simple Tag/Len/Value order and are concatenated:
 *   +--------+--------+-------------+
 *   | tag(1) | len(1) | value(len) |
 *   +--------+--------+-------------+
 *
 * Endianness for numeric values is little-endian. Strings are raw bytes,
 * not NUL-terminated. When returning strings, we copy up to a safe bound
 * and do not include a trailing NUL inside the TLV; callers should treat
 * them as byte arrays.
 *
 * Supported Verbs (aligned with @ref node_protocol.hpp)
 * -----------------------------------------------------
 * - GET_ID / PING
 *   Echoes current ID in RESP_OK. Useful for presence and basic health checks.
 *
 * - SET_ID
 *   Accepts TAG_ID as a string TLV. Validates length and allowed characters
 *   [A-Za-z0-9-_]. On success: writes to NVS, nudges display, RESP_OK with ID,
 *   then emits an unsolicited hello so nearby hosts learn the change.
 *
 * - GET_PARAM
 *   Request specific tags by sending TLVs with len=0 (tag only). The response
 *   includes those tags populated with current values.
 *
 * - SET_PARAM
 *   Update configuration by sending TLVs with values. Validates ranges
 *   (e.g., SF 7..12, CR 5..8, ACK_MODE 0/1). On success: persist to NVS and
 *   RESP_OK echoing all settable tags (so callers see final, clamped values).
 *
 * - GET_ALL
 *   Bulk read of identity, radio, behavior, and diagnostic tags. Intended for
 *   diagnostic panels and initial sync. May grow over time as diagnostics are
 *   implemented.
 *
 * - MSG
 *   Accepts a short text payload (len bytes directly in the frame after the
 *   header for this verb). Stores a copy for UI/debug, optionally draws to the
 *   display, then RESP_OK with ID. Designed for quick status fan-out.
 *
 * Boot-Time Behavior
 * ------------------
 * - node_interface_begin() loads state from NVS (or applies defaults).
 * - node_interface_send_hello() sends RESP_OK with TAG_ID and seq=0 to announce
 *   presence without waiting for a poll. Hosts can treat seq=0 as "unsolicited".
 *
 * Persistence Rules
 * -----------------
 * - All settable fields live in ESP32 NVS under a dedicated namespace.
 * - Writes occur only after successful validation. Failed validation never
 *   touches NVS and returns RESP_ERR.
 * - String fields are bounded. We copy and clamp before writing.
 *
 * Safety and Failure Modes
 * ------------------------
 * - All handlers are defensive about lengths and bounds. Unknown tags are
 *   ignored; malformed TLVs cause RESP_ERR for that operation without
 *   crashing the node.
 * - Display calls are guarded by node_display_available(). If the panel is
 *   missing, all UI calls silently no-op.
 * - Numeric conversions are explicit little-endian to keep cross-platform
 *   behavior predictable.
 *
 * Minimal Exchange Examples (pseudo)
 * ----------------------------------
 * 1) Read ID
 *   Host -> [GET_ID, 0x00, 0x01, 0x00]
 *   Node <- RESP_OK with TAG_ID
 *
 * 2) Set ID
 *   Host -> [SET_ID, 0x00, 0x02, 0x03, TAG_ID, 0x03, 'N','3','0']
 *   Node <- RESP_OK with TAG_ID + unsolicited hello (seq=0)
 *
 * 3) Read a parameter
 *   Host -> [GET_PARAM, 0x00, 0x03, 0x02, TAG_SF, 0x00]
 *   Node <- RESP_OK with TAG_SF, 0x01, <value>
 *
 * Extending the Interface
 * -----------------------
 * 1) Define new Tag and/or Verb in @ref node_protocol.hpp.
 * 2) Add handling branches in node_interface_on_packet():
 *    - Validate TLVs
 *    - Update state (if applicable)
 *    - Build RESP_OK with results, or RESP_ERR on failure
 * 3) Persist changes in save_to_nvs() if configuration is modified.
 * 4) If user-visible, call a minimal node_display_* helper.
 * Keep handlers short. If work grows complex, push it into a leaf module that
 * exposes a small API, and keep node_interface as the conductor.
 *
 * Security Notes
 * --------------
 * - This build targets local, link-level control over USB serial. There is no
 *   authentication layer in the verb set. If you expose the serial port beyond
 *   a trusted link, wrap it in a secured transport or gate commands in a
 *   higher layer.
 *
 * Testing Hooks
 * -------------
 * - node_interface_last_text() exposes the last MSG payload for UI/tests.
 * - node_interface_id() returns a stable, NUL-terminated identity buffer.
 *
 * @author Leo
 * @author ChatGPT
 */

#include <cstddef>
#include <cstdint>

/**
 * @brief Initialize the node interface and load state from NVS.
 *
 * @details
 * This function must be called once at boot. It restores any previously
 * saved configuration (node ID, radio parameters, behavior flags) from
 * ESP32 NVS. If nothing is stored, safe defaults are applied.
 * After initialization, the node has a valid identity and parameters
 * for normal operation.
 *
 * @note In field use, this ensures the node remembers its identity
 * across power cycles and cold restarts.
 */
void node_interface_begin();

/**
 * @brief Get the current node ID.
 *
 * @return const char* Pointer to a static, NUL-terminated string containing
 *         the node's current ID.
 *
 * @details
 * The returned buffer is static and must not be modified or freed by the
 * caller. Always safe to call; a valid string is guaranteed even if an
 * ID has never been set.
 */
const char* node_interface_id();

/**
 * @brief Handle one complete ViaText inner frame.
 *
 * @param frame Pointer to the raw frame data (verb, flags, seq, len, TLVs).
 * @param len   Number of bytes in the frame.
 *
 * @details
 * This function acts as the main dispatcher for inbound traffic.
 * The lower protocol layer (node_protocol) passes fully framed data here.
 * The function inspects the verb, validates TLVs, updates node state as
 * needed, and emits a response frame.
 *
 * @note Must complete quickly. Handlers are designed for short,
 * deterministic execution suitable for embedded event loops.
 */
void node_interface_on_packet(const uint8_t* frame, size_t len);

/**
 * @brief Send an unsolicited "hello" frame announcing the node's ID.
 *
 * @details
 * Builds and transmits a RESP_OK frame with TAG_ID and sequence set to 0.
 * Sequence=0 marks the frame as unsolicited. Typically used at boot so
 * other peers or hosts can learn of this node without polling first.
 */
void node_interface_send_hello();

/**
 * @brief Get the last MSG text payload received.
 *
 * @return const char* Pointer to a static, NUL-terminated string containing
 *         the last received MSG payload. Returns an empty string if none.
 *
 * @details
 * Useful for logging, debugging, or displaying the most recent broadcast
 * to a local UI. The buffer is static and remains valid until the next
 * MSG payload arrives.
 */
const char* node_interface_last_text();
