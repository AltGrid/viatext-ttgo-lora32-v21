#include <Arduino.h>
#include <cstring>
#include <Wire.h>
#include <PacketSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== Protocol =====
enum Verb : uint8_t { GET_ID=0x01, SET_ID=0x02, PING=0x03, RESP_OK=0x90, RESP_ERR=0x91 };
enum Tag  : uint8_t { TAG_ID=0x01 };

// ===== Display =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1     // no reset pin
#define OLED_ADDR     0x3C   // common for TTGO 0.96" OLEDs

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== Serial (SLIP) =====
// Equivalent explicit form: PacketSerial_<SLIP, SLIP::END, 256> ps;
SLIPPacketSerial ps;

// Keep ID bounded and mutable
static char device_id[32] = "vt-arduino";

// ===== UI helpers =====
static void draw_id() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("ViaText Node"));

  display.setCursor(0, 16);
  display.setTextSize(1);
  display.println(F("NODE ID:"));

  display.setCursor(0, 30);
  display.setTextSize(2);
  display.print(device_id);

  display.display();
}

// ===== Protocol helpers =====
static void send_resp_err(uint8_t seq, uint8_t code) {
  uint8_t buf[16];
  size_t i = 0;
  buf[i++] = RESP_ERR;
  buf[i++] = 0;       // flags
  buf[i++] = seq;     // mirror
  buf[i++] = 0;       // TLV len (none for now)
  // You can add TLV(code) later for richer errors
  ps.send(buf, i);
}

static void send_resp_ok_with_id(uint8_t seq) {
  uint8_t buf[64];
  size_t i = 0;
  buf[i++] = RESP_OK;
  buf[i++] = 0;       // flags
  buf[i++] = seq;     // mirror
  buf[i++] = 0;       // TLV len placeholder

  // TLV: id
  const uint8_t id_len = (uint8_t)min<size_t>(strlen(device_id), 255);
  buf[i++] = TAG_ID;
  buf[i++] = id_len;
  memcpy(buf + i, device_id, id_len);
  i += id_len;

  buf[3] = (uint8_t)(i - 4);
  ps.send(buf, i);
}

// Parse TLVs from a frame (no heap; bounds-checked)
static void handle_set_id(const uint8_t* data, size_t len, uint8_t seq) {
  if (len < 4) { send_resp_err(seq, 1); return; }
  const uint8_t tlv_len = data[3];
  if ((size_t)4 + tlv_len > len) { send_resp_err(seq, 2); return; }

  size_t off = 4;
  bool updated = false;
  const size_t tlv_end = 4 + tlv_len;

  while (off + 2 <= tlv_end) {
    uint8_t tag = data[off++];
    uint8_t l   = data[off++];
    if (off + l > tlv_end) break;

    if (tag == TAG_ID) {
      size_t copy_len = min<size_t>(l, sizeof(device_id) - 1);
      memcpy(device_id, data + off, copy_len);
      device_id[copy_len] = '\0';
      updated = true;
    }
    off += l;
  }

  if (updated) {
    draw_id();                  // update the OLED immediately
    send_resp_ok_with_id(seq);
  } else {
    send_resp_err(seq, 3);
  }
}

static void handle_frame(const uint8_t* data, size_t len) {
  if (len < 4) return;
  const Verb v   = (Verb)data[0];
  const uint8_t seq = data[2];

  switch (v) {
    case GET_ID:
      send_resp_ok_with_id(seq);
      break;
    case PING:
      // Could add a timestamp TLV later; for now echo ID
      send_resp_ok_with_id(seq);
      break;
    case SET_ID:
      handle_set_id(data, len, seq);
      break;
    default:
      send_resp_err(seq, 0xFF);
      break;
  }
}

static void onPacket(const uint8_t* buffer, size_t size) {
  handle_frame(buffer, size);
}

// ===== Arduino setup/loop =====
void setup() {
  // Serial/SLIP
  Serial.begin(115200);
  // while (!Serial) { ; } // optional on some USB CDC setups
  ps.setStream(&Serial);
  ps.setPacketHandler(&onPacket);

  // I2C + OLED
  Wire.begin(21, 22);  // TTGO default pins for OLED I2C
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    // If init fails, we just proceed without display (no crash)
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("ViaText Booting..."));
    display.display();
    delay(400);
    draw_id();  // show initial NODE ID
  }
}

void loop() {
  ps.update();
  // add non-blocking tasks here
}
