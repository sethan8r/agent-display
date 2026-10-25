// Icon test firmware: the same mini main screen with four icon sets, to pick one on the real panel.
// Icons are separate labels in their own font, centered on the capital letters of the text line.
// Tap anywhere: next screen. Serial: "next", "prev", "screen N".
#include <Arduino.h>
#include <lvgl.h>

#include "app/theme.h"
#include "board/cyd.h"
#include "icon_sets.h"

#define EMBED(sym, file)                                       \
  extern const uint8_t sym[] asm("_binary_fonts_" file "_start"); \
  extern const uint8_t sym##_end[] asm("_binary_fonts_" file "_end");
EMBED(inter400, "Inter_400_ttf")
EMBED(inter600, "Inter_600_ttf")
EMBED(lucide, "lucide_ttf")
EMBED(ph_bold, "test_ph_bold_ttf")
EMBED(ph_fill, "test_ph_fill_ttf")
EMBED(fa_solid, "test_fa_solid_ttf")

using ui::Theme;
static cyd::LGFX lcd;
static constexpr int W = cyd::W, H = cyd::H, SETS = sizeof ICON_SETS / sizeof ICON_SETS[0];
static const uint8_t* const SET_TTF[SETS][2] = {{lucide, lucide_end}, {ph_bold, ph_bold_end}, {ph_fill, ph_fill_end}, {fa_solid, fa_solid_end}};
enum { I_EDIT, I_RUN, I_READ, I_FIND, I_WEB, I_AGENT, I_WARN, I_QUESTION, I_PLAN, I_CHECK, I_ERROR, I_DOT, I_OTHER,
       I_GEAR, I_UP, I_DOWN, I_BACK, I_PLUG, I_FOLDER, I_CPU, I_CLOCK, I_COFFEE, I_REFRESH, I_DOWNLOAD, I_SUN, I_MOON, I_TARGET };

static int screen = 0, pending = 0;
static const int SCREENS = 1 + SETS * 3 + 1;

/* ---------- fonts ---------- */
static lv_font_t* text_fonts[2][41];
static lv_font_t* icon_fonts[SETS][41];
static const lv_font_t* font(int px, bool semi = false) {
  lv_font_t*& f = text_fonts[semi][px];
  if (!f) f = semi ? lv_tiny_ttf_create_data_ex(inter600, inter600_end - inter600, px, LV_FONT_KERNING_NONE, 32)
                   : lv_tiny_ttf_create_data_ex(inter400, inter400_end - inter400, px, LV_FONT_KERNING_NONE, 32);
  return f;
}
static const lv_font_t* icons(int set, int px) {
  lv_font_t*& f = icon_fonts[set][px];
  if (!f) f = lv_tiny_ttf_create_data_ex(SET_TTF[set][0], SET_TTF[set][1] - SET_TTF[set][0], px, LV_FONT_KERNING_NONE, 16);
  return f;
}

static uint32_t codepoint(const char* s) {  // icons are 3-byte UTF-8 (U+E000..U+FFFF)
  const uint8_t* u = (const uint8_t*)s;
  return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
}

// vertical center of a glyph inside a label of that font
static int glyph_center(const lv_font_t* f, uint32_t cp) {
  lv_font_glyph_dsc_t g;
  if (!lv_font_get_glyph_dsc(f, &g, cp, 0)) return lv_font_get_line_height(f) / 2;
  int top = lv_font_get_line_height(f) - f->base_line - (g.box_h + g.ofs_y);
  return top + g.box_h / 2;
}

/* ---------- builders ---------- */
static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }
static lv_obj_t* box(lv_obj_t* p, int x, int y, int w, int h) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}
static lv_obj_t* fill(lv_obj_t* o, uint32_t c, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_bg_color(o, hex(c), 0);
  return o;
}
static lv_obj_t* round(lv_obj_t* o, int r) { lv_obj_set_style_radius(o, r, 0); return o; }
static lv_obj_t* label(lv_obj_t* p, const char* s, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}
static int width_of(lv_obj_t* o) { lv_obj_update_layout(o); return lv_obj_get_width(o); }

