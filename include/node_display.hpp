#pragma once
/**
 * @page vt-node-display ViaText Node Display (TTGO LoRa32 + SSD1306)
 * @file node_display.hpp
 * @brief Minimal OLED UI helpers for ViaText nodes using a 0.96" SSD1306 panel.
 *
 * Overview
 * --------
 * This header declares a tiny, reliable display shim for the TTGO LoRa32 class
 * of boards with a 0.96" SSD1306 OLED. It exists for one reason: keep the UI
 * simple and stable so the rest of the node never talks directly to third-party
 * libraries. This layer gives you a few high-value calls (boot text, ID screen, 
 * two-line status) and nothing more. No retained widgets, no layouts, 
 * no theme engine—just draw and push.
 *
 * Where This Fits
 * ---------------
 * - Transport and protocol live elsewhere (node_protocol.*).
 * - Command/state handling and persistence live elsewhere (node_interface.*).
 * - This module is presentation only. It should be safe to ignore or replace.
 *
 * Philosophy
 * ----------
 * - Simplicity: The display is optional, and the API surface is tiny. If the
 *   panel is missing or damaged, the node should still run. All functions are
 *   safe to call after a failed init; they no-op cleanly.
 * - Portability: I2C pins are parameters, not assumptions. The code targets
 *   Arduino + Adafruit_SSD1306 but is intentionally quarantined to this file.
 *   If you later swap drivers or panels, you edit one place.
 * - Autonomy: This is for field work. It favors deterministic behavior, clear
 *   text, and predictable output over pretty animations. If you cannot explain
 *   the draw calls in a dark room with a headlamp, they do not belong here.
 *
 * Responsibilities
 * ----------------
 * - Initialize the OLED over I2C and report availability.
 * - Provide idempotent helpers for a boot message, the node ID screen, and a
 *   two-line status. Keep font sizes fixed and readable.
 * - Offer a clear() and a flush() so callers can compose simple sequences.
 *
 * Non-Goals
 * ---------
 * - Retained UI, layout systems, or off-screen object models.
 * - Fancy fonts, icons, or proportional text. We stick to readable defaults.
 * - Hardware abstraction beyond what is necessary to draw text reliably.
 *
 * Dependencies
 * ------------
 * - Arduino core for ESP32 (Wire/TwoWire).
 * - Adafruit_GFX and Adafruit_SSD1306 libraries.
 * - A 128x64 SSD1306 connected via I2C. Address defaults to 0x3C with a
 *   fallback probe at 0x3D (common on TTGO boards).
 *
 * Electrical Notes (TTGO LoRa32 V2.x defaults)
 * --------------------------------------------
 * - SDA: GPIO 21
 * - SCL: GPIO 22
 * - VCC: 3V3
 * - GND: GND
 * If you reroute I2C, pass the actual pins to node_display_begin().
 *
 * Typical Usage
 * -------------
 * @code
 * if (node_display_begin( 21,  22,  0x3C)) {
 *     node_display_draw_boot("ViaText Booting...");
 *     node_display_draw_id(current_id);
 * } else {
 *     // Carry on headless; the node can still operate.
 * }
 * @endcode
 *
 * Failure Modes
 * -------------
 * - Missing panel or wrong address: node_display_begin() returns false.
 * - Any subsequent draw or flush call becomes a no-op (safe to call).
 * - This design avoids throwing or halting inside display routines. The radio
 *   and protocol keep running even if glass is dark.
 *
 * Extension Points
 * ----------------
 * - If you add more helpers, keep them small, synchronous, and text-first.
 * - Do not leak Adafruit types through this header. Keep the boundary clean so
 *   swapping the driver or display later does not ripple through the codebase.
 *
 * Testing Tips
 * ------------
 * - Use node_display_available() to guard optional UI paths in integration
 *   tests. You should be able to run the node headless during automated CI.
 * - For bench work, print the same strings you draw to Serial so you can read
 *   them over USB even when the panel is disconnected.
 *
 * License and Attribution
 * -----------------------
 * This header is written for long-term readability. Favor clarity over clever
 * tricks. Avoid hidden state. Keep the text legible at arm's length.
 *
 * @author Leo
 * @author ChatGPT
 */

#include <Arduino.h>
#include <stdint.h>
/**
 * @brief Initialize the OLED display over I2C.
 *
 * @param sda_pin GPIO pin number used for SDA.
 * @param scl_pin GPIO pin number used for SCL.
 * @param addr    I2C address of the display (default 0x3C).
 * @return true if the display was successfully initialized, false otherwise.
 *
 * @details
 * This function attempts to bring up the SSD1306 OLED. It tries the
 * provided address first, then falls back to 0x3D (a common alternative
 * on TTGO LoRa32 boards). On success, internal state is marked as ready
 * and subsequent drawing calls will have effect. On failure, all draw
 * functions degrade to safe no-ops.
 *
 * @note In field use, this ensures the node can continue operating even
 * when the display is absent or damaged.
 */
bool node_display_begin(int sda_pin, int scl_pin, uint8_t addr = 0x3C);

/**
 * @brief Check if the display is available.
 *
 * @return true if initialization succeeded and the display is ready,
 *         false otherwise.
 *
 * @details
 * This should be called before attempting to draw. Guards optional UI
 * paths in code so the node can run headless without error.
 */
bool node_display_available();

/**
 * @brief Clear the screen.
 *
 * @details
 * Clears the entire framebuffer and pushes the change to the panel
 * immediately. Safe to call repeatedly. No effect if display is not
 * available.
 */
void node_display_clear();

/**
 * @brief Draw a simple boot or status message.
 *
 * @param msg Optional extra line of text to display under the fixed
 *            "ViaText Booting..." header. May be null or empty.
 *
 * @details
 * This helper paints a predictable boot screen using the small font.
 * Useful during bring-up to confirm hardware and identity. Messages
 * are truncated if too long for the display width.
 */
void node_display_draw_boot(const char* msg);

/**
 * @brief Draw the Node ID screen.
 *
 * @param id Pointer to a string containing the node ID.
 *
 * @details
 * Renders a three-line layout:
 *   Line 1: "ViaText Node"
 *   Line 2: "NODE ID:"
 *   Line 3: The ID itself in large font.
 *
 * If @p id is null, prints an empty string. The large font makes the
 * ID legible at arm’s length in the field.
 */
void node_display_draw_id(const char* id);

/**
 * @brief Draw two lines of arbitrary text.
 *
 * @param line1 First line (small font). May be null.
 * @param line2 Second line (small font). May be null.
 *
 * @details
 * Clears the display, writes up to two lines in small font, and flushes
 * to the panel. Useful for ad-hoc status or debugging output. Null
 * pointers are ignored safely.
 */
void node_display_draw_two_lines(const char* line1, const char* line2);

/**
 * @brief Flush pending draw operations to the display.
 *
 * @details
 * Pushes the current framebuffer to the panel without clearing or
 * redrawing. Usually not required because the helper functions already
 * flush automatically, but exposed for manual composition when multiple
 * draws are performed in sequence.
 */
void node_display_flush();
