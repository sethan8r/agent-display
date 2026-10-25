// agent-display firmware: snapshots from the hub over USB serial (docs/protocol.md) -> the screens in app/ui.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "app/protocol.h"
#include "app/ui.h"
#include "board/cyd.h"

static cyd::LGFX lcd;
static constexpr uint32_t SILENCE_MS = 6000;  // same as the UI's "hub not connected"

static char line[32 * 1024 + 1];  // protocol limit for one snapshot
static size_t line_len = 0;
static bool overflow = false;
static uint32_t last_snap = 0;

static void hello() {
  Serial.printf("{\"t\":\"hello\",\"fw\":\"%s\",\"proto\":%d,\"board\":\"cyd2usb\"}\n", FW_VERSION, PROTO);
}

static void handle_line() {
  JsonDocument doc;
  if (deserializeJson(doc, line, line_len) || doc["t"] != "snap") return;  // noise at boot, other message types
  if (millis() - last_snap > SILENCE_MS) hello();  // the hub (re)appeared: tell it which firmware this is
  last_snap = millis();
  Snapshot s;
  protocol::parse(doc.as<JsonVariantConst>(), s);
  ui::show(s);
}

static void read_serial() {
  uint8_t buf[512];
  while (int n = Serial.available()) {  // in chunks: byte by byte was the bottleneck in the speed test
    n = Serial.read(buf, min(n, (int)sizeof buf));
    for (int i = 0; i < n; i++) {
      char c = buf[i];
      if (c == '\n') {
        if (!overflow && line_len) handle_line();
        line_len = 0;
        overflow = false;
      } else if (c != '\r') {
        if (line_len < sizeof line - 1) line[line_len++] = c;
        else overflow = true;  // too long for the protocol: drop the whole line
      }
    }
  }
}

void setup() {
  Serial.setRxBufferSize(16 * 1024);
  Serial.begin(921600);
  for (int pin : cyd::PIN_LED) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }  // RGB LED off
  ui::begin(lcd);
  hello();
}

void loop() {
  read_serial();
  ui::loop();
  delay(2);
}
