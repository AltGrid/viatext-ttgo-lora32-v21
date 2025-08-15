/**
 * @file node_protocol.cpp
 * @brief Transport + protocol glue (SLIP over UART, frame boundaries, verbs/tags).
 *
 * Responsibilities:
 *  - Owns the PacketSerial/SLIP transport over Hardware Serial.
 *  - Converts raw UART bytes <-> complete inner frames.
 *  - Delivers frames to a handler (defaults to node_interface_on_packet()).
 *
 * Notes:
 *  - Inner frame format: [0]=verb, [1]=flags, [2]=seq, [3]=TLV_LEN, [4..] TLVs
 *  - This module does not interpret TLVs or update device state; all verbs/tags
 *    including GET_PARAM / SET_PARAM / GET_ALL are handled upstream.
 */

#include "node_protocol.hpp"
#include "node_interface.hpp"   // default callback: node_interface_on_packet()

#include <Arduino.h>
#include <PacketSerial.h>
#include <cstring>

// Global SLIP transport
static SLIPPacketSerial g_ps;

// Current handler (optional). If null, use node_interface_on_packet().
static void (*g_handler)(const uint8_t* frame, size_t len) = nullptr;

// PacketSerial callback: forward to current handler or default
static void on_slip_packet(const uint8_t* buffer, size_t size) {
    if (g_handler) {
        g_handler(buffer, size);
    } else {
        node_interface_on_packet(buffer, size);
    }
}

void node_protocol_begin(unsigned long baud) {
    Serial.begin(baud);
    g_ps.setStream(&Serial);
    g_ps.setPacketHandler(&on_slip_packet);
}

void node_protocol_update() {
    g_ps.update();
}

void node_protocol_set_handler(void (*handler)(const uint8_t* frame, size_t len)) {
    g_handler = handler;  // nullptr restores default to node_interface_on_packet()
}

void protocol_send(const uint8_t* frame, size_t len) {
    g_ps.send(frame, len);  // SLIP-encode + write to Serial
}

void node_protocol_send_text(const char* s) {
    if (!s) return;
    size_t n = strnlen(s, 255);
    uint8_t b[4 + 255];
    size_t  i = 0;

    b[i++] = Verb::MSG;
    b[i++] = 0;          // flags
    b[i++] = 0;          // seq (unused for unsolicited MSG)
    b[i++] = static_cast<uint8_t>(n); // TLV_LEN here is raw payload length for MSG

    if (n) {
        memcpy(b + i, s, n);
        i += n;
    }

    protocol_send(b, i);
}
