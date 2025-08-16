// node_interface.cpp — implementation for node_interface.hpp
// See node_interface.hpp for API/overview and tests/examples for usage patterns.

#include "node_interface.hpp"   // Node-facing API: I/O, IDs, hello, send/receive
#include "node_protocol.hpp"    // Core protocol loop: begin, update, handlers
#include "node_display.hpp"     // OLED/LCD drawing: boot screen, ID display

#include <Arduino.h>            // Arduino framework base (pins, Serial, etc.)
#include <Preferences.h>        // ESP32 NVS key/value storage (persist ID/alias)
#include <cstring>              // Standard C string utilities (memcpy, strcmp, etc.)

// ============================================================================
// Persistent state + storage
// ============================================================================

static Preferences s_prefs;          // Handle to ESP32 NVS key/value storage
static bool        s_prefs_open = false; // Tracks whether NVS is successfully opened

static char        s_id[32]    = "HckrMn";   // Node ID (31 chars + NUL terminator)
static char        s_alias[32] = "";         // Human-friendly alias/name

static uint32_t    s_freq_hz   = 915000000;  // Default radio frequency (Hz)
static uint8_t     s_sf        = 9;          // LoRa spreading factor
static uint32_t    s_bw_hz     = 125000;     // LoRa bandwidth (Hz)
static uint8_t     s_cr        = 5;          // LoRa coding rate (4/5 default)
static int8_t      s_tx_pwr    = 17;         // Radio TX power (dBm)

static uint8_t     s_chan      = 0;          // Logical channel index
static uint8_t     s_mode      = 0;          // Node mode (0=relay, etc.)
static uint8_t     s_hops      = 1;          // Maximum relay hops allowed
static uint32_t    s_beacon_s  = 0;          // Beacon interval (seconds, 0=disabled)
static uint16_t    s_buf_size  = 32;         // Outbound buffer/queue size
static uint8_t     s_ack_mode  = 0;          // ACK setting (0=off, 1=on)


// last received text for UI/debug
static char s_last_text[64] = "";  // Holds the most recent incoming MSG text

// ============================================================================
// Load / Save helpers
// ============================================================================

//
// load_from_nvs()
// ----------------
// Pulls persisted configuration out of ESP32 NVS into our globals.
// If NVS is unavailable, keep defaults in memory and exit quietly.
//
// Process:
//   1. Attempt to open the "viatext" namespace in read/write mode.
//   2. If open fails, leave defaults untouched.
//   3. Otherwise, read each key into its associated global.
//      Each getter takes a fallback default (the in-memory value).
//
static void load_from_nvs() {
  // Phase 1: open NVS (read/write so we can later update too)
  s_prefs_open = s_prefs.begin("viatext", /*readOnly=*/false);
  if (!s_prefs_open) return;  // If open fails, don't touch defaults

  // Phase 2: identity strings (guarded by buffer sizes)
  s_prefs.getString("id", s_id, sizeof(s_id));
  s_prefs.getString("alias", s_alias, sizeof(s_alias));

  // Phase 3: radio/system parameters (numeric values)
  s_freq_hz  = s_prefs.getULong("freq_hz", s_freq_hz);
  s_sf       = s_prefs.getUChar("sf", s_sf);
  s_bw_hz    = s_prefs.getULong("bw_hz", s_bw_hz);
  s_cr       = s_prefs.getUChar("cr", s_cr);
  s_tx_pwr   = s_prefs.getChar("tx_pwr", s_tx_pwr);
  s_chan     = s_prefs.getUChar("chan", s_chan);
  s_mode     = s_prefs.getUChar("mode", s_mode);
  s_hops     = s_prefs.getUChar("hops", s_hops);
  s_beacon_s = s_prefs.getULong("beacon_s", s_beacon_s);
  s_buf_size = s_prefs.getUShort("buf_size", s_buf_size);
  s_ack_mode = s_prefs.getUChar("ack_mode", s_ack_mode);
}


