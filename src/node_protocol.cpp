// -----------------------------------------------------------------------------
// node_protocol.cpp
// Implementation of the transport/protocol glue declared in node_protocol.hpp.
//
// Notes:
//  * See node_protocol.hpp for API contract, design overview, and usage.
//  * See tests/ (or examples/) for how frames are built and consumed.
//  * This file is about mechanics: wiring SLIP/Serial, handler dispatch,
//    and buffer details.
//
// ----------------------------------------------------------------------------- 


#include "node_protocol.hpp"     // ViaText protocol core (packet handling, send/receive, update loop)
#include "node_interface.hpp"    // Node-specific interface layer (default RX callback: node_interface_on_packet)

#include <Arduino.h>             // Arduino framework core (pin control, Serial, timing, etc.)
#include <PacketSerial.h>        // Lightweight SLIP/packet framing library over serial
#include <cstring>               // C string utilities (memcpy, memset, strlen, etc.)


// Global SLIP transport (single shared PacketSerial instance for SLIP framing)
static SLIPPacketSerial g_ps;  

// Current handler (optional). If null, use node_interface_on_packet().
static void (*g_handler)(const uint8_t* frame, size_t len) = nullptr;

// PacketSerial callback: invoked whenever a full SLIP frame is received.
// This function routes the decoded packet to either a user-specified
// handler (if installed) or falls back to the default ViaText handler.
//
// Parameters:
//   buffer : pointer to the received SLIP frame payload
//   size   : number of bytes in the payload
static void on_slip_packet(const uint8_t* buffer, size_t size) {
    // If a custom handler is registered, forward the packet there
    if (g_handler) {
        g_handler(buffer, size);
    } else {
        // Otherwise, pass the packet into the default node interface handler
        node_interface_on_packet(buffer, size);
    }
}

// -----------------------------------------------------------------------------
// Initialize the protocol transport
// - Start the hardware serial port at the given baud rate
// - Bind it to our PacketSerial instance
// - Register the packet handler callback
// -----------------------------------------------------------------------------
void node_protocol_begin(unsigned long baud) {
    Serial.begin(baud);                   // 1) Open Serial at requested speed
    g_ps.setStream(&Serial);              // 2) Attach Serial stream to PacketSerial
    g_ps.setPacketHandler(&on_slip_packet); // 3) Tell PacketSerial what to do on full packet
}

// -----------------------------------------------------------------------------
// Service routine for the protocol transport
// - Should be called often (e.g., each loop() tick)
// - Pulls in any new serial bytes and fires callbacks if a full frame arrives
// -----------------------------------------------------------------------------
void node_protocol_update() {
    g_ps.update();  // Non-blocking pump of the PacketSerial state machine
}

// -----------------------------------------------------------------------------
// Replace or restore the inbound packet handler
// - Pass a function pointer to redirect packets
// - Pass nullptr to fall back on the default node_interface_on_packet()
// -----------------------------------------------------------------------------
void node_protocol_set_handler(void (*handler)(const uint8_t* frame, size_t len)) {
    g_handler = handler;  // store handler (nullptr = restore default)
}

// -----------------------------------------------------------------------------
// Send a complete inner frame
// - Caller provides an already-built frame (verb/flags/seq/tlv_len + body)
// - We hand it to PacketSerial, which SLIP-encodes and pushes it to Serial
// -----------------------------------------------------------------------------
void protocol_send(const uint8_t* frame, size_t len) {
    g_ps.send(frame, len);  // SLIP-encode + write out on Serial
}

// -----------------------------------------------------------------------------
// Convenience: send a simple MSG text frame
// - Wraps a C-string into a ViaText "MSG" packet
// - Avoids having to hand-build the header for quick demos/status
// -----------------------------------------------------------------------------
void node_protocol_send_text(const char* s) {
    if (!s) return;                     // Guard against null pointers
    size_t n = strnlen(s, 255);         // Clamp length to 255 bytes max
    uint8_t b[4 + 255];                 // Header (4) + payload (up to 255)
    size_t  i = 0;

    // ---- Frame header ----
    b[i++] = Verb::MSG;                 // verb = MSG
    b[i++] = 0;                         // flags = 0
    b[i++] = 0;                         // seq = 0 (unused for unsolicited MSG)
    b[i++] = static_cast<uint8_t>(n);   // TLV_LEN = length of payload

    // ---- Copy payload ----
    if (n) {
        memcpy(b + i, s, n);            // copy string into frame buffer
        i += n;                         // advance index
    }

    // ---- Transmit ----
    protocol_send(b, i);                // send complete frame via SLIP
}

