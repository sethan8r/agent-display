#pragma once
// Colors and sizes, same values as design/index.html (tuned on the real TN panel)
#include <lvgl.h>

namespace ui {

struct Theme {
  uint32_t bg, panel, panel2, line, fg, mut, dim;
  uint32_t work, done, wait, subs, idle, error, wait_ink;
  lv_opa_t frame, frame_dim, tint, fill;  // card frame, dimmed frame (done/idle), "waits for you" fill, status fill
};

inline const Theme DARK = {0x0D1015, 0x161A21, 0x1E232C, 0x262C37, 0xE9EDF3, 0x8D96A7, 0x566070,
                           0x2F86FF, 0x00F25C, 0xFFA600, 0xA366FF, 0x7C8594, 0xFF2A2A, 0xFFA600,
                           230, 140, 50, 38};
// light: saturated colors and opaque frames, the TN panel washes pale ones out on white
inline const Theme LIGHT = {0xF2F3F5, 0xFFFFFF, 0xE9ECF0, 0xCDD2DA, 0x14171C, 0x4A5260, 0xA0A7B2,
                            0x0057FF, 0x00B347, 0xFF8A00, 0x8B2CFF, 0x808894, 0xF01818, 0xA85A00,
                            255, 215, 85, 38};

// text size setting: name / text / small px and item heights
struct Size {
  int fn, ft, fs, card, row, cell;
};
inline const Size SIZES[3] = {{13, 11, 10, 48, 24, 48}, {14, 12, 11, 50, 26, 50}, {16, 13, 12, 56, 30, 56}};

}  // namespace ui