//
// save_to_nvs()
// --------------
// Push current in-memory config back into ESP32 NVS.
// Ensures persistence across resets/power cycles.
//
// Process:
//   1. If NVS isn't open yet, try to open it now (read/write).
//      If that fails, exit without saving.
//   2. Write each field to its matching key.
//      Each call replaces or creates the key with the new value.
//
// Notes:
//   - All globals are assumed valid by this point (validated earlier).
//   - NVS commit is implicit in Preferences API (auto after each put).
//
static void save_to_nvs() {
  // Step 1: open storage if not already open
  if (!s_prefs_open) {
    s_prefs_open = s_prefs.begin("viatext", false);
    if (!s_prefs_open) return;  // bail out if open fails
  }

  // Step 2: write identity strings
  s_prefs.putString("id", s_id);
  s_prefs.putString("alias", s_alias);

  // Step 3: write radio/system parameters
  s_prefs.putULong("freq_hz", s_freq_hz);
  s_prefs.putUChar("sf", s_sf);
  s_prefs.putULong("bw_hz", s_bw_hz);
  s_prefs.putUChar("cr", s_cr);
  s_prefs.putChar("tx_pwr", s_tx_pwr);
  s_prefs.putUChar("chan", s_chan);
  s_prefs.putUChar("mode", s_mode);
  s_prefs.putUChar("hops", s_hops);
  s_prefs.putULong("beacon_s", s_beacon_s);
  s_prefs.putUShort("buf_size", s_buf_size);
  s_prefs.putUChar("ack_mode", s_ack_mode);
}


// ============================================================================
// Validation helpers
// ============================================================================

/*
 * is_valid_id()
 * -------------
 * Validate a node ID string for storage/display.
 * Rules: 1..31 chars; only [A-Za-z0-9-_].
 * No allocation; scans once and early-outs on first bad char.
 */
static bool is_valid_id(const char* p) {
  size_t n = strlen(p);                 // measure once
  if (n == 0 || n > 31) return false;   // enforce length bounds (1..31)

  // Character whitelist check:
  // --------------------------
  // This loop enforces that every character in the node ID belongs
  // to a limited "safe set":
  //   - a..z  (lowercase letters)
  //   - A..Z  (uppercase letters)
  //   - 0..9  (digits)
  //   - '-' or '_' (dash or underscore)
  // 
  // The expression builds a boolean "ok" by chaining comparisons.
  // If *any* character falls outside this whitelist, we immediately
  // return false.
  // 
  // This style avoids using <ctype.h> helpers (like isalnum())
  // because those are locale-dependent and sometimes pull in extra
  // runtime baggage — not great on embedded targets.
  // Scan characters and reject on first illegal byte.
  for (size_t i = 0; i < n; ++i) {
    char c = p[i];
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              (c == '-' || c == '_');  // allow dash and underscore only
    if (!ok) return false;
  }
  return true;                          // all checks passed
}

/*
 * is_valid_sf()
 * -------------
 * Legal LoRa spreading factors are 7..12 inclusive.
 */
static bool is_valid_sf(uint8_t v)      { return v >= 7 && v <= 12; }

/*
 * is_valid_cr()
 * -------------
 * Coding rate code 5..8 maps to 4/5..4/8.
 */
static bool is_valid_cr(uint8_t v)      { return v >= 5 && v <= 8; }

/*
 * is_valid_ack()
 * --------------
 * ACK mode is a boolean flag encoded as 0 or 1.
 */
static bool is_valid_ack(uint8_t v)     { return v == 0 || v == 1; }


// ============================================================================
// TLV helpers
// ============================================================================

//
// frame_begin()
// -------------
// Start a new outbound frame in the caller’s buffer.
//
// Process:
//   1. Reset the write index `i` to 0 (fresh buffer).
//   2. Write standard 4-byte header:
//        [0] verb   — command type (GET_ID, SET_PARAM, etc.)
//        [1] flags  — currently unused, set to 0
//        [2] seq    — sequence number for matching responses
//        [3] TLV_LEN— placeholder (0 for now, patched by frame_end())
//   3. Caller will append TLVs starting at buf[4].
//
// Notes:
//   - This does not allocate; caller must supply a buffer large enough.
//   - `i` is passed by reference so the caller’s index advances in place.

