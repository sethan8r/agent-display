// Screens from design/index.html. The whole screen is rebuilt when something visible changes (at most ~1/s);
// blinking and scrolling names run from timers on top of it, keyed to the clock so a rebuild doesn't restart them.
#include "app/ui.h"

#include <esp_random.h>
#include <lvgl.h>
#include <time.h>

#include <vector>

#include "app/i18n.h"
#include "app/icons.h"
#include "app/settings.h"
#include "app/theme.h"

extern const uint8_t inter400[] asm("_binary_fonts_Inter_400_ttf_start");
extern const uint8_t inter400_end[] asm("_binary_fonts_Inter_400_ttf_end");
extern const uint8_t inter600[] asm("_binary_fonts_Inter_600_ttf_start");
extern const uint8_t inter600_end[] asm("_binary_fonts_Inter_600_ttf_end");
extern const uint8_t fa_solid[] asm("_binary_fonts_fa_solid_ttf_start");
extern const uint8_t fa_solid_end[] asm("_binary_fonts_fa_solid_ttf_end");

namespace ui {
namespace {

constexpr int W = cyd::W, H = cyd::H;
constexpr int HDR = 30, FTR = 36, PAGER = 34, LIST = H - HDR - FTR;  // list area: 254, or 220 with the pager
constexpr uint32_t LOST_MS = 6000, DETAILS_MS = 30000, PHRASE_MS = 120000, TOAST_MS = 2500;
constexpr uint32_t INK_ON_WAIT = 0x1A1200;  // text on yellow badges
constexpr int ICON = 16, ICON_SMALL = 12, SLOT = 20;  // icons are 16 px whatever the text size; 12 inside small badges

enum Screen { MAIN, DETAILS, SETTINGS, CALIB };
enum SettingId { SET_THEME, SET_LANG, SET_SIZE, SET_LAYOUT };

cyd::LGFX* lcd;
lv_indev_t* touch;
Settings cfg;
Limit nvs_h5, nvs_d7;
Snapshot snap;
bool have_snap = false;
uint32_t rx_ms = 0;

Screen screen = MAIN;
bool calib_from_settings = false, calib_now = false;
int page = 0;
String sel, shown_key;
uint32_t touch_ms = 0, details_ms = 0, toast_ms = 0, phrase_ms = 0;
int phrase = -1;
bool dirty = true, was_empty = false, empty_before = false, blink_on = true, fonts_stale = false;

struct Blink {
  lv_obj_t* o;
  lv_color_t bg_on, bg_off;
  lv_opa_t border_on, border_off;
};
std::vector<Blink> blinks;
std::vector<lv_obj_t*> marquees;
std::vector<String> item_ids;  // session id per clickable item of the current screen
lv_obj_t* timer_line = nullptr;
lv_obj_t* bright_label = nullptr;

const Theme& th() { return cfg.theme ? LIGHT : DARK; }
const Size& sz() { return SIZES[cfg.size]; }
const Strings& T() { return cfg.lang ? en() : ru(); }
bool lost() { return !have_snap || millis() - rx_ms > LOST_MS; }
bool ok() { return !lost() && snap.v == PROTO; }
lv_color_t hex(uint32_t c) { return lv_color_hex(c); }
lv_color_t mix(uint32_t a, uint32_t b, lv_opa_t k) { return lv_color_mix(hex(a), hex(b), k); }

/* ---------- fonts: TinyTTF renders Inter and the Font Awesome icons at any size ---------- */
// every font keeps its own glyph cache, so: small caches, few sizes, and drop them all when the text size changes
lv_font_t* icon_fonts[41];
lv_font_t* text_fonts[2][41];

const lv_font_t* icons(int px) {
  lv_font_t*& f = icon_fonts[px];
  if (!f) f = lv_tiny_ttf_create_data_ex(fa_solid, fa_solid_end - fa_solid, px, LV_FONT_KERNING_NONE, 16);
  return f;
}

const lv_font_t* font(int px, bool semi = false) {
  lv_font_t*& f = text_fonts[semi][px];
  if (!f) f = semi ? lv_tiny_ttf_create_data_ex(inter600, inter600_end - inter600, px, LV_FONT_KERNING_NONE, 32)
                   : lv_tiny_ttf_create_data_ex(inter400, inter400_end - inter400, px, LV_FONT_KERNING_NONE, 32);
  return f;
}

void free_fonts() {  // only when no label uses them
  for (lv_font_t*& f : icon_fonts)
    if (f) { lv_tiny_ttf_destroy(f); f = nullptr; }
  for (auto& w : text_fonts)
    for (lv_font_t*& f : w)
      if (f) { lv_tiny_ttf_destroy(f); f = nullptr; }
}

int line_h(int px, bool semi = false) { return lv_font_get_line_height(font(px, semi)); }

uint32_t codepoint(const char* s) {  // icons are 3-byte UTF-8 (U+E000..U+FFFF)
  const uint8_t* u = (const uint8_t*)s;
  return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
}

// vertical center of a glyph's ink inside a label of that font
int glyph_center(const lv_font_t* f, uint32_t cp) {
  lv_font_glyph_dsc_t g;
  if (!lv_font_get_glyph_dsc(f, &g, cp, 0)) return lv_font_get_line_height(f) / 2;
  return lv_font_get_line_height(f) - f->base_line - (g.box_h + g.ofs_y) + g.box_h / 2;
}

// how far an icon label must sit below a text label's top so the icon centers on the capital letters
int icon_dy(const char* g, int px, int text_px, bool semi) {
  return glyph_center(font(text_px, semi), '0') - glyph_center(icons(px), codepoint(g));
}

/* ---------- builders ---------- */
lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}

lv_obj_t* fill(lv_obj_t* o, lv_color_t c, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_bg_color(o, c, 0);
  return o;
}

lv_obj_t* round(lv_obj_t* o, int r) {
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_clip_corner(o, true, 0);
  return o;
}

lv_obj_t* frame(lv_obj_t* o, uint32_t color, lv_opa_t opa, int width = 2) {
  lv_obj_set_style_border_width(o, width, 0);
  lv_obj_set_style_border_color(o, hex(color), 0);
  lv_obj_set_style_border_opa(o, opa, 0);
  return o;
}

lv_obj_t* text(lv_obj_t* parent, const char* s, int px, bool semi, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font(px, semi), 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}
lv_obj_t* text(lv_obj_t* parent, const String& s, int px, bool semi, uint32_t color) {
  return text(parent, s.c_str(), px, semi, color);
}

int width_of(lv_obj_t* o) {
  lv_obj_update_layout(o);
  return lv_obj_get_width(o);
}

lv_obj_t* icon(lv_obj_t* parent, const char* g, int px, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, g);
  lv_obj_set_style_text_font(l, icons(px), 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}

// icon centered in a SLOT-wide column at x, on the text line whose label top is y
lv_obj_t* icon_at(lv_obj_t* p, const char* g, int px, uint32_t color, int x, int y, int text_px, bool semi = false) {
  lv_obj_t* l = icon(p, g, px, color);
  lv_obj_set_pos(l, x + (SLOT - width_of(l)) / 2, y + icon_dy(g, px, text_px, semi));
  return l;
}

// icon in a flex row: boxes get centered, so shift the icon onto the text's capital letters
lv_obj_t* icon_in_row(lv_obj_t* row, const char* g, int px, uint32_t color, int text_px, bool semi = false) {
  lv_obj_t* l = icon(row, g, px, color);
  int dy = (lv_font_get_line_height(icons(px)) - line_h(text_px, semi)) / 2 + icon_dy(g, px, text_px, semi);
  lv_obj_set_style_translate_y(l, dy, 0);
  return l;
}

lv_obj_t* hrow(lv_obj_t* parent, int w, int gap = 5) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(r, w, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, gap, 0);
  return r;
}

