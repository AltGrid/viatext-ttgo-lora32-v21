/**
 * @file node_display.cpp
 * @brief Implementation for the matching node_display.hpp (presentation-only OLED shim).
 *
 * Notes:
 * - API/overview lives in node_display.hpp. Keep this file focused on "how" not "what".
 * - For usage examples, see your node bring-up tests or sketches that call node_display_*().
 * - Style: block-by-block reasoning; only line comments where maintainers trip.
 */

#include "node_display.hpp"     // Public API surface; keep Adafruit types out of this header.

/* Arduino I2C core (ESP32/Arduino). Repo (ESP32 core): https://github.com/espressif/arduino-esp32 */
#include <Wire.h>               // TwoWire / I2C bus access

/* Graphics primitives. Repo: https://github.com/adafruit/Adafruit-GFX-Library */
#include <Adafruit_GFX.h>       // Text, lines, rectangles, coordinate space

/* SSD1306 panel driver. Repo: https://github.com/adafruit/Adafruit_SSD1306 */
#include <Adafruit_SSD1306.h>   // 128x64 OLED control (buffered)

/*------------------------------------------------------------------------------
  Internal state
  --------------
  We hide concrete driver types and globals in an anonymous namespace to keep the
  header clean and avoid leaking Adafruit types across translation units.
------------------------------------------------------------------------------*/
namespace {
constexpr int  kWidth  = 128;   // Physical panel width (columns)
constexpr int  kHeight = 64;    // Physical panel height (rows)
constexpr int  kReset  = -1;    // No dedicated reset pin on TTGO; use -1 per driver spec

// Global, single display instance bound to Wire. This is acceptable because the
// node has exactly one panel; callers never see this concrete type.
Adafruit_SSD1306 g_display(kWidth, kHeight, &Wire, kReset);

// Latched "display ready" flag. All draw functions short-circuit if false.
bool g_ok = false;

// Optional hook to centralize any "make sure device is awake" work later.
// Currently a no-op, but kept to make guard logic grep-able.
inline void ensure_on() {
  if (!g_ok) return;            // If init failed, all display ops are no-ops by design.
  // no-op hook; kept so we can centralize guard if needed
}
} // namespace

/*------------------------------------------------------------------------------
  node_display_begin
  ------------------
  Bring up I2C and the SSD1306. We try the requested address first, then a known
  TTGO fallback (0x3D). On success we set a minimal "Display OK" message.

  Assumptions:
  - Running on an Arduino core where Wire.begin(sda,scl) selects the pins.
  - Only one OLED is present; we keep a single global driver instance.

  Invariants:
  - g_ok reflects whether the panel accepted init.
  - Post-failure, all other functions must be safe no-ops.

  Tradeoffs:
  - We accept global state for simplicity and determinism on small MCUs.
  - No retries/backoff here; higher layers decide how noisy to be.

  Phases:
  1) Start I2C on provided pins.
  2) Try primary address; if that fails and isn't already 0x3D, try 0x3D.
  3) If up, set legible defaults, paint a one-shot status, and latch g_ok.
------------------------------------------------------------------------------*/
bool node_display_begin(int sda_pin, int scl_pin, uint8_t addr) {
  // Phase 1: I2C bus selection on ESP32-style cores.
  Wire.begin(sda_pin, scl_pin);

  // Phase 2: Probe primary, then fallback commonly seen on TTGO boards.
  g_ok = g_display.begin(SSD1306_SWITCHCAPVCC, addr);
  if (!g_ok && addr != 0x3D) {                  // Avoid redundant retry if caller already passed 0x3D.
    g_ok = g_display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  // Phase 3: Minimal confirmation splash; keep it short to not block boot.
  if (g_ok) {
    g_display.clearDisplay();
    g_display.setTextColor(SSD1306_WHITE);      // Monochrome buffer; WHITE lights pixels.
    g_display.setTextSize(1);                   // Small font = most lines visible for diagnostics.
    g_display.setCursor(0, 0);                  // Top-left origin.
    g_display.println(F("Display OK"));         // F() stores literal in flash to save RAM.
    g_display.display();                        // Push buffer to glass.
  }
  return g_ok;
}

/*------------------------------------------------------------------------------
  node_display_available
  ----------------------
  Report latched availability. This lets higher layers guard optional UI code
  without coupling to driver internals.
------------------------------------------------------------------------------*/
bool node_display_available() {
  return g_ok;                                   // Pure read of the readiness flag.
}

/*------------------------------------------------------------------------------
  node_display_clear
  ------------------
  Clear and flush the screen. Safe no-op when display is absent.

  Phases:
  1) Guard on g_ok.
  2) Clear local buffer.
  3) Flush buffer to panel.
------------------------------------------------------------------------------*/
void node_display_clear() {
  if (!g_ok) return;                             // Headless-friendly: do nothing if panel is down.
  g_display.clearDisplay();                      // Zero the framebuffer.
  g_display.display();                           // Commit to glass.
}

/*------------------------------------------------------------------------------
  node_display_draw_boot
  ----------------------
  Paint a simple boot/status screen. Always uses small font for maximum text.
  If a custom message is provided, we stack it beneath the fixed header.

  Assumptions:
  - msg may be null or empty; both are valid and ignored.

  Phases:
  1) Guard and clear.
  2) Draw fixed header ("ViaText Booting...").
  3) Optionally draw caller-provided line at y=12.
  4) Flush.
------------------------------------------------------------------------------*/
void node_display_draw_boot(const char* msg) {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);
  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  g_display.println(F("ViaText Booting..."));
  if (msg && *msg) {                             // Defensive: require non-null and non-empty.
    g_display.setCursor(0, 12);                  // Next text row (8px font height + spacing).
    g_display.println(msg);
  }
  g_display.display();
}