static inline void frame_begin(uint8_t verb, uint8_t seq, uint8_t* buf, size_t& i) {
  i = 0;                 // reset write pointer to beginning of buffer
  buf[i++] = verb;       // field 0: verb
  buf[i++] = 0;          // field 1: flags (unused, always 0)
  buf[i++] = seq;        // field 2: sequence number
  buf[i++] = 0;          // field 3: TLV length placeholder (filled in later)
}

//
// tlv_put()
// ---------
// Append a TLV triplet at the current write index.
// Caller supplies:
//   - tag  : what the value means
//   - p    : pointer to value bytes (may be nullptr if len == 0)
//   - len  : number of bytes to copy
//
// Process:
//   1) Write tag.
//   2) Write len.
//   3) If len > 0, memcpy value bytes and advance index.
//
// Assumptions / notes:
//   - Caller guarantees `buf` has enough headroom (no bounds checks here).
//   - `p` must point to at least `len` bytes when len > 0.
//   - Use tlv_put_le<T>() for numeric types to ensure little-endian encoding.
//   - memcpy() is correct here; the source never overlaps with destination.

static inline void tlv_put(uint8_t* buf, size_t& i, uint8_t tag, const void* p, uint8_t len) {
  buf[i++] = tag;                  // T
  buf[i++] = len;                  // L
  if (len) { memcpy(buf + i, p, len); i += len; }  // V (optional payload)
}

// tlv_put_le<T>()
// ---------------
// Serialize an integral value into little-endian byte order and append as a TLV.
// Useful for numeric tags where the protocol standardizes on LE.
//
// Process:
//   1) Create a fixed-size temp buffer for T's bytes.
//   2) For each byte position j, extract the 8-bit slice (LSB first).
//   3) Delegate to tlv_put() with the tag and serialized bytes.
//
// Notes:
//   - Works for any integral T (uint8_t, uint16_t, uint32_t, int8_t, etc.).
//   - Little-endian means least-significant byte goes out first.
//   - No dynamic allocation; temp buffer lives on the stack.

template<typename T>
static inline void tlv_put_le(uint8_t* buf, size_t& i, uint8_t tag, T value) {
  uint8_t tmp[sizeof(T)];                               // step 1: hold serialized bytes
  for (size_t j = 0; j < sizeof(T); ++j)                // step 2: byte-wise extract
    tmp[j] = (uint8_t)((value >> (8 * j)) & 0xFF);      //   take LSB first, then next...
  tlv_put(buf, i, tag, tmp, sizeof(T));                 // step 3: append TLV triplet
}


// frame_end()
// -----------
// Close out a TLV frame by patching the payload length.
// Up to now, we've been writing bytes into buf[] and bumping `i`.
// The first 4 bytes of the header reserved byte [3] as a placeholder.
// Here we overwrite buf[3] with the true payload length = (i - 4).
//
// Process:
//   1. Take the current cursor `i` (index after last written byte).
//   2. Subtract 4 (the header size).
//   3. Store result as an unsigned 8-bit in header slot [3].
//
// Notes:
//   - Max payload length is capped at 255 because field is uint8_t.
//   - Caller must call this once after writing all TLVs.

static inline void frame_end(uint8_t* buf, size_t& i) {
  buf[3] = static_cast<uint8_t>(i - 4);  // length = total - header
}

// tlv_find() — scan TLV area for first matching tag; return pointer to value or nullptr.
// Purpose: locate `tag` inside frame’s TLV block; set `out_len` to its value length.
// Assumptions: 4-byte header present; frame[3] holds TLV payload length (set by frame_end()).
// Invariants: never read past `len`; abort on malformed TLV.
// Tradeoffs: returns only the first match; duplicates ignored.
// Flow: header check -> TLV bounds check -> iterate TLVs -> return on match.