lv_obj_t* one_line(lv_obj_t* l, int w, int px, bool semi = false) {  // cut with "…"
  lv_obj_set_size(l, w, line_h(px, semi));
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  return l;
}

lv_obj_t* wrapped(lv_obj_t* l, int w, int px, bool semi, int max_lines) {
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  int max_h = max_lines * line_h(px, semi);
  if (width_of(l) && lv_obj_get_height(l) > max_h) {
    lv_obj_set_height(l, max_h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  }
  return l;
}

// one line that scrolls when too long, moved by marquee_tick()
lv_obj_t* marquee(lv_obj_t* parent, int x, int y, int w, const String& s, int px, bool semi, uint32_t color) {
  lv_obj_t* clip = box(parent, x, y, w, line_h(px, semi));
  marquees.push_back(text(clip, s, px, semi, color));
  return clip;
}

lv_obj_t* badge(lv_obj_t* parent, const char* g, const String& s, lv_color_t bg, uint32_t ink) {
  lv_obj_t* b = hrow(parent, LV_SIZE_CONTENT, 3);
  lv_obj_set_height(b, 16);
  lv_obj_set_style_pad_hor(b, 6, 0);
  round(fill(b, bg), 8);
  if (g) icon_in_row(b, g, ICON_SMALL, ink, 11, true);
  text(b, s, 11, true, ink);
  lv_obj_update_layout(b);
  return b;
}

void blink_apply(const Blink& b) {
  lv_obj_set_style_bg_color(b.o, blink_on ? b.bg_on : b.bg_off, 0);
  if (b.border_on != b.border_off) lv_obj_set_style_border_opa(b.o, blink_on ? b.border_on : b.border_off, 0);
}

void blink(lv_obj_t* o, lv_color_t on, lv_color_t off, lv_opa_t border_on = 0, lv_opa_t border_off = 0) {
  blinks.push_back({o, on, off, border_on, border_off});
  blink_apply(blinks.back());
}

uint32_t level_color(int pct, uint32_t normal) { return pct >= 90 ? th().error : pct >= 70 ? th().wait : normal; }

void bar(lv_obj_t* p, int x, int y, int w, int h, int pct, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t* b = round(fill(box(p, x, y, w, h), hex(th().panel2)), 2);
  if (pct > 0) round(fill(box(b, 0, 0, max(2, w * min(pct, 100) / 100), h), hex(color), opa), 2);
}

void ctx_bar(lv_obj_t* p, int x, int y, int w, int h, int ctx) {
  bar(p, x, y, w, h, ctx, level_color(ctx, th().fg), ctx >= 70 ? LV_OPA_COVER : 140);
}

void on(lv_obj_t* o, lv_event_cb_t cb, intptr_t data = 0) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void*)data);
}
intptr_t arg(lv_event_t* e) { return (intptr_t)lv_event_get_user_data(e); }

/* ---------- formatting: same rules as the mockup ---------- */
int st_index(const String& s) {
  static const char* keys[] = {"idle", "work", "subs", "wait", "done", "error"};
  for (int i = 0; i < 6; i++)
    if (s == keys[i]) return i;
  return 0;
}
int wf_index(const String& w) { return w == "perm" ? 0 : w == "question" ? 1 : w == "plan" ? 2 : -1; }
bool is_wait(const Session& s) { return s.status == "wait"; }
bool dimmed(const Session& s) { int i = st_index(s.status); return i == 0 || i == 4; }

