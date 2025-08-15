#pragma once
/**
 * @file node_display.hpp
 * @brief Minimal OLED UI for ViaText node (TTGO LoRa32 0.96" SSD1306).
 *
 * Responsibilities
 * ----------------
 * - Initialize I2C + SSD1306 and report availability.
 * - Provide a few small, stable rendering helpers so higher layers
 *   never touch Adafruit/SSD1306 directly.
 *
 * What this is NOT:
 * - A retained UI or layout engine. Calls render directly.
 *
 * Typical use
 * -----------
 *   if (node_display_begin(21, 22, 0x3C)) {
 *     node_display_draw_boot("ViaText Booting...");
 *     node_display_draw_id(current_id);
 *   }
 */

#include <Arduino.h>
#include <stdint.h>

/// Initialize I2C + OLED. Tries `addr` then (if that fails) 0x3D.
/// Returns true if the display is ready.
bool node_display_begin(int sda_pin, int scl_pin, uint8_t addr = 0x3C);

/// Is the display initialized and ready?
bool node_display_available();

/// Clear the screen and push to panel.
void node_display_clear();

/// Draw a simple boot/status message (small font).
void node_display_draw_boot(const char* msg);

/// Draw the Node ID screen:
///  Line 1: "ViaText Node"
///  Line 2: "NODE ID:"
///  Line 3: <id> (large font)
void node_display_draw_id(const char* id);

/// Draw two arbitrary lines (small font), useful for quick status.
void node_display_draw_two_lines(const char* line1, const char* line2);

/// Low-level flush if you did multiple draws (usually not needed externally).
void node_display_flush();