static const uint8_t* tlv_find(const uint8_t* frame, size_t len, uint8_t tag, uint8_t& out_len) {
  if (len < 4) return nullptr;                 // not enough bytes for header
  size_t tlv_len = frame[3];                   // declared TLV section length (bytes after header)
  if (4 + tlv_len > len) return nullptr;       // TLV block would overrun provided frame length

  size_t off = 4, end = 4 + tlv_len;           // scan window: start right after header
  while (off + 2 <= end) {                     // need at least tag(1)+len(1) available
    uint8_t t = frame[off++];                  // read Tag
    uint8_t L = frame[off++];                  // read Length
    if (off + L > end) break;                  // malformed: value claims to run past TLV block
    if (t == tag) {                            // match — hand back value pointer and length
      out_len = L;
      return frame + off;                      // value starts at current offset
    }
    off += L;                                  // skip over Value to next TLV
  }
  return nullptr;                              // not found (or bailed on malformed structure)
}


// tlv_read_le<T>() — decode a little-endian integer from raw bytes into `out`.
// Purpose: read `len` bytes at `p` as a little-endian value of type T.
// Assumptions: caller passed the exact byte count for T; `p` points to len bytes.
// Invariants: do not read past `len`; do not allocate; deterministic across platforms.
// Tradeoffs: rejects size mismatches (hard fail) instead of partial reads.
// Flow: size check -> zero-init accumulator -> byte-wise OR with left-shifts.

template<typename T>
static bool tlv_read_le(const uint8_t* p, uint8_t len, T& out) {
  if (len != sizeof(T)) return false;              // must match exact width
  out = 0;                                         // phase 1: clear accumulator
  for (size_t j = 0; j < sizeof(T); ++j) {         // phase 2: assemble LE -> native
    // Cast each input byte to T before shifting to avoid UB/sign issues.
    // Shift by 8*j (LSB first), then OR into the accumulator.
    out |= (static_cast<T>(p[j]) << (8 * j));
  }
  return true;                                     // success: `out` now holds the value
}


// ============================================================================
// RESP helpers
// ============================================================================

// send_resp_err() — minimal RESP_ERR builder/sender.
// Purpose: emit an empty error response tied to `seq`.
// Assumptions: caller decided this operation failed; no TLVs are attached.
// Invariants: header is 4 bytes; TLV length is zero; buffer is stack-local.
// Flow: allocate small buffer -> write header -> finalize length -> send.

static void send_resp_err(uint8_t seq) {
  uint8_t b[8]; size_t i;                  // tiny scratch buffer + cursor
  frame_begin(Verb::RESP_ERR, seq, b, i);  // header: verb=ERR, seq=caller
  frame_end(b, i);                          // TLV length = i-4 (here: 0)
  protocol_send(b, i);                      // hand off to transport
}


// send_tag_value() — append one TLV for `tag` using current in-memory state.
// Purpose: serialize a single tag into buf at cursor `i` (TLV: tag,len,value).
// Assumptions: caller began a frame (header already written) and `buf` has room.
// Invariants: numeric values encoded little-endian via tlv_put_le<>; strings are raw bytes.
// Tradeoffs: silently ignore unknown tags (forward-compatible on the wire).
// Flow: switch(tag) -> compute/copy source value -> tlv_put/tlv_put_le -> advance `i`.