// text at (x, y) with an icon in front of it; returns the x after the text
static int icon_text(lv_obj_t* p, int set, int ic, int icon_px, uint32_t icon_color, const char* txt, int px, bool semi,
                     uint32_t color, int x, int y) {
  const lv_font_t* tf = font(px, semi);
  const lv_font_t* f = icons(set, icon_px);
  const char* g = ICON_SETS[set].glyph[ic];
  lv_obj_t* i = label(p, g, f, icon_color);
  lv_obj_set_pos(i, x, y + glyph_center(tf, '0') - glyph_center(f, codepoint(g)));
  x += width_of(i) + 4;
  if (!txt) return x;
  lv_obj_t* t = label(p, txt, tf, color);
  lv_obj_set_pos(t, x, y);
  return x + width_of(t);
}

/* ---------- screens ---------- */
static void caption(lv_obj_t* scr, const String& s, uint32_t color, int y) {
  lv_obj_set_pos(label(scr, s.c_str(), font(11), color), 8, y);
}

static void intro(lv_obj_t* scr) {
  const Theme& t = ui::DARK;
  fill(scr, t.bg);
  lv_obj_t* c = box(scr, 12, 20, W - 24, H - 40);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 8, 0);
  label(c, "Тест значков", font(18, true), t.fg);
  lv_obj_t* d = label(c, "4 набора: Lucide (сейчас), Phosphor Bold, Phosphor Fill, Font Awesome. По каждому — все значки, "
                         "главный экран в тёмной и в светлой теме. В конце — размеры.\n\nТап — следующий экран.",
                      font(13), t.fg);
  lv_obj_set_width(d, W - 24);
  for (int s = 0; s < SETS; s++) {
    lv_obj_t* r = box(c, 0, 0, W - 24, 24);
    icon_text(r, s, I_GEAR, 18, t.fg, ICON_SETS[s].title, 13, true, t.fg, 0, 3);
  }
}

static void all_icons(lv_obj_t* scr, int set) {
  const Theme& t = ui::DARK;
  fill(scr, t.bg);
  caption(scr, String(screen + 1) + "/" + SCREENS + " · " + ICON_SETS[set].title + " · все значки", t.mut, 4);
  for (int i = 0; i < ICON_COUNT; i++) {
    int col = i % 4, row = i / 4, x = 4 + col * 58, y = 22 + row * 42;
    lv_obj_t* cell = box(scr, x, y, 56, 40);
    lv_obj_t* g = label(cell, ICON_SETS[set].glyph[i], icons(set, 20), t.fg);
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* n = label(cell, ICON_NAMES[i], font(10), t.mut);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, 0);
  }
}

static void card(lv_obj_t* scr, const Theme& t, int set, int y, uint32_t c, bool dim, bool wait, const char* name,
                 const char* tm, int ic, const char* act, int ctx) {
  lv_obj_t* o = round(fill(box(scr, 4, y, W - 8, 50), lv_color_to_u32(lv_color_mix(hex(wait ? t.wait : c), hex(t.panel),
                                                                                    wait ? t.tint : t.fill)) & 0xFFFFFF), 8);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, hex(c), 0);
  lv_obj_set_style_border_opa(o, dim ? t.frame_dim : t.frame, 0);
  lv_obj_set_pos(label(o, name, font(14, true), t.fg), 7, 3);
  lv_obj_t* tl = label(o, tm, font(11), t.mut);
  lv_obj_set_pos(tl, W - 8 - 4 - 7 - width_of(tl), 6);
  icon_text(o, set, ic, 15, c, act, 12, wait, wait ? t.wait_ink : t.mut, 7, 22);
  char p[8];
  snprintf(p, sizeof p, "%d%%", ctx);
  lv_obj_t* pl = label(o, p, font(11), t.mut);
  lv_obj_set_pos(pl, W - 8 - 4 - 7 - width_of(pl), 23);
  lv_obj_t* bar = round(fill(box(o, 7, 41, W - 26, 3), t.panel2), 2);
  round(fill(box(bar, 0, 0, (W - 26) * ctx / 100, 3), ctx >= 90 ? t.error : t.fg, ctx >= 90 ? LV_OPA_COVER : 140), 2);
}

