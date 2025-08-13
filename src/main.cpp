// src/main.cpp
// ViaText TTGO LoRa32 V2.1 — strict A..E loop (holders -> add_message -> tick -> get_message)
// Keep this file simple; device behavior lives in handle_message().

#include <Arduino.h>

// Core + model types (submodule: external/viatext-core)
#include "external/viatext-core/include/viatext/core.hpp"
#include "external/viatext-core/include/viatext/message.hpp"
#include "external/viatext-core/include/viatext/message_id.hpp"
#include "external/viatext-core/include/viatext/package.hpp"

// LoRa transport (SX1262 on LilyGO v2.1)
#include "external/viatext-core/include/viatext/transport/transport_lora_sx1262.hpp"

// ---------- Tunables ----------
static constexpr uint32_t BAUD            = 115200;
static constexpr size_t   LORA_RX_CAP     = 256;   // LoRa MTU guarded by transport
static constexpr uint16_t SERIAL_LINE_CAP = 512;   // newline-terminated collector

// ---------- Globals ----------
using viatext::Core;
using viatext::Package;
using viatext::transport::LoRaSX1262;
using viatext::transport::LoRaConfig;

Core       core{"B1"};      // set per device: A/B/C/D etc.
LoRaSX1262 lora;

// serial line buffer (simple)
static char   serial_line[SERIAL_LINE_CAP];
static size_t serial_len = 0;

// ---------- Device policy ----------
static void handle_message(const Package& pkg)
{
  // Fan out to Serial (operator view) and LoRa (radio mesh).
  // Keep payload raw (STAMP~from~to~data) for transparency.

  // 1) Serial out
  Serial.print("TX/USB ");
  Serial.write(pkg.payload.data(), pkg.payload.size());
  Serial.print("\n");

  // 2) LoRa out
  if (!pkg.payload.empty() && pkg.payload.size() <= lora.mtu()) {
    auto tx = lora.send(reinterpret_cast<const uint8_t*>(pkg.payload.data()),
                        pkg.payload.size());
    if (tx != viatext::transport::TxResult::Ok) {
      Serial.println("[warn] lora.send busy/error");
    }
  } else if (pkg.payload.size() > lora.mtu()) {
    Serial.println("[warn] payload exceeds LoRa MTU");
  }
}

// ---------- Ingress holders ----------
static bool acquire_serial_package(Package& out_pkg)
{
  bool have_line = false;

  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') { have_line = true; break; }

    if (serial_len + 1 < SERIAL_LINE_CAP) {
      serial_line[serial_len++] = static_cast<char>(c);
    } else {
      // overflow: drop this line to stay simple/deterministic
      serial_len = 0;
      Serial.println("[warn] serial line overflow; flushed");
    }
  }

  if (!have_line) return false;

  // Build Package from collected line
  out_pkg = Package{};
  out_pkg.payload.assign(serial_line, serial_line + serial_len);
  out_pkg.args.set_flag("-serial");
  out_pkg.args.set("--src", "serial");

  serial_len = 0;
  return true;
}

static bool acquire_lora_package(Package& out_pkg)
{
  lora.poll(); // service ISR flags if needed

  if (lora.available() == 0) return false;

  uint8_t buf[LORA_RX_CAP];
  size_t  out_len = 0;
  auto    rx = lora.recv(buf, sizeof(buf), out_len);
  if (rx != viatext::transport::RxResult::Ok || out_len == 0) return false;

  out_pkg = Package{};
  out_pkg.payload.assign(reinterpret_cast<const char*>(buf),
                         reinterpret_cast<const char*>(buf) + out_len);
  out_pkg.args.set_flag("-lora");
  out_pkg.args.set("--src", "lora");
  return true;
}

// ---------- Arduino lifecycle ----------
void setup()
{
  Serial.begin(BAUD);
  while (!Serial) { /* wait on native USB */ }

  Serial.println("\n[ViaText] TTGO v2.1 boot");
  Serial.print("[ViaText] node id: ");
  Serial.println(core.get_node_id().c_str());

  // Bring up LoRa
  LoRaConfig cfg; // set fields if you later expose BW/SF/CR/power
  if (!lora.begin(cfg)) {
    Serial.println("[fatal] LoRa init failed");
  } else {
    Serial.print("[ViaText] LoRa up, mtu=");
    Serial.println(static_cast<int>(lora.mtu()));
  }

  // Optional operator heartbeat
  Package hello;
  hello.payload.assign("HELLO~~~ViaText LoRa node");
  hello.args.set_flag("-serial"); // local origin
  core.add_message(hello);
}

void loop()
{
  // --- A) Listen (collect holders only, no Core calls yet) ---
  Package serial_pkg, radio_pkg;
  const bool have_serial = acquire_serial_package(serial_pkg);
  const bool have_radio  = acquire_lora_package(radio_pkg);

  // --- B) add_message(...) for each source (at most one each per loop) ---
  if (have_serial) {
    if (!core.add_message(serial_pkg)) {
      Serial.println("[drop] inbox full (serial)");
    }
  }
  if (have_radio) {
    if (!core.add_message(radio_pkg)) {
      Serial.println("[drop] inbox full (lora)");
    }
  }

  // --- C) tick(now) exactly once per loop ---
  core.tick(millis());

  // --- D/E) Drain and handle (device policy lives in handle_message) ---
  Package out;
  while (core.get_message(out)) {
    handle_message(out);
  }
}