static void send_tag_value(uint8_t* buf, size_t& i, uint8_t tag) {
  // switch: one branch per protocol tag to keep it grep-friendly and explicit.
  switch (tag) {

    // Identity/System: Node ID string (no NUL inside the TLV).
    case TAG_ID: {
      uint8_t L = (uint8_t)strnlen(s_id, sizeof(s_id));
      tlv_put(buf, i, TAG_ID, s_id, L);
      break;
    }

    // Identity/System: human-friendly alias (len may be 0).
    case TAG_ALIAS: {
      uint8_t L = (uint8_t)strnlen(s_alias, sizeof(s_alias));
      tlv_put(buf, i, TAG_ALIAS, s_alias, L);
      break;
    }

    // Identity/System: firmware version string (tie to build macro later).
    case TAG_FW_VERSION: {
      const char* ver = "1.0.0"; // TODO: tie to build version macro if available
      tlv_put(buf, i, tag, ver, (uint8_t)strlen(ver));
      break;
    }

    // Identity/System: uptime in seconds (derived from millis()).
    case TAG_UPTIME_S: {
      uint32_t uptime = millis() / 1000;
      tlv_put_le<uint32_t>(buf, i, tag, uptime);
      break;
    }

    // Identity/System: boot epoch if available (stubbed to 0 for now).
    case TAG_BOOT_TIME: {
      uint32_t boot_time = 0; // TODO: real epoch from RTC if available
      tlv_put_le<uint32_t>(buf, i, tag, boot_time);
      break;
    }

    // Radio: RF center frequency in Hz (LE u32).
    case TAG_FREQ_HZ: {
      tlv_put_le<uint32_t>(buf, i, tag, s_freq_hz);
      break;
    }

    // Radio: LoRa spreading factor (7..12).
    case TAG_SF: {
      tlv_put_le<uint8_t>(buf, i, tag, s_sf);
      break;
    }

    // Radio: RF bandwidth in Hz (LE u32).
    case TAG_BW_HZ: {
      tlv_put_le<uint32_t>(buf, i, tag, s_bw_hz);
      break;
    }

    // Radio: LoRa coding rate code (5..8 -> 4/5..4/8).
    case TAG_CR: {
      tlv_put_le<uint8_t>(buf, i, tag, s_cr);
      break;
    }

    // Radio: TX power in dBm (signed 8-bit).
    case TAG_TX_PWR_DBM: {
      tlv_put_le<int8_t>(buf, i, tag, s_tx_pwr);
      break;
    }

    // Radio: logical channel index.
    case TAG_CHAN: {
      tlv_put_le<uint8_t>(buf, i, tag, s_chan);
      break;
    }

    // Behavior/Routing: node mode (implementation-defined).
    case TAG_MODE: {
      tlv_put_le<uint8_t>(buf, i, tag, s_mode);
      break;
    }

    // Behavior/Routing: maximum hop count permitted.
    case TAG_HOPS: {
      tlv_put_le<uint8_t>(buf, i, tag, s_hops);
      break;
    }

    // Behavior/Routing: beacon interval in seconds (0 disables).
    case TAG_BEACON_SEC: {
      tlv_put_le<uint32_t>(buf, i, tag, s_beacon_s);
      break;
    }

    // Behavior/Routing: TX buffer/queue size.
    case TAG_BUF_SIZE: {
      tlv_put_le<uint16_t>(buf, i, tag, s_buf_size);
      break;
    }

    // Behavior/Routing: ACK mode (0 or 1).
    case TAG_ACK_MODE: {
      tlv_put_le<uint8_t>(buf, i, tag, s_ack_mode);
      break;
    }

    // Diagnostics: placeholder RSSI dBm (wire up real metric later).
    case TAG_RSSI_DBM: {
      tlv_put_le<int16_t>(buf, i, tag, -42);
      break;
    }

    // Diagnostics: placeholder SNR dB (wire up real metric later).
    case TAG_SNR_DB: {
      tlv_put_le<int8_t>(buf, i, tag, 7);
      break;
    }

    // Diagnostics: placeholder battery millivolts (ADC later).
    case TAG_VBAT_MV: {
      tlv_put_le<uint16_t>(buf, i, tag, 3700);
      break;
    }

    // Diagnostics: placeholder temperature in deci-deg C.
    case TAG_TEMP_C10: {
      tlv_put_le<int16_t>(buf, i, tag, 215);
      break;
    }

    // Diagnostics: placeholder free heap bytes.
    case TAG_FREE_MEM: {
      tlv_put_le<uint32_t>(buf, i, tag, 123456);
      break;
    }

    // Diagnostics: placeholder free flash/storage bytes.
    case TAG_FREE_FLASH: {
      tlv_put_le<uint32_t>(buf, i, tag, 654321);
      break;
    }

    // Diagnostics: placeholder log entry count.
    case TAG_LOG_COUNT: {
      tlv_put_le<uint16_t>(buf, i, tag, 0);
      break;
    }

    // Unknown tag: emit nothing; safe no-op for forward compatibility.
    default: {
      // Unknown tag — ignore silently
      break;
    }
  }
}