static lv_obj_t* chip(lv_obj_t* p, int set, int ic, const char* txt, uint32_t bg, uint32_t ink, int x, int y, int w) {
  lv_obj_t* c = round(fill(box(p, x, y, w, 16), bg), 8);
  icon_text(c, set, ic, 12, ink, txt, 11, true, ink, 5, 1);
  return c;
}

static void main_screen(lv_obj_t* scr, int set, bool light) {
  const Theme& t = light ? ui::LIGHT : ui::DARK;
  fill(scr, t.bg);
  lv_obj_t* h = box(scr, 0, 0, W, 30);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, hex(t.line), 0);
  lv_obj_set_pos(label(h, "Агенты", font(13, true), t.fg), 8, 7);
  lv_obj_t* cnt = round(fill(box(h, 66, 7, 20, 16), t.panel2), 8);
  lv_obj_center(label(cnt, "6", font(11, true), t.mut));
  chip(h, set, I_WARN, "2", t.wait, 0x1A1200, 90, 7, 34);
  lv_obj_set_pos(label(h, "16:20", font(12), t.mut), W - 76, 8);
  lv_obj_t* gear = label(h, ICON_SETS[set].glyph[I_GEAR], icons(set, 18), t.mut);
  lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -9, 0);

  card(scr, t, set, 34, t.work, false, false, "agent-display", "4 мин", I_EDIT, "Edit firmware/src/ui.cpp", 42);
  card(scr, t, set, 88, t.wait, false, true, "Рефакторинг хаба", "1 мин", I_WARN, "Bash: git push origin main", 67);
  card(scr, t, set, 142, t.done, true, false, "Миграция БД", "12 мин назад", I_CHECK, "Готов", 91);

  // compact rows with the other status icons
  struct { uint32_t c; int ic; const char* name; } rows[] = {
    {t.subs, I_AGENT, "shop-backend"}, {t.wait, I_QUESTION, "Telegram-бот"}, {t.error, I_ERROR, "api-gateway"}};
  for (int i = 0; i < 3; i++) {
    lv_obj_t* r = round(fill(box(scr, 4, 196 + i * 20, 115, 18),
                             lv_color_to_u32(lv_color_mix(hex(rows[i].c), hex(t.panel), t.fill)) & 0xFFFFFF), 4);
    icon_text(r, set, rows[i].ic, 13, rows[i].c, rows[i].name, 11, true, t.fg, 5, 2);
  }
  int tools[] = {I_RUN, I_READ, I_FIND, I_WEB, I_PLAN, I_DOT, I_OTHER, I_FOLDER, I_CPU, I_CLOCK};
  for (int i = 0; i < 10; i++) {
    lv_obj_t* g = label(scr, ICON_SETS[set].glyph[tools[i]], icons(set, 15), t.fg);
    lv_obj_set_pos(g, 126 + (i % 5) * 22, 197 + (i / 5) * 22);
  }

  for (int k = 0; k < 2; k++) {
    lv_obj_t* b = round(fill(box(scr, k ? W - 88 : 4, 252, 84, 30), t.panel2), 7);
    lv_obj_t* a = label(b, ICON_SETS[set].glyph[k ? I_DOWN : I_UP], icons(set, 16), t.fg);
    if (k) {
      lv_obj_align(a, LV_ALIGN_LEFT_MID, 6, 0);
      chip(b, set, I_WARN, "1 ниже", t.wait, 0x1A1200, 26, 7, 54);
    } else lv_obj_center(a);
  }
  caption(scr, String(screen + 1) + "/" + SCREENS, t.mut, 259);
  lv_obj_set_x(lv_obj_get_child(scr, -1), W / 2 - 14);

  lv_obj_t* f = box(scr, 0, H - 36, W, 36);
  lv_obj_set_style_border_side(f, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(f, 1, 0);
  lv_obj_set_style_border_color(f, hex(t.line), 0);
  lv_obj_set_pos(label(f, "5ч", font(11, true), t.mut), 8, 6);
  lv_obj_set_pos(label(f, "42%", font(12, true), t.fg), 26, 5);
  icon_text(f, set, I_REFRESH, 11, t.mut, "18:36", 11, false, t.mut, 66, 6);
  lv_obj_set_pos(label(f, "Нед", font(11, true), t.mut), 125, 6);
  lv_obj_set_pos(label(f, "87%", font(12, true), t.fg), 150, 5);
  icon_text(f, set, I_REFRESH, 11, t.mut, "ср 21:20", 11, false, t.mut, 186, 6);
  lv_obj_t* b1 = round(fill(box(f, 8, 25, 107, 4), t.panel2), 2);
  round(fill(box(b1, 0, 0, 45, 4), t.work), 2);
  lv_obj_t* b2 = round(fill(box(f, 125, 25, 107, 4), t.panel2), 2);
  round(fill(box(b2, 0, 0, 93, 4), t.wait), 2);
}