uint32_t st_color(const Session& s) {
  const Theme& t = th();
  const uint32_t by[] = {t.idle, t.work, t.subs, t.wait, t.done, t.error};
  return by[st_index(s.status)];
}

const char* st_icon(const Session& s) {
  int i = st_index(s.status);
  if (i == 3) { int w = wf_index(s.wait_for); return w == 1 ? icon::question : w == 2 ? icon::plan : icon::warn; }
  static const char* by[] = {icon::dot, icon::run, icon::agent, icon::warn, icon::check, icon::error};
  return by[i];
}

const char* tool_icon(const String& n) {
  if (n == "edit") return icon::edit;
  if (n == "run") return icon::run;
  if (n == "read") return icon::read;
  if (n == "find") return icon::find;
  if (n == "web") return icon::web;
  if (n == "agent") return icon::agent;
  if (n == "ask") return icon::warn;
  return icon::other;
}

const char* st_label(const Session& s, bool shrt) {
  int w = wf_index(s.wait_for);
  if (is_wait(s) && w >= 0) return T().wf[w][shrt];
  return T().st[st_index(s.status)][shrt];
}

String dur(int32_t sec, bool shrt) {
  const char* const* u = T().u + (shrt ? 4 : 0);
  sec = max(sec, (int32_t)0);
  if (sec < 60) return String(sec) + u[0];
  int m = sec / 60;
  if (m < 60) return String(m) + u[1];
  int h = m / 60, mm = m % 60;
  if (h < 24) return String(h) + u[2] + (mm ? String(shrt ? "" : " ") + mm + u[1] : String());
  return String(h / 24) + u[3];
}

String time_text(const Session& s, bool shrt) {
  int i = st_index(s.status);
  if (i == 0) return "";
  String d = dur((int32_t)(snap.now - s.since), shrt);
  if ((i == 4 || i == 5) && !shrt) {
    char b[48];
    snprintf(b, sizeof b, T().ago, d.c_str());
    return b;
  }
  return d;
}

String hhmm(uint32_t t) {
  time_t x = (time_t)t + snap.tz;
  struct tm tm;
  gmtime_r(&x, &tm);
  char b[8];
  snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
  return b;
}

String reset_label(uint32_t r) {
  if (r - snap.now < 86400) return hhmm(r);
  time_t x = (time_t)r + snap.tz;
  struct tm tm;
  gmtime_r(&x, &tm);
  return String(T().wd[tm.tm_wday]) + " " + hhmm(r);
}

String ktok(uint32_t n) {
  char b[12];
  if (n >= 1000000) snprintf(b, sizeof b, "%.1fM", n / 1e6);
  else snprintf(b, sizeof b, "%luk", (unsigned long)((n + 500) / 1000));
  return b;
}

void line2(const Session& s, const char*& ic, String& txt) {
  switch (st_index(s.status)) {
    case 1: ic = s.icon.length() ? tool_icon(s.icon) : icon::run; txt = s.action.length() ? s.action : T().thinking; break;
    case 3: ic = st_icon(s); txt = s.action.length() ? s.action : String(st_label(s, false)); break;
    case 2: ic = icon::agent; txt = String(T().wait_subs) + ": " + (int)(s.subagents.size() + s.subagents_more); break;
    default: ic = st_icon(s); txt = st_label(s, false);
  }
}

/* ---------- events ---------- */
void go(Screen s) {
  screen = s;
  dirty = true;
}
void cb_open(lv_event_t* e) {
  sel = item_ids[arg(e)];
  details_ms = millis();
  go(DETAILS);
}
void cb_back(lv_event_t*) { go(MAIN); }
void cb_settings(lv_event_t*) { go(SETTINGS); }
void cb_up(lv_event_t*) { page--; dirty = true; }
void cb_down(lv_event_t*) { page++; dirty = true; }
void cb_recal(lv_event_t*) { calib_from_settings = true; go(CALIB); }
void cb_calib_go(lv_event_t*) { calib_now = true; }

void apply_brightness() { lcd->setBrightness(cfg.bright * 255 / 100); }

void cb_setting(lv_event_t* e) {
  int v = arg(e), id = v >> 8, val = v & 0xFF;
  if (id == SET_THEME) cfg.theme = val;
  else if (id == SET_LANG) cfg.lang = val;
  else if (id == SET_SIZE) { cfg.size = val; page = 0; fonts_stale = true; }
  else if (id == SET_LAYOUT) { cfg.layout = val; page = 0; }
  settings::save(cfg);
  dirty = true;
}

void cb_bright(lv_event_t* e) {
  lv_obj_t* s = (lv_obj_t*)lv_event_get_target(e);
  cfg.bright = lv_slider_get_value(s);
  apply_brightness();
  if (bright_label) lv_label_set_text_fmt(bright_label, "%d%%", cfg.bright);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings::save(cfg);
}

/* ---------- parts ---------- */
lv_obj_t* bar_top(lv_obj_t* scr) {
  lv_obj_t* h = box(scr, 0, 0, W, HDR);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  frame(h, th().line, LV_OPA_COVER, 1);
  return h;
}

