#pragma once
// docs/protocol.md on the board side: a JSON snapshot from the hub into a Snapshot for the UI.
#include <ArduinoJson.h>

#include "app/model.h"

namespace protocol {

void parse(JsonVariantConst d, Snapshot& s);  // a snapshot with v != PROTO keeps only v, now and tz

}  // namespace protocol