/*------------------------------------------------------------------------------
  node_display_draw_id
  --------------------
  Render a three-line identity screen: title, label, and the ID in large font.

  Assumptions:
  - 'id' may be null; we print an empty string in that case to avoid crashes.

  Tradeoffs:
  - Fixed positions and sizes for legibility; no horizontal centering logic to
    keep code obvious and predictable in the field.

  Phases:
  1) Guard and clear.
  2) Title line (small font).
  3) Label line (small font).
  4) ID line (large font).
  5) Flush.
------------------------------------------------------------------------------*/
void node_display_draw_id(const char* id) {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);

  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  g_display.println(F("ViaText Node"));

  g_display.setCursor(0, 16);                    // Leave a blank row between title and label for clarity.
  g_display.println(F("NODE ID:"));

  g_display.setTextSize(2);                      // Large, readable at arm's length.
  g_display.setCursor(0, 30);                    // Chosen to keep 2x font within 64px height.
  g_display.print(id ? id : "");                 // Null-safe; avoid derefing null pointer.
  g_display.display();
}

/*------------------------------------------------------------------------------
  node_display_draw_two_lines
  ---------------------------
  Quick two-line text helper in small font. Useful for ad-hoc status.

  Assumptions:
  - Either line pointer may be null; null lines are simply skipped.

  Phases:
  1) Guard and clear.
  2) Draw line1 if present.
  3) Draw line2 if present (auto-advances vertically via println()).
  4) Flush.
------------------------------------------------------------------------------*/
void node_display_draw_two_lines(const char* line1, const char* line2) {
  if (!g_ok) return;
  g_display.clearDisplay();
  g_display.setTextColor(SSD1306_WHITE);
  g_display.setTextSize(1);
  g_display.setCursor(0, 0);
  if (line1) g_display.println(line1);          // Safe: println(nullptr) would crash; we guard.
  if (line2) g_display.println(line2);
  g_display.display();
}

/*------------------------------------------------------------------------------
  node_display_flush
  ------------------
  Expose a raw "flush buffer to glass" call. Most helpers already flush, but
  this is useful when composing multiple draws manually.

  Phases:
  1) Guard.
  2) Flush.
------------------------------------------------------------------------------*/
void node_display_flush() {
  if (!g_ok) return;
  g_display.display();
}
