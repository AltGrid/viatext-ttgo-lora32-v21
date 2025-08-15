/**
 * @file node_display.cpp
 * @brief Implementation of the minimal OLED UI helpers for ViaText node.
 */

#include "node_display.hpp"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
constexpr int  kWidth  = 128;
constexpr int  kHeight = 64;
constexpr int  kReset  = -1;

Adafruit_SSD1306 g_display(kWidth, kHeight, &Wire, kReset);
bool g_ok = false;

inline void ensure_on() {
  if (!g_ok) return;
  // no-op hook; kept so we can centralize guard if needed
}
} // namespace

bool node_display_begin(int sda_pin, int scl_pin, uint8_t addr) {
  Wire.begin(sda_pin, scl_pin);

  // Try primary, then fallback commonly seen on TTGO.
  g_ok = g_display.begin(SSD1306_SWITCHCAPVCC, addr);
  if (!g_ok && addr != 0x3D) {
    g_ok = g_display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  if (g_ok) {
    g_display.clearDisplay();
    g_display.setTextColor(SSD1306_WHITE);
    g_display.setTextSize(1);
    g_display.setCursor(0, 0);
    g_display.println(F("Display OK"));
    g_display.display();
  }
  return g_ok;
}

bool node_display_available() {
  return g_ok;
}

void node_display_clear() {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.display();
}

void node_display_draw_boot(const char* msg) {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);
  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  g_display.println(F("ViaText Booting..."));
  if (msg && *msg) {
    g_display.setCursor(0, 12);
    g_display.println(msg);
  }
  g_display.display();
}

void node_display_draw_id(const char* id) {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);

  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  g_display.println(F("ViaText Node"));

  g_display.setCursor(0, 16);
  g_display.println(F("NODE ID:"));

  g_display.setTextSize(2);
  g_display.setCursor(0, 30);
  g_display.print(id ? id : "");
  g_display.display();
}

void node_display_draw_two_lines(const char* line1, const char* line2) {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);
  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  if (line1) g_display.println(line1);
  if (line2) g_display.println(line2);
  g_display.display();
}

void node_display_flush() {
  if (!g_ok) return;
  g_display.display();
}
