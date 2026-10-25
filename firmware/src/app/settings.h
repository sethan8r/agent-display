#pragma once
// Board settings and the last known limits, kept in NVS. Written only when they change (flash wear).
#include <stdint.h>

#include "app/model.h"

struct Settings {  // 24 bytes, no padding: stored as one blob
  uint16_t cal[8] = {};  // touch calibration from LovyanGFX
  uint8_t version = 1;
  uint8_t theme = 0;     // 0 dark, 1 light
  uint8_t lang = 0;      // 0 ru, 1 en
  uint8_t size = 1;      // 0: 13 px, 1: 14 px, 2: 16 px
  uint8_t layout = 0;    // 0 large, 1 compact, 2 grid
  uint8_t bright = 100;  // %
  uint8_t calibrated = 0;
  uint8_t reserved = 0;
};
static_assert(sizeof(Settings) == 24, "Settings must have no padding");

namespace settings {
void load(Settings& s);
void save(const Settings& s);
void load_limits(Limit& h5, Limit& d7);
void save_limits(const Limit& h5, const Limit& d7);
}  // namespace settings