static void sizes(lv_obj_t* scr) {
  const Theme& t = ui::DARK;
  fill(scr, t.bg);
  caption(scr, String(screen + 1) + "/" + SCREENS + " · значок рядом с текстом 12 px: 12 / 14 / 16 px", t.mut, 4);
  for (int s = 0; s < SETS; s++) {
    int y = 26 + s * 72;
    lv_obj_set_pos(label(scr, ICON_SETS[s].title, font(12, true), t.fg), 8, y);
    for (int k = 0; k < 3; k++) {
      int px = 12 + k * 2;
      icon_text(scr, s, I_WARN, px, t.wait, "Bash: push", 12, false, t.fg, 8 + k * 78, y + 20);
      icon_text(scr, s, I_CHECK, px, t.done, "Готов", 12, false, t.fg, 8 + k * 78, y + 40);
    }
  }
}

static void show(int n) {
  screen = n;
  lv_obj_t* old = lv_screen_active();
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_screen_load(scr);
  if (old) lv_obj_delete(old);
  // every TinyTTF font keeps its own caches: ~40 of them run the heap out, so drop the ones nothing shows anymore
  for (auto& set : icon_fonts)
    for (lv_font_t*& f : set)
      if (f) { lv_tiny_ttf_destroy(f); f = nullptr; }
  lv_obj_add_event_cb(scr, [](lv_event_t*) { pending = (screen + 1) % SCREENS; }, LV_EVENT_CLICKED, nullptr);
  if (n == 0) intro(scr);
  else if (n == SCREENS - 1) sizes(scr);
  else {
    int set = (n - 1) / 3, kind = (n - 1) % 3;
    if (kind == 0) all_icons(scr, set);
    else main_screen(scr, set, kind == 2);
  }
  Serial.printf("screen %d heap %lu\n", n, (unsigned long)ESP.getFreeHeap());
}

/* ---------- LVGL glue ---------- */
static uint16_t buf1[W * 32], buf2[W * 32];
static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  lcd.pushImage(a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), (const lgfx::rgb565_t*)px);
  lv_display_flush_ready(d);
}
static void touch_cb(lv_indev_t*, lv_indev_data_t* d) {
  int32_t x, y;
  bool p = lcd.getTouch(&x, &y);
  if (p) { d->point.x = x; d->point.y = y; }
  d->state = p ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void setup() {
  Serial.begin(921600);
  for (int pin : cyd::PIN_LED) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }
  lcd.init();
  lcd.setBrightness(255);
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t* disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, sizeof buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_cb);
  Serial.println("hello icons");
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') { if (c != '\r' && line.length() < 40) line += c; continue; }
    if (line == "next") pending = (screen + 1) % SCREENS;
    else if (line == "prev") pending = (screen + SCREENS - 1) % SCREENS;
    else if (line.startsWith("screen ")) pending = constrain(line.substring(7).toInt(), 0, SCREENS - 1);
    line = "";
  }
  if (pending >= 0) { int n = pending; pending = -1; show(n); }
  lv_timer_handler();
  delay(2);
}
