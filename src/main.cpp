// src/main.cpp
// Minimal LoRa sender/receiver test for TTGO LoRa32 v2.1
// Uses Sandeep Mistry's LoRa library (https://github.com/sandeepmistry/arduino-LoRa)
// No Core, no Package — just direct radio send/receive.

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// Pin mapping for LilyGO LoRa32 v2.1 (SX1276)
static constexpr int PIN_LORA_SS   = 18;
static constexpr int PIN_LORA_RST  = 23;
static constexpr int PIN_LORA_DIO0 = 26;

// LoRa settings
static constexpr long LORA_FREQ = 915E6; // change for your region

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }

  Serial.println("\n[LoRa Test] Booting...");

  // Reset LoRa module
  pinMode(PIN_LORA_RST, OUTPUT);
  digitalWrite(PIN_LORA_RST, HIGH);
  delay(10);
  digitalWrite(PIN_LORA_RST, LOW);
  delay(10);
  digitalWrite(PIN_LORA_RST, HIGH);
  delay(10);

  // Start LoRa
  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("[fatal] LoRa init failed. Check wiring.");
    while (true) delay(1000);
  }
  Serial.print("[LoRa] Init OK @ ");
  Serial.print(LORA_FREQ / 1E6);
  Serial.println(" MHz");
}

void loop() {
  // Send a hello every 2 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 2000) {
    lastSend = millis();
    Serial.println("[LoRa] Sending 'hello world'");
    LoRa.beginPacket();
    LoRa.print("hello world");
    LoRa.endPacket();
  }

  // Check for incoming packets
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    Serial.print("[LoRa] Received packet: ");
    while (LoRa.available()) {
      Serial.print((char)LoRa.read());
    }
    Serial.println();
  }
}