// node_interface_begin() — bring in persisted settings at boot.
// Purpose: hydrate in-memory state from NVS (or keep defaults if NVS fails).
// Assumptions: NVS namespace/key names match load_from_nvs() expectations.
// Invariants: safe to call once at boot; leaves globals consistent on failure.
// Flow: call loader -> return (no I/O side effects here).

void node_interface_begin() {
  load_from_nvs();
}

// node_interface_id() — expose current node ID buffer.
// Purpose: return stable, NUL-terminated ID string for callers.
// Assumptions: s_id lifetime is static; no allocation/copy performed.
// Invariants: never returns nullptr; contents may change only via setters.

const char* node_interface_id() { return s_id; }

// node_interface_send_hello() — unsolicited presence announce.
// Purpose: emit RESP_OK(seq=0) with TAG_ID so hosts can discover the node.
// Assumptions: protocol_send() handles transport/framing beyond inner frame.
// Invariants: minimal frame (header + TAG_ID TLV); no other TLVs attached.
// Flow: begin frame -> add TAG_ID -> finalize length -> send.

void node_interface_send_hello() { 
  uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,0,b,i);  // fresh header, seq=0 marks unsolicited
  send_tag_value(b,i,TAG_ID);                                 // include current ID as TLV payload
  frame_end(b,i);                                             // patch TLV length (i - 4)
  protocol_send(b,i);                                         // hand off to transport layer
}

// Return the most recent text payload seen by this node.
// - Returns a raw const char* (C-string) pointing to static/global storage.
// - Caller must **not free or modify** this pointer.
// - Contents remain valid until the next inbound message updates `s_last_text`.
const char* node_interface_last_text() { 
    return s_last_text; 
}


// node_interface_on_packet() — central verb/TLV dispatcher for one complete frame.
// Purpose: decode the inner frame (verb + TLV area), mutate local state, and emit a response.
// Assumptions: transport already delivered a full inner frame (not SLIP bytes). Header is 4 bytes.
// Invariants: never read past `len`; unknown/invalid inputs yield RESP_ERR but never crash.
// Tradeoffs: fast, linear parsing with minimal copying; unknown tags ignored for forward-compat.
// Flow: guard -> extract verb/seq -> switch(verb) -> per-verb handling -> RESP_OK/RESP_ERR.

