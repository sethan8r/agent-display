#include "app/settings.h"

#include <Preferences.h>

namespace settings {

static constexpr const char* NS = "agentdisp";

static void put_if_changed(const char* key, const void* data, size_t len) {
  Preferences p;
  p.begin(NS, false);
  uint8_t old[32];
  if (p.getBytesLength(key) != len || p.getBytes(key, old, len) != len || memcmp(old, data, len) != 0)
    p.putBytes(key, data, len);
  p.end();
}

static bool get(const char* key, void* data, size_t len) {
  Preferences p;
  p.begin(NS, true);
  bool ok = p.getBytesLength(key) == len && p.getBytes(key, data, len) == len;
  p.end();
  return ok;
}

void load(Settings& s) {
  Settings stored;
  if (get("cfg", &stored, sizeof stored) && stored.version == Settings().version) s = stored;
  // out of range after a corrupted blob or a format change without a version bump: back to defaults
  Settings d;
  if (s.theme > 1) s.theme = d.theme;
  if (s.lang > 1) s.lang = d.lang;
  if (s.size > 2) s.size = d.size;
  if (s.layout > 2) s.layout = d.layout;
  if (s.bright < 10 || s.bright > 100) s.bright = d.bright;
}

void save(const Settings& s) { put_if_changed("cfg", &s, sizeof s); }

void load_limits(Limit& h5, Limit& d7) {
  int32_t v[6];
  if (!get("lim", v, sizeof v)) return;
  if (v[0]) h5 = Limit::of(v[1], v[2]);
  if (v[3]) d7 = Limit::of(v[4], v[5]);
}

void save_limits(const Limit& h5, const Limit& d7) {
  int32_t v[6] = {h5.has, h5.pct, (int32_t)h5.reset, d7.has, d7.pct, (int32_t)d7.reset};
  put_if_changed("lim", v, sizeof v);
}

}  // namespace settings