void header(lv_obj_t* scr) {
  const Theme& t = th();
  lv_obj_t* h = bar_top(scr);
  lv_obj_t* prev = text(h, T().agents, 13, true, t.fg);
  lv_obj_align(prev, LV_ALIGN_LEFT_MID, 8, 0);
  if (ok()) {
    int n = snap.sessions.size() + snap.sessions_more, waits = 0;
    for (auto& s : snap.sessions) waits += is_wait(s);
    if (n) {
      lv_obj_t* c = badge(h, nullptr, String(n), hex(t.panel2), t.mut);
      lv_obj_align_to(c, prev, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
      prev = c;
    }
    if (waits) {
      lv_obj_t* c = badge(h, icon::warn, String(waits), hex(t.wait), INK_ON_WAIT);
      lv_obj_align_to(c, prev, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
      blink(c, hex(t.wait), mix(t.wait, t.bg, 90));
    }
  }
  lv_obj_align(text(h, lost() ? "--:--" : hhmm(snap.now).c_str(), 12, false, t.mut), LV_ALIGN_RIGHT_MID, -40, 0);
  lv_obj_t* gear = box(h, W - 36, 0, 36, HDR);
  lv_obj_center(icon(gear, icon::gear, ICON, t.mut));
  on(gear, cb_settings);
  if (ok() && snap.fw_update.length()) round(fill(box(gear, 24, 6, 6, 6), hex(t.done)), 3);
}

void back_header(lv_obj_t* scr, const char* title) {
  lv_obj_t* h = bar_top(scr);
  lv_obj_t* b = box(h, 0, 0, 80, HDR);
  lv_obj_t* r = hrow(b, LV_SIZE_CONTENT, 4);
  icon_in_row(r, icon::back, ICON, th().work, 13, true);
  text(r, T().back, 13, true, th().work);
  lv_obj_align(r, LV_ALIGN_LEFT_MID, 6, 0);
  on(b, cb_back);
  if (title) lv_obj_align(text(h, title, 13, true, th().fg), LV_ALIGN_LEFT_MID, 84, 0);
}

void limit_col(lv_obj_t* f, int x, int w, const Limit& l, const char* name, bool stale) {
  const Theme& t = th();
  uint32_t mut = stale ? t.dim : t.mut, fg = stale ? t.dim : t.fg;
  bool reset = !stale && l.has && snap.now >= l.reset;
  lv_obj_t* n = text(f, name, 11, true, mut);
  lv_obj_set_pos(n, x, 6);
  lv_obj_t* v = text(f, !l.has ? String("—") : reset ? String(T().reset) : String(l.pct) + "%", reset ? 11 : 12, true,
                     reset ? t.done : fg);
  lv_obj_set_pos(v, x + width_of(n) + 4, reset ? 6 : 5);
  if (l.has && !reset && !stale) {
    lv_obj_t* r = text(f, reset_label(l.reset), 11, false, mut);
    int rx = x + w - width_of(r);
    lv_obj_set_pos(r, rx, 6);
    lv_obj_t* ri = icon(f, icon::refresh, ICON_SMALL, mut);
    lv_obj_set_pos(ri, rx - width_of(ri) - 3, 6 + icon_dy(icon::refresh, ICON_SMALL, 11, false));
  }
  int pct = l.has && !reset ? l.pct : 0;
  bar(f, x, 25, w, 4, pct, stale ? t.dim : level_color(pct, t.work));
}

void footer(lv_obj_t* scr) {
  const Theme& t = th();
  lv_obj_t* f = box(scr, 0, H - FTR, W, FTR);
  lv_obj_set_style_border_side(f, LV_BORDER_SIDE_TOP, 0);
  frame(f, t.line, LV_OPA_COVER, 1);
  bool fresh = ok() && (snap.h5.has || snap.d7.has);
  const Limit& a = fresh ? snap.h5 : nvs_h5;
  const Limit& b = fresh ? snap.d7 : nvs_d7;
  if (!a.has && !b.has) {
    lv_obj_center(text(f, T().lim_no, 11, false, t.dim));
    return;
  }
  constexpr int CW = (W - 16 - 10) / 2;
  limit_col(f, 8, CW, a, T().h5, !ok());
  limit_col(f, 8 + CW + 10, CW, b, T().wk, !ok());
}

// a clickable session item: background tinted with the status color, blinking when it waits for you
lv_obj_t* tile(lv_obj_t* scr, const Session& s, int x, int y, int w, int h, int radius, bool framed) {
  const Theme& t = th();
  uint32_t c = st_color(s);
  lv_obj_t* o = round(box(scr, x, y, w, h), radius);
  lv_color_t calm = mix(c, t.panel, t.fill);
  fill(o, calm);
  if (framed) frame(o, c, dimmed(s) ? t.frame_dim : t.frame);
  if (is_wait(s)) blink(o, mix(t.wait, t.panel, t.tint), calm, framed ? t.frame : 0, framed ? 56 : 0);
  on(o, cb_open, item_ids.size());
  item_ids.push_back(s.id);
  return o;
}

void card(lv_obj_t* scr, const Session& s, int y) {
  const Theme& t = th();
  const Size& z = sz();
  uint32_t c = st_color(s);
  int w = W - 8, inner = w - 4 - 14;  // 2 px frame each side, 7 px padding
  lv_obj_t* o = tile(scr, s, 4, y, w, z.card, 8, true);
  lv_obj_t* tm = text(o, time_text(s, false), z.fs, false, t.mut);
  int tw = width_of(tm);
  lv_obj_set_pos(tm, 7 + inner - tw, 3 + (line_h(z.fn, true) - line_h(z.fs)) / 2 + 1);
  marquee(o, 7, 3, inner - tw - 6, s.name, z.fn, true, t.fg);
  int r2 = 3 + line_h(z.fn, true) + 1;
  const char* ic;
  String txt;
  line2(s, ic, txt);
  icon_at(o, ic, ICON, c, 5, r2, z.ft, is_wait(s));
  lv_obj_t* pc = text(o, s.ctx < 0 ? String() : String(s.ctx) + "%", z.fs, false, t.mut);
  int pw = width_of(pc);
  lv_obj_set_pos(pc, 7 + inner - pw, r2 + 1);
  uint32_t ink = is_wait(s) ? t.wait_ink : s.status == "error" ? c : t.mut;
  lv_obj_set_pos(one_line(text(o, txt, z.ft, is_wait(s), ink), inner - SLOT - 2 - pw - 6, z.ft, is_wait(s)), 5 + SLOT + 4, r2);
  ctx_bar(o, 7, r2 + line_h(z.ft) + 4, inner, 3, s.ctx);
}

void row(lv_obj_t* scr, const Session& s, int y) {
  const Theme& t = th();
  const Size& z = sz();
  uint32_t c = st_color(s);
  int w = W - 8;
  lv_obj_t* o = tile(scr, s, 4, y, w, z.row, 5, false);
  fill(box(o, 0, 0, 4, z.row), hex(c), dimmed(s) ? 128 : LV_OPA_COVER);
  lv_obj_t* vb = round(fill(box(o, w - 9, (z.row - 16) / 2, 4, 16), hex(t.panel2)), 2);
  if (s.ctx > 0) {
    int hh = max(2, 16 * min(s.ctx, 100) / 100);
    round(fill(box(vb, 0, 16 - hh, 4, hh), hex(level_color(s.ctx, t.fg)), s.ctx >= 70 ? LV_OPA_COVER : 140), 2);
  }
  lv_obj_t* tm = text(o, time_text(s, true), z.fs, false, t.mut);
  int tw = width_of(tm);
  lv_obj_align(tm, LV_ALIGN_RIGHT_MID, -15, 0);
  int nx = 7 + SLOT + 4, ny = (z.row - line_h(z.fn - 1, true)) / 2;
  icon_at(o, st_icon(s), ICON, c, 7, ny, z.fn - 1, true);
  marquee(o, nx, ny, w - nx - 15 - tw - 6, s.name, z.fn - 1, true, t.fg);
}

void cell(lv_obj_t* scr, const Session& s, int x, int y) {
  const Theme& t = th();
  const Size& z = sz();
  uint32_t c = st_color(s);
  int w = (W - 8 - 4) / 2, inner = w - 4 - 12;
  lv_obj_t* o = tile(scr, s, x, y, w, z.cell, 8, true);
  marquee(o, 6, 3, inner, s.name, z.fn - 1, true, t.fg);
  int r2 = 3 + line_h(z.fn - 1, true) + 1;
  icon_at(o, st_icon(s), ICON, c, 4, r2, z.fs, is_wait(s));
  String tt = time_text(s, true);
  String lbl = String(st_label(s, true)) + (tt.length() ? String(" · ") + tt : String());
  lv_obj_set_pos(one_line(text(o, lbl, z.fs, is_wait(s), is_wait(s) ? t.wait_ink : t.mut), inner - SLOT, z.fs, is_wait(s)),
                 4 + SLOT + 2, r2);
  ctx_bar(o, 6, r2 + line_h(z.fs) + 4, inner, 3, s.ctx);
}

void page_button(lv_obj_t* scr, int x, int y, const char* ic, int waits, const char* word, bool enabled, lv_event_cb_t cb) {
  const Theme& t = th();
  lv_obj_t* b = round(fill(box(scr, x, y, 84, 30), hex(t.panel2)), 7);
  if (enabled) on(b, cb);
  else lv_obj_set_style_opa(b, 77, 0);
  lv_obj_t* il = icon(b, ic, ICON, t.fg);
  if (!waits) { lv_obj_center(il); return; }
  lv_obj_align(il, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_t* bd = badge(b, icon::warn, String(waits) + " " + word, hex(t.wait), INK_ON_WAIT);
  lv_obj_align(bd, LV_ALIGN_RIGHT_MID, -4, 0);
  blink(bd, hex(t.wait), mix(t.wait, t.panel2, 90));
}

lv_obj_t* center_column(lv_obj_t* scr) {
  lv_obj_t* c = box(scr, 0, HDR, W, LIST);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 8, 0);
  return c;
}

lv_obj_t* centered(lv_obj_t* l, int w, int px, bool semi) {
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return wrapped(l, w, px, semi, 4);
}

/* ---------- screens ---------- */
void build_empty(lv_obj_t* scr) {
  const Theme& t = th();
  // a new random phrase each time the screen becomes empty and every 2 minutes, never the same twice in a row
  if (!empty_before || millis() - phrase_ms > PHRASE_MS) {
    phrase = phrase < 0 ? random(16) : (phrase + 1 + random(15)) % 16;
    phrase_ms = millis();
  }
  was_empty = true;
  lv_obj_t* c = center_column(scr);
  icon(c, icon::coffee, 30, t.mut);
  centered(text(c, T().phrases[phrase], 15, true, t.fg), 200, 15, true);
  text(c, T().no_agents, 12, false, t.mut);
}

void build_main(lv_obj_t* scr) {
  header(scr);
  const auto& ss = snap.sessions;
  if (ss.empty()) {
    build_empty(scr);
    footer(scr);
    return;
  }
  const Size& z = sz();
  auto cap = [&](int a) {
    return cfg.layout == 0 ? a / (z.card + 4) : cfg.layout == 1 ? (a - 2) / (z.row + 2) : a / (z.cell + 4) * 2;
  };
  int n = ss.size(), cap_all = cap(LIST - 4), cap_paged = cap(LIST - PAGER - 4);
  bool paged = n > cap_all;
  int per = paged ? cap_paged : cap_all, pages = (n + per - 1) / per;
  page = constrain(page, 0, pages - 1);
  int from = page * per, to = min(n, from + per), y = HDR + 4;
  for (int i = from; i < to; i++) {
    if (cfg.layout == 0) { card(scr, ss[i], y); y += z.card + 4; }
    else if (cfg.layout == 1) { row(scr, ss[i], y); y += z.row + 2; }
    else { int k = i - from; cell(scr, ss[i], 4 + (k % 2) * ((W - 12) / 2 + 4), HDR + 4 + (k / 2) * (z.cell + 4)); }
  }
  if (paged) {
    int above = 0, below = 0;  // sessions waiting for you on other pages
    for (int i = 0; i < n; i++) {
      if (!is_wait(ss[i])) continue;
      if (i < from) above++;
      else if (i >= to) below++;
    }
    int py = H - FTR - PAGER + 2;
    page_button(scr, 4, py, icon::up, above, T().above, page > 0, cb_up);
    page_button(scr, W - 88, py, icon::down, below, T().below, page < pages - 1, cb_down);
    String pn = String(page + 1) + " / " + pages + (snap.sessions_more ? String(" · +") + snap.sessions_more : String());
    lv_obj_align(text(scr, pn, 11, false, th().mut), LV_ALIGN_BOTTOM_MID, 0, -FTR - PAGER / 2 + 6);
  }
  footer(scr);
}

void build_details(lv_obj_t* scr, const Session& s) {
  const Theme& t = th();
  const Size& z = sz();
  uint32_t c = st_color(s);
  constexpr int CW = W - 20;
  back_header(scr, nullptr);
  lv_obj_align(badge(scr, nullptr, st_label(s, true), mix(c, t.bg, 38), c), LV_ALIGN_TOP_RIGHT, -8, 7);
  // auto-return countdown; created at its current length so a data update doesn't make it jump
  uint32_t idle = millis() - max(details_ms, touch_ms);
  timer_line = fill(box(scr, 0, HDR, W - W * min(idle, DETAILS_MS) / DETAILS_MS, 2), hex(t.line));

  lv_obj_t* col = box(scr, 10, HDR + 7, CW, LIST - 7);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 5, 0);

  wrapped(text(col, s.name, z.fn, true, t.fg), CW, z.fn, true, 2);

  lv_obj_t* st = hrow(col, CW);
  icon_in_row(st, st_icon(s), ICON, c, z.ft, true);
  text(st, st_label(s, false), z.ft, true, c);
  String tt = time_text(s, false);
  if (tt.length()) text(st, String("· ") + tt, z.ft, false, t.mut);

  lv_obj_t* cx = box(col, 0, 0, CW, line_h(z.fs));
  text(cx, T().ctx, z.fs, false, t.mut);
  String cv = s.tokens_max ? ktok(s.tokens) + " / " + ktok(s.tokens_max) + " · " : String();
  cv += s.ctx < 0 ? String(T().nodata) : String(s.ctx) + "%";
  lv_obj_align(text(cx, cv, z.fs, true, t.fg), LV_ALIGN_TOP_RIGHT, 0, 0);
  ctx_bar(col, 0, 0, CW, 5, s.ctx);

  if (s.status == "work" || is_wait(s)) {
    const char* ic;
    String txt;
    line2(s, ic, txt);
    lv_obj_t* a = round(fill(box(col, 0, 0, CW, LV_SIZE_CONTENT), hex(t.panel)), 6);
    lv_obj_set_style_pad_ver(a, 4, 0);
    lv_obj_set_style_pad_hor(a, 7, 0);
    if (is_wait(s)) blink(a, mix(t.wait, t.panel, t.tint), hex(t.panel));
    icon_at(a, ic, ICON, c, -2, 0, z.ft);
    lv_obj_set_pos(wrapped(text(a, txt, z.ft, false, t.fg), CW - 14 - SLOT - 2, z.ft, false, 2), SLOT + 2, 0);
  }

  if (!s.subagents.empty()) {
    text(col, String(T().subagents) + " · " + (int)(s.subagents.size() + s.subagents_more), z.fs, true, t.mut);
    for (auto& a : s.subagents) {
      lv_obj_t* r = hrow(col, CW);
      lv_obj_t* ai = icon_in_row(r, icon::agent, ICON, t.subs, z.ft);
      lv_obj_t* ty = text(r, a.type, z.ft, true, t.fg);
      one_line(text(r, a.task, z.ft, false, t.mut), CW - width_of(ai) - width_of(ty) - 10, z.ft);
    }
    if (s.subagents_more) text(col, String("+") + s.subagents_more + " " + T().more, z.fs, false, t.mut);
  }

  lv_obj_t* d = hrow(col, CW);
  lv_obj_t* fi = icon_in_row(d, icon::folder, ICON, t.mut, z.ft);
  one_line(text(d, s.dir, z.ft, false, t.mut), CW - width_of(fi) - 5, z.ft);
  lv_obj_t* m = hrow(col, CW);
  if (s.model.length()) {
    icon_in_row(m, icon::cpu, ICON, t.mut, z.ft);
    text(m, s.model + " ·", z.ft, false, t.mut);
  }
  icon_in_row(m, icon::clock, ICON, t.mut, z.ft);
  text(m, String(T().opened) + " " + dur((int32_t)(snap.now - s.started), false), z.ft, false, t.mut);
  footer(scr);
}

void section(lv_obj_t* p, int x, int y, const char* label) { lv_obj_set_pos(text(p, label, 11, true, th().mut), x, y); }

void seg(lv_obj_t* p, int x, int y, int w, std::initializer_list<const char*> labels, int cur, int setting,
         const int* px = nullptr, bool icon_labels = false) {
  const Theme& t = th();
  lv_obj_t* s = round(fill(box(p, x, y, w, 34), hex(t.panel2)), 8);
  int n = labels.size(), bw = (w - 4) / n, i = 0;
  for (const char* l : labels) {
    lv_obj_t* b = round(box(s, 2 + i * bw, 2, bw, 30), 6);
    if (i == cur) frame(fill(b, hex(t.panel)), t.line, LV_OPA_COVER, 1);
    uint32_t ink = i == cur ? t.fg : t.mut;
    lv_obj_center(icon_labels ? icon(b, l, ICON, ink) : text(b, l, px ? px[i] : 12, i == cur, ink));
    on(b, cb_setting, setting << 8 | i);
    i++;
  }
}

void build_settings(lv_obj_t* scr) {
  const Theme& t = th();
  back_header(scr, T().settings);
  int x = 10, w = W - 20, half = (w - 8) / 2, y = HDR + 6;
  section(scr, x, y, T().theme);
  section(scr, x + half + 8, y, T().lang);
  seg(scr, x, y + 19, half, {icon::moon, icon::sun}, cfg.theme, SET_THEME, nullptr, true);
  seg(scr, x + half + 8, y + 19, half, {"RU", "EN"}, cfg.lang, SET_LANG);
  y += 59;
  section(scr, x, y, T().size);
  static const int size_px[] = {11, 13, 16};
  seg(scr, x, y + 19, w, {"A", "A", "A"}, cfg.size, SET_SIZE, size_px);
  y += 57;
  lv_obj_set_pos(icon(scr, icon::sun, ICON, t.mut), x, y + 7);
  lv_obj_t* sl = lv_slider_create(scr);
  lv_obj_set_pos(sl, x + 26, y + 11);
  lv_obj_set_size(sl, w - 26 - 46, 8);
  lv_slider_set_range(sl, 10, 100);
  lv_slider_set_value(sl, cfg.bright, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, hex(t.panel2), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, hex(t.work), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, hex(t.work), LV_PART_KNOB);
  lv_obj_add_event_cb(sl, cb_bright, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, cb_bright, LV_EVENT_RELEASED, nullptr);
  bright_label = text(scr, String(cfg.bright) + "%", 11, false, t.mut);
  lv_obj_set_pos(bright_label, x + w - 34, y + 8);
  y += 34;
  section(scr, x, y, T().layout);
  seg(scr, x, y + 19, w, {T().large, T().compact, T().grid}, cfg.layout, SET_LAYOUT);
  y += 59;
  lv_obj_t* b = round(fill(box(scr, x, y + 2, w, 36), hex(t.panel2)), 8);
  lv_obj_t* rr = hrow(b, LV_SIZE_CONTENT, 6);
  icon_in_row(rr, icon::target, ICON, t.fg, 12, true);
  text(rr, T().recal, 12, true, t.fg);
  lv_obj_center(rr);
  on(b, cb_recal);

  lv_obj_t* v = box(scr, 0, H - 30, W, 30);
  lv_obj_set_flex_flow(v, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(v, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (ok() && snap.fw_update.length())
  {
    lv_obj_t* ur = hrow(v, LV_SIZE_CONTENT, 4);
    icon_in_row(ur, icon::download, ICON_SMALL, t.done, 11, true);
    text(ur, String(T().fw_avail) + " " + snap.fw_update, 11, true, t.done);
  }
  text(v, String(T().fw) + " " + FW_VERSION + " · " + T().proto + " v" + PROTO, 11, false, t.dim);

  if (toast_ms && millis() - toast_ms < TOAST_MS) {
    lv_obj_t* toast = round(fill(box(scr, 20, H - 76, W - 40, 28), hex(t.done)), 8);
    lv_obj_center(text(toast, T().calib_saved, 12, true, 0x04210F));
  }
}

void build_calib(lv_obj_t* scr) {
  lv_obj_t* c = center_column(scr);
  lv_obj_set_size(c, W, H);
  lv_obj_set_pos(c, 0, 0);
  icon(c, icon::target, 30, 0xFF5D5D);
  text(c, T().calib_t, 15, true, 0xFFFFFF);
  centered(text(c, T().calib_s, 12, false, 0x9AA3B2), 200, 12, false);
  text(c, T().calib_go, 13, true, DARK.work);
  on(scr, cb_calib_go);
}

void build_lost(lv_obj_t* scr) {
  const Theme& t = th();
  header(scr);
  lv_obj_t* c = center_column(scr);
  icon(c, icon::plug, 30, t.mut);
  text(c, T().hub_lost, 15, true, t.fg);
  centered(text(c, T().hub_lost_sub, 12, false, t.mut), 200, 12, false);
  if (nvs_h5.has || nvs_d7.has) centered(text(c, T().lim_stale, 11, false, t.dim), 200, 11, false);
  footer(scr);
}

void build_mismatch(lv_obj_t* scr) {
  const Theme& t = th();
  bool newer = snap.v > PROTO;
  header(scr);
  lv_obj_t* c = center_column(scr);
  icon(c, icon::warn, 30, t.wait);
  centered(text(c, newer ? T().plug_newer : T().fw_newer, 15, true, t.fg), 210, 15, true);
  text(c, String(newer ? T().upd_fw : T().upd_plug) + " " + T().in_cc, 12, false, t.mut);
  lv_obj_t* code = round(fill(box(c, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT), hex(t.panel2)), 6);
  lv_obj_set_style_pad_all(code, 6, 0);
  text(code, newer ? "/agent-display:update-firmware" : "/plugin update agent-display", 11, true, t.fg);
  footer(scr);
}

const Session* selected() {
  for (auto& s : snap.sessions)
    if (s.id == sel) return &s;
  return nullptr;
}

void rebuild() {
  dirty = false;
  blinks.clear();
  marquees.clear();
  item_ids.clear();
  timer_line = bright_label = nullptr;
  empty_before = was_empty;
  was_empty = false;
  lv_obj_t* old = lv_screen_active();
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_screen_load(scr);
  if (old) lv_obj_delete(old);
  if (fonts_stale) { free_fonts(); fonts_stale = false; }
  fill(scr, hex(screen == CALIB ? 0x000000 : th().bg));
  if (screen == CALIB) build_calib(scr);
  else if (screen == SETTINGS) build_settings(scr);
  else if (lost()) build_lost(scr);
  else if (snap.v != PROTO) build_mismatch(scr);
  else if (screen == DETAILS && selected()) build_details(scr, *selected());
  else { screen = MAIN; build_main(scr); }
}

// everything visible that can change without a touch; the screen is rebuilt when this changes
String screen_key() {
  String k;
  k.reserve(1024);
  k += lost() ? 'L' : 'C';
  if (!have_snap) return k;
  k += snap.v; k += hhmm(snap.now); k += snap.fw_update; k += snap.sessions_more;
  for (const Limit* l : {&snap.h5, &snap.d7}) { k += l->has; k += l->pct; k += snap.now >= l->reset; }
  for (auto& s : snap.sessions) {
    k += s.id; k += s.name; k += s.status; k += s.wait_for; k += s.icon; k += s.action; k += s.ctx; k += s.tokens;
    k += time_text(s, false); k += (snap.now - s.started) / 60; k += s.model; k += s.dir; k += s.subagents_more;
    for (auto& a : s.subagents) { k += a.type; k += a.task; }
  }
  return k;
}

void calibrate() {
  calib_now = false;
  int32_t x, y;
  while (lcd->getTouch(&x, &y)) delay(10);  // let go of the "tap to start" first
  lcd->fillScreen(TFT_BLACK);
  lcd->calibrateTouch(cfg.cal, TFT_WHITE, TFT_BLACK, 20);
  cfg.calibrated = 1;
  settings::save(cfg);
  toast_ms = calib_from_settings ? millis() : 0;
  go(calib_from_settings ? SETTINGS : MAIN);
  if (lv_screen_active()) lv_obj_invalidate(lv_screen_active());
}

/* ---------- LVGL glue and timers ---------- */
uint16_t buf1[W * 32], buf2[W * 32];

void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  lcd->pushImage(a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), (const lgfx::rgb565_t*)px);
  lv_display_flush_ready(d);
}

void touch_cb(lv_indev_t*, lv_indev_data_t* d) {
  int32_t x, y;
  bool pressed = lcd->getTouch(&x, &y);
  if (pressed) {
    d->point.x = x;
    d->point.y = y;
    touch_ms = millis();
  }
  d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void blink_tick(lv_timer_t*) {
  blink_on = !blink_on;
  for (auto& b : blinks) blink_apply(b);
}

// long names: stand 3 s at the start, move to the end at 25 px/s, stand 1 s, jump back
void marquee_tick(lv_timer_t*) {
  uint32_t t = (uint64_t)millis() * 25 / 1000;
  for (lv_obj_t* l : marquees) {
    int32_t over = lv_obj_get_width(l) - lv_obj_get_width(lv_obj_get_parent(l));
    if (over <= 0) continue;
    int32_t x = -LV_CLAMP(0, (int32_t)(t % (over + 100)) - 75, over);
    if (lv_obj_get_x(l) != x) lv_obj_set_x(l, x);
  }
}

void watch_tick(lv_timer_t*) {
  uint32_t now = millis();
  if (screen == MAIN || screen == DETAILS) {
    String k = screen_key();
    if (k != shown_key) { shown_key = k; dirty = true; }
  }
  if (screen == MAIN && was_empty && now - phrase_ms > PHRASE_MS) dirty = true;
  if (screen == DETAILS) {
    uint32_t idle = now - max(details_ms, touch_ms);
    if (idle > DETAILS_MS) go(MAIN);
    else if (timer_line) lv_obj_set_width(timer_line, W - W * idle / DETAILS_MS);
  }
  if (screen == SETTINGS && toast_ms && now - toast_ms > TOAST_MS) { toast_ms = 0; dirty = true; }
}

}  // namespace

void begin(cyd::LGFX& l) {
  lcd = &l;
  settings::load(cfg);
  settings::load_limits(nvs_h5, nvs_d7);
  randomSeed(esp_random());
  lcd->init();
  lcd->setRotation(0);
  apply_brightness();
  if (cfg.calibrated) lcd->setTouchCalibrate(cfg.cal);
  else screen = CALIB;

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t* disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, sizeof buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
  touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_cb);
  lv_timer_create(blink_tick, 500, nullptr);
  lv_timer_create(marquee_tick, 40, nullptr);
  lv_timer_create(watch_tick, 200, nullptr);
}

void show(const Snapshot& s) {
  snap = s;
  have_snap = true;
  rx_ms = millis();
  auto same = [](const Limit& a, const Limit& b) { return a.has == b.has && a.pct == b.pct && a.reset == b.reset; };
  if (s.v == PROTO && (s.h5.has || s.d7.has) && !(same(s.h5, nvs_h5) && same(s.d7, nvs_d7))) {
    nvs_h5 = s.h5;
    nvs_d7 = s.d7;
    settings::save_limits(nvs_h5, nvs_d7);
  }
}

void loop() {
  if (calib_now) calibrate();
  // never rebuild under the finger: the pressed object would vanish and the tap would be lost
  if (dirty && lv_indev_get_state(touch) != LV_INDEV_STATE_PRESSED) rebuild();
  lv_timer_handler();
}

}  // namespace ui