void node_interface_on_packet(const uint8_t* frame, size_t len) {
  // Guard: require a non-null buffer and at least header bytes.
  if (!frame || len < 4) return;

  // Extract verb and sequence early; used by all responder branches.
  const uint8_t verb = frame[0];
  const uint8_t seq  = frame[2];

  // Dispatch by verb. Keep branches short and explicit for debuggability.
  switch (verb) {

    // Simple query path: echo ID for presence/health checks.
    case Verb::GET_ID:
    case Verb::PING:
      { uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);  // start RESP_OK
        send_tag_value(b,i,TAG_ID);                                   // add ID TLV
        frame_end(b,i); protocol_send(b,i);                           // finalize + send
      }
      break;

    // Mutate identity: validate incoming TAG_ID, persist, update display, ack, then announce.
    case Verb::SET_ID: {
      uint8_t L=0; const uint8_t* p = tlv_find(frame,len,TAG_ID,L);   // locate TAG_ID TLV
      if (!p||L==0) { send_resp_err(seq); break; }                    // must have a non-empty value
      char tmp[sizeof(s_id)]; size_t copy = (L>=sizeof(tmp))?(sizeof(tmp)-1):L;  // clamp length
      memcpy(tmp,p,copy); tmp[copy]='\0';                              // make a safe C-string
      if (!is_valid_id(tmp)) { send_resp_err(seq); break; }            // enforce charset/length policy
      strncpy(s_id,tmp,sizeof(s_id)); save_to_nvs();                   // commit new ID to RAM+NVS
      node_display_draw_id(s_id);                                      // nudge UI if present
      { uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);   // ack with the new ID
        send_tag_value(b,i,TAG_ID);
        frame_end(b,i); protocol_send(b,i);
      }
      node_interface_send_hello();                                     // unsolicited announce (seq=0)
      break;
    }

    // Parameter read: for each TLV with len==0, populate that tag in the response.
    case Verb::GET_PARAM: {
      uint8_t b[128]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);    // start RESP_OK
      size_t off=4,end=4+frame[3];                                     // TLV scan window
      while (off+2<=end) {                                             // need tag+len available
        uint8_t t=frame[off++]; uint8_t L=frame[off++];                // read tag, len
        off+=L; // skip value                                            // skip any provided value
        if (L==0) send_tag_value(b,i,t);                                // tag-only means "please return"
      }
      frame_end(b,i); protocol_send(b,i);                              // finalize + send
      break;
    }

    // Parameter write: validate and apply each provided TLV; NVS save only if all OK.
    case Verb::SET_PARAM: {
      bool ok=true;                                                     // optimistic parse
      size_t off=4,end=4+frame[3];                                     // TLV scan window
      while (off+2<=end) {
        uint8_t t=frame[off++]; uint8_t L=frame[off++];                // read tag, len
        const uint8_t* p = frame+off; off+=L;                           // grab value then advance
        
        // Dispatch on the incoming command kind: select the appropriate handler
        // (e.g., ID, ALIAS, MESSAGE) and execute its logic. Each case translates
        // the abstract command enum into a concrete action for this node.
        switch (t) {

          // Alias: copy string into s_alias with length clamp and NUL termination.
          case TAG_ALIAS: {
            strncpy(s_alias, (const char*)p, std::min((size_t)L, sizeof(s_alias) - 1));
            break;
          }

          // Frequency: expect a 4-byte LE value -> s_freq_hz.
          case TAG_FREQ_HZ: {
            ok &= tlv_read_le<uint32_t>(p, L, s_freq_hz);
            break;
          }

          // Spreading Factor: must be valid SF (7..12); otherwise fail.
          case TAG_SF: {
            uint8_t v;
            if (tlv_read_le<uint8_t>(p, L, v) && is_valid_sf(v)) {
              s_sf = v;
            } else {
              ok = false;
            }
            break;
          }

          // Bandwidth: expect a 4-byte LE value -> s_bw_hz.
          case TAG_BW_HZ: {
            ok &= tlv_read_le<uint32_t>(p, L, s_bw_hz);
            break;
          }

          // Coding Rate: must pass validation; otherwise fail.
          case TAG_CR: {
            uint8_t v;
            if (tlv_read_le<uint8_t>(p, L, v) && is_valid_cr(v)) {
              s_cr = v;
            } else {
              ok = false;
            }
            break;
          }

          // TX Power: signed 8-bit dBm value.
          case TAG_TX_PWR_DBM: {
            ok &= tlv_read_le<int8_t>(p, L, s_tx_pwr);
            break;
          }

          // Channel index: 8-bit integer.
          case TAG_CHAN: {
            ok &= tlv_read_le<uint8_t>(p, L, s_chan);
            break;
          }

          // Mode: 8-bit integer representing current node mode.
          case TAG_MODE: {
            ok &= tlv_read_le<uint8_t>(p, L, s_mode);
            break;
          }

          // Hop count: 8-bit integer for routing depth.
          case TAG_HOPS: {
            ok &= tlv_read_le<uint8_t>(p, L, s_hops);
            break;
          }

          // Beacon interval: 4-byte LE seconds.
          case TAG_BEACON_SEC: {
            ok &= tlv_read_le<uint32_t>(p, L, s_beacon_s);
            break;
          }

          // Buffer size: 2-byte LE value.
          case TAG_BUF_SIZE: {
            ok &= tlv_read_le<uint16_t>(p, L, s_buf_size);
            break;
          }

          // ACK Mode: must be validated (0/1); otherwise fail.
          case TAG_ACK_MODE: {
            uint8_t v;
            if (tlv_read_le<uint8_t>(p, L, v) && is_valid_ack(v)) {
              s_ack_mode = v;
            } else {
              ok = false;
            }
            break;
          }
        }

      }
      if (!ok) { send_resp_err(seq); break; }                           // all-or-nothing semantics
      save_to_nvs();                                                    // persist applied settings
      uint8_t b[128]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);     // echo back current values
      // echo back all settable tags
      send_tag_value(b,i,TAG_ALIAS);
      send_tag_value(b,i,TAG_FREQ_HZ);
      send_tag_value(b,i,TAG_SF);
      send_tag_value(b,i,TAG_BW_HZ);
      send_tag_value(b,i,TAG_CR);
      send_tag_value(b,i,TAG_TX_PWR_DBM);
      send_tag_value(b,i,TAG_CHAN);
      send_tag_value(b,i,TAG_MODE);
      send_tag_value(b,i,TAG_HOPS);
      send_tag_value(b,i,TAG_BEACON_SEC);
      send_tag_value(b,i,TAG_BUF_SIZE);
      send_tag_value(b,i,TAG_ACK_MODE);
      frame_end(b,i); protocol_send(b,i);                               // finalize + send
      break;
    }

    // Bulk read: return identity, radio, behavior, and diagnostic tags in one shot.
    case Verb::GET_ALL: {
      uint8_t b[192]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
      for (uint8_t t : {
        TAG_ID,TAG_ALIAS,TAG_FREQ_HZ,TAG_SF,TAG_BW_HZ,TAG_CR,TAG_TX_PWR_DBM,
        TAG_CHAN,TAG_MODE,TAG_HOPS,TAG_BEACON_SEC,TAG_BUF_SIZE,TAG_ACK_MODE,
        TAG_RSSI_DBM,TAG_SNR_DB,TAG_VBAT_MV,TAG_TEMP_C10,TAG_FREE_MEM,TAG_FREE_FLASH,TAG_LOG_COUNT
      }) send_tag_value(b,i,t);                                          // emit each tag’s TLV
      frame_end(b,i); protocol_send(b,i);
      break;
    }

    // Text message: copy payload for UI/debug, optionally draw, ack with ID.
    case Verb::MSG: {
      if (len<4) { send_resp_err(seq); break; }                          // header must exist
      uint8_t L=frame[3]; if (len<4+L) { send_resp_err(seq); break; }    // ensure declared bytes exist
      size_t copy=(L>=sizeof(s_last_text))?(sizeof(s_last_text)-1):L;    // clamp to buffer-1 for NUL
      memcpy(s_last_text,frame+4,copy); s_last_text[copy]='\0';          // stash and terminate
      if (node_display_available())
        node_display_draw_two_lines("RX Msg:", s_last_text);             // non-fatal UI side-effect
      Serial.printf("[RX] %s\n", s_last_text);                           // debug trace to serial
      { uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);     // minimal ack with ID
        send_tag_value(b,i,TAG_ID);
        frame_end(b,i); protocol_send(b,i);
      }
      break;
    }

    // Fallback: unknown verb -> RESP_ERR (don’t crash; caller gets an error frame).
    default:
      send_resp_err(seq);
      break;
  }
}


