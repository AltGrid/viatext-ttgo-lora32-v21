#include "node_interface.hpp"
#include "node_protocol.hpp"
#include "node_display.hpp"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

// ============================================================================
// Persistent state + storage
// ============================================================================
static Preferences s_prefs;
static bool        s_prefs_open = false;
static char        s_id[32]    = "HckrMn";   // 31 chars + NUL
static char        s_alias[32] = "";         // friendly name
static uint32_t    s_freq_hz   = 915000000;
static uint8_t     s_sf        = 9;
static uint32_t    s_bw_hz     = 125000;
static uint8_t     s_cr        = 5;
static int8_t      s_tx_pwr    = 17;
static uint8_t     s_chan      = 0;
static uint8_t     s_mode      = 0;
static uint8_t     s_hops      = 1;
static uint32_t    s_beacon_s  = 0;
static uint16_t    s_buf_size  = 32;
static uint8_t     s_ack_mode  = 0;

// last received text for UI/debug
static char s_last_text[64] = "";

// ============================================================================
// Load / Save helpers
// ============================================================================
static void load_from_nvs() {
  s_prefs_open = s_prefs.begin("viatext", /*readOnly=*/false);
  if (!s_prefs_open) return;
  s_prefs.getString("id", s_id, sizeof(s_id));
  s_prefs.getString("alias", s_alias, sizeof(s_alias));
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

static void save_to_nvs() {
  if (!s_prefs_open) {
    s_prefs_open = s_prefs.begin("viatext", false);
    if (!s_prefs_open) return;
  }
  s_prefs.putString("id", s_id);
  s_prefs.putString("alias", s_alias);
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
static bool is_valid_id(const char* p) {
  size_t n = strlen(p);
  if (n == 0 || n > 31) return false;
  for (size_t i = 0; i < n; ++i) {
    char c = p[i];
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              (c == '-' || c == '_');
    if (!ok) return false;
  }
  return true;
}
static bool is_valid_sf(uint8_t v)      { return v >= 7 && v <= 12; }
static bool is_valid_cr(uint8_t v)      { return v >= 5 && v <= 8; }
static bool is_valid_ack(uint8_t v)     { return v == 0 || v == 1; }

// ============================================================================
// TLV helpers
// ============================================================================
static inline void frame_begin(uint8_t verb, uint8_t seq, uint8_t* buf, size_t& i) {
  i = 0;
  buf[i++] = verb;
  buf[i++] = 0;
  buf[i++] = seq;
  buf[i++] = 0;
}
static inline void tlv_put(uint8_t* buf, size_t& i, uint8_t tag, const void* p, uint8_t len) {
  buf[i++] = tag;
  buf[i++] = len;
  if (len) { memcpy(buf + i, p, len); i += len; }
}
template<typename T>
static inline void tlv_put_le(uint8_t* buf, size_t& i, uint8_t tag, T value) {
  uint8_t tmp[sizeof(T)];
  for (size_t j=0;j<sizeof(T);++j) tmp[j] = (uint8_t)((value >> (8*j)) & 0xFF);
  tlv_put(buf, i, tag, tmp, sizeof(T));
}
static inline void frame_end(uint8_t* buf, size_t& i) {
  buf[3] = static_cast<uint8_t>(i - 4);
}
static const uint8_t* tlv_find(const uint8_t* frame, size_t len, uint8_t tag, uint8_t& out_len) {
  if (len < 4) return nullptr;
  size_t tlv_len = frame[3];
  if (4 + tlv_len > len) return nullptr;
  size_t off = 4, end = 4 + tlv_len;
  while (off + 2 <= end) {
    uint8_t t = frame[off++];
    uint8_t L = frame[off++];
    if (off + L > end) break;
    if (t == tag) { out_len = L; return frame + off; }
    off += L;
  }
  return nullptr;
}
template<typename T>
static bool tlv_read_le(const uint8_t* p, uint8_t len, T& out) {
  if (len != sizeof(T)) return false;
  out = 0;
  for (size_t j=0;j<sizeof(T);++j) out |= ((T)p[j]) << (8*j);
  return true;
}

// ============================================================================
// RESP helpers
// ============================================================================
static void send_resp_err(uint8_t seq) {
  uint8_t b[8]; size_t i;
  frame_begin(Verb::RESP_ERR, seq, b, i);
  frame_end(b, i);
  protocol_send(b, i);
}

static void send_tag_value(uint8_t* buf, size_t& i, uint8_t tag) {
  switch(tag) {
    // --- Identity / System ---
    case TAG_ID: {
      uint8_t L = (uint8_t)strnlen(s_id,sizeof(s_id));
      tlv_put(buf,i,TAG_ID,s_id,L);
      break;
    }
    case TAG_ALIAS: {
      uint8_t L = (uint8_t)strnlen(s_alias,sizeof(s_alias));
      tlv_put(buf,i,TAG_ALIAS,s_alias,L);
      break;
    }
    case TAG_FW_VERSION: {
      const char* ver = "1.0.0"; // TODO: tie to build version macro if available
      tlv_put(buf,i,tag,ver,(uint8_t)strlen(ver));
      break;
    }
    case TAG_UPTIME_S: {
      uint32_t uptime = millis() / 1000;
      tlv_put_le<uint32_t>(buf,i,tag,uptime);
      break;
    }
    case TAG_BOOT_TIME: {
      uint32_t boot_time = 0; // TODO: real epoch from RTC if available
      tlv_put_le<uint32_t>(buf,i,tag,boot_time);
      break;
    }

    // --- Radio ---
    case TAG_FREQ_HZ:    tlv_put_le<uint32_t>(buf,i,tag,s_freq_hz); break;
    case TAG_SF:         tlv_put_le<uint8_t>(buf,i,tag,s_sf); break;
    case TAG_BW_HZ:      tlv_put_le<uint32_t>(buf,i,tag,s_bw_hz); break;
    case TAG_CR:         tlv_put_le<uint8_t>(buf,i,tag,s_cr); break;
    case TAG_TX_PWR_DBM: tlv_put_le<int8_t>(buf,i,tag,s_tx_pwr); break;
    case TAG_CHAN:       tlv_put_le<uint8_t>(buf,i,tag,s_chan); break;

    // --- Behavior / Routing ---
    case TAG_MODE:       tlv_put_le<uint8_t>(buf,i,tag,s_mode); break;
    case TAG_HOPS:       tlv_put_le<uint8_t>(buf,i,tag,s_hops); break;
    case TAG_BEACON_SEC: tlv_put_le<uint32_t>(buf,i,tag,s_beacon_s); break;
    case TAG_BUF_SIZE:   tlv_put_le<uint16_t>(buf,i,tag,s_buf_size); break;
    case TAG_ACK_MODE:   tlv_put_le<uint8_t>(buf,i,tag,s_ack_mode); break;

    // --- Diagnostics (stubbed defaults, replace with real values when ready) ---
    case TAG_RSSI_DBM:   tlv_put_le<int16_t>(buf,i,tag,-42); break;      // TODO: real radio RSSI
    case TAG_SNR_DB:     tlv_put_le<int8_t>(buf,i,tag,7); break;         // TODO: real SNR
    case TAG_VBAT_MV:    tlv_put_le<uint16_t>(buf,i,tag,3700); break;    // TODO: read ADC
    case TAG_TEMP_C10:   tlv_put_le<int16_t>(buf,i,tag,215); break;      // TODO: real temp
    case TAG_FREE_MEM:   tlv_put_le<uint32_t>(buf,i,tag,123456); break;  // TODO: heap info
    case TAG_FREE_FLASH: tlv_put_le<uint32_t>(buf,i,tag,654321); break;  // TODO: FS info
    case TAG_LOG_COUNT:  tlv_put_le<uint16_t>(buf,i,tag,0); break;       // TODO: log size

    default:
      // Unknown tag — ignore silently
      break;
  }
}


// ============================================================================
// Public API
// ============================================================================
void node_interface_begin() {
  load_from_nvs();
}
const char* node_interface_id() { return s_id; }
void node_interface_send_hello() { 
  uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,0,b,i);
  send_tag_value(b,i,TAG_ID);
  frame_end(b,i);
  protocol_send(b,i);
}
const char* node_interface_last_text() { return s_last_text; }

void node_interface_on_packet(const uint8_t* frame, size_t len) {
  if (!frame || len < 4) return;
  const uint8_t verb = frame[0];
  const uint8_t seq  = frame[2];

  switch (verb) {
    case Verb::GET_ID:
    case Verb::PING:
      { uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
        send_tag_value(b,i,TAG_ID);
        frame_end(b,i); protocol_send(b,i);
      }
      break;

    case Verb::SET_ID: {
      uint8_t L=0; const uint8_t* p = tlv_find(frame,len,TAG_ID,L);
      if (!p||L==0) { send_resp_err(seq); break; }
      char tmp[sizeof(s_id)]; size_t copy = (L>=sizeof(tmp))?(sizeof(tmp)-1):L;
      memcpy(tmp,p,copy); tmp[copy]='\0';
      if (!is_valid_id(tmp)) { send_resp_err(seq); break; }
      strncpy(s_id,tmp,sizeof(s_id)); save_to_nvs();
      node_display_draw_id(s_id);
      { uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
        send_tag_value(b,i,TAG_ID);
        frame_end(b,i); protocol_send(b,i);
      }
      node_interface_send_hello();
      break;
    }

    case Verb::GET_PARAM: {
      uint8_t b[128]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
      size_t off=4,end=4+frame[3];
      while (off+2<=end) {
        uint8_t t=frame[off++]; uint8_t L=frame[off++];
        off+=L; // skip value
        if (L==0) send_tag_value(b,i,t);
      }
      frame_end(b,i); protocol_send(b,i);
      break;
    }

    case Verb::SET_PARAM: {
      bool ok=true;
      size_t off=4,end=4+frame[3];
      while (off+2<=end) {
        uint8_t t=frame[off++]; uint8_t L=frame[off++];
        const uint8_t* p = frame+off; off+=L;
        switch(t) {
          case TAG_ALIAS: strncpy(s_alias,(const char*)p,std::min((size_t)L,sizeof(s_alias)-1)); break;
          case TAG_FREQ_HZ: ok &= tlv_read_le<uint32_t>(p,L,s_freq_hz); break;
          case TAG_SF: { uint8_t v; if(tlv_read_le<uint8_t>(p,L,v)&&is_valid_sf(v)) s_sf=v; else ok=false; break; }
          case TAG_BW_HZ: ok &= tlv_read_le<uint32_t>(p,L,s_bw_hz); break;
          case TAG_CR: { uint8_t v; if(tlv_read_le<uint8_t>(p,L,v)&&is_valid_cr(v)) s_cr=v; else ok=false; break; }
          case TAG_TX_PWR_DBM: ok &= tlv_read_le<int8_t>(p,L,s_tx_pwr); break;
          case TAG_CHAN: ok &= tlv_read_le<uint8_t>(p,L,s_chan); break;
          case TAG_MODE: ok &= tlv_read_le<uint8_t>(p,L,s_mode); break;
          case TAG_HOPS: ok &= tlv_read_le<uint8_t>(p,L,s_hops); break;
          case TAG_BEACON_SEC: ok &= tlv_read_le<uint32_t>(p,L,s_beacon_s); break;
          case TAG_BUF_SIZE: ok &= tlv_read_le<uint16_t>(p,L,s_buf_size); break;
          case TAG_ACK_MODE: { uint8_t v; if(tlv_read_le<uint8_t>(p,L,v)&&is_valid_ack(v)) s_ack_mode=v; else ok=false; break; }
        }
      }
      if (!ok) { send_resp_err(seq); break; }
      save_to_nvs();
      uint8_t b[128]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
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
      frame_end(b,i); protocol_send(b,i);
      break;
    }

    case Verb::GET_ALL: {
      uint8_t b[192]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
      for (uint8_t t : {
        TAG_ID,TAG_ALIAS,TAG_FREQ_HZ,TAG_SF,TAG_BW_HZ,TAG_CR,TAG_TX_PWR_DBM,
        TAG_CHAN,TAG_MODE,TAG_HOPS,TAG_BEACON_SEC,TAG_BUF_SIZE,TAG_ACK_MODE,
        TAG_RSSI_DBM,TAG_SNR_DB,TAG_VBAT_MV,TAG_TEMP_C10,TAG_FREE_MEM,TAG_FREE_FLASH,TAG_LOG_COUNT
      }) send_tag_value(b,i,t);
      frame_end(b,i); protocol_send(b,i);
      break;
    }

    case Verb::MSG: {
      if (len<4) { send_resp_err(seq); break; }
      uint8_t L=frame[3]; if (len<4+L) { send_resp_err(seq); break; }
      size_t copy=(L>=sizeof(s_last_text))?(sizeof(s_last_text)-1):L;
      memcpy(s_last_text,frame+4,copy); s_last_text[copy]='\0';
      if (node_display_available())
        node_display_draw_two_lines("RX Msg:", s_last_text);
      Serial.printf("[RX] %s\n", s_last_text);
      { uint8_t b[64]; size_t i; frame_begin(Verb::RESP_OK,seq,b,i);
        send_tag_value(b,i,TAG_ID);
        frame_end(b,i); protocol_send(b,i);
      }
      break;
    }

    default:
      send_resp_err(seq);
      break;
  }
}
