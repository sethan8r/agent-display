#pragma once
// The interface from design/index.html, drawn with LVGL. Feed it snapshots; it handles touch, settings and screens.
#include "app/model.h"
#include "board/cyd.h"

namespace ui {

void begin(cyd::LGFX& lcd);
void show(Snapshot s);  // a new snapshot (from the hub, or the demo); taken by value to move, not copy
void loop();                   // call from loop() as often as possible

}  // namespace ui
