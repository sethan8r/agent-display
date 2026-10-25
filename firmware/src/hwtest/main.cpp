// Hardware test firmware: identifies the display, checks colors / rotation / touch, shows font sizes and the
// card palette from design/, reads the light sensor and measures USB serial throughput.
// Every screen reacts to taps and to text commands over serial (see handle_command), so it can be driven remotely.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include "board/cyd.h"

extern const uint8_t inter400[] asm("_binary_fonts_Inter_400_ttf_start");
extern const uint8_t inter400_end[] asm("_binary_fonts_Inter_400_ttf_end");
extern const uint8_t inter600[] asm("_binary_fonts_Inter_600_ttf_start");
extern const uint8_t inter600_end[] asm("_binary_fonts_Inter_600_ttf_end");

static cyd::LGFX lcd;
static cyd::PanelOpt opt;
static uint16_t touch_cal[8];
static bool touch_calibrated = false;

enum Screen { S_INFO, S_COLORS, S_ROTATE, S_TOUCH, S_FONTS, S_CARDS, S_USB, S_COUNT };
static int pending = S_INFO;  // screen to build on the next loop(): never rebuild inside an LVGL event
static int current = -1;
static bool calib_requested = false;
static bool light_theme = false;
static lv_timer_t* screen_timer = nullptr;
static lv_obj_t* live = nullptr;  // label refreshed by screen_timer

static struct { uint32_t lines, bytes, errors, last_parse_us, t_first, t_last; } usb;

/* ---------- fonts: TinyTTF renders Inter at any size, cached per (weight, size) ---------- */
static lv_font_t* font_cache[2][41];
static const lv_font_t* font(int px, bool semi = false) {
  lv_font_t*& f = font_cache[semi][px];
  // small glyph cache: the default (128 glyphs per font) runs the heap out with ten fonts
  if (!f) f = semi ? lv_tiny_ttf_create_data_ex(inter600, inter600_end - inter600, px, LV_FONT_KERNING_NONE, 16)
                   : lv_tiny_ttf_create_data_ex(inter400, inter400_end - inter400, px, LV_FONT_KERNING_NONE, 16);
  return f;
}

/* ---------- palette from design/index.html ---------- */
// light theme: saturated colors and opaque frames, the TN panel washes pale ones out on white
struct Theme { uint32_t bg, panel, panel2, line, fg, mut, work, done, wait, subs, wait_ink; lv_opa_t frame, frame_dim, tint; };
static const Theme DARK = {0x0D1015, 0x161A21, 0x1E232C, 0x262C37, 0xE9EDF3, 0x8D96A7, 0x4C9AFF, 0x38D18A, 0xFFB020, 0xB385FF,
                           0xFFB020, 204, 115, 43};
static const Theme LIGHT = {0xF2F3F5, 0xFFFFFF, 0xE9ECF0, 0xCDD2DA, 0x14171C, 0x4A5260, 0x0057FF, 0x00B347, 0xFF8A00, 0x8B2CFF,
                            0xA85A00, 255, 215, 85};
static const Theme& th() { return light_theme ? LIGHT : DARK; }
static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

/* ---------- LVGL glue ---------- */
static uint16_t buf1[cyd::W * 32], buf2[cyd::W * 32];

static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  lcd.pushImage(a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), (const lgfx::rgb565_t*)px);
  lv_display_flush_ready(d);
}

static void touch_cb(lv_indev_t*, lv_indev_data_t* d) {
  int32_t x, y;
  bool pressed = lcd.getTouch(&x, &y);
  if (pressed) { d->point.x = x; d->point.y = y; }
  d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void apply_panel() {
  lcd.apply(opt);
  lcd.init();
  lcd.setRotation(opt.rotation);
  lcd.setBrightness(255);
  if (touch_calibrated) lcd.setTouchCalibrate(touch_cal);
  if (lv_screen_active()) lv_obj_invalidate(lv_screen_active());
}

/* ---------- small builders ---------- */
static lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t* fill(lv_obj_t* o, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_bg_color(o, hex(color), 0);
  return o;
}

static lv_obj_t* text(lv_obj_t* parent, const char* s, int px, bool semi, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font(px, semi), 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}

static lv_obj_t* wrap(lv_obj_t* l, int width = cyd::W - 16) {
  lv_obj_set_width(l, width);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  return l;
}

static lv_obj_t* dots(lv_obj_t* l, int width) {  // one line, cut with "…"
  lv_obj_set_width(l, width);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  return l;
}

// one line that scrolls when too long: stands 3 s at the start, moves to the end, stands 1 s, jumps back
static constexpr int MARQUEE_PX_S = 25, MARQUEE_START_MS = 3000, MARQUEE_END_MS = 1000;

static void marquee_exec(void* var, int32_t v) {
  lv_obj_t* l = (lv_obj_t*)var;
  int32_t over = lv_obj_get_width(l) - lv_obj_get_width(lv_obj_get_parent(l));
  lv_obj_set_x(l, -LV_CLAMP(0, v - MARQUEE_PX_S * MARQUEE_START_MS / 1000, over));
}

static lv_obj_t* marquee(lv_obj_t* parent, const char* s, int px, bool semi, uint32_t color, int width) {
  lv_obj_t* clip = box(parent);
  lv_obj_set_size(clip, width, lv_font_get_line_height(font(px, semi)));
  lv_obj_t* l = text(clip, s, px, semi, color);
  lv_obj_update_layout(l);
  int32_t over = lv_obj_get_width(l) - width;
  if (over > 0) {
    int32_t total = over + MARQUEE_PX_S * (MARQUEE_START_MS + MARQUEE_END_MS) / 1000;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, marquee_exec);
    lv_anim_set_values(&a, 0, total);
    lv_anim_set_duration(&a, total * 1000 / MARQUEE_PX_S);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }
  return clip;
}

static lv_obj_t* column(lv_obj_t* parent, int pad, int gap) {
  lv_obj_t* c = box(parent);
  lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(c, pad, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, gap, 0);
  return c;
}

static lv_obj_t* button(lv_obj_t* parent, const char* label, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 108, 40);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(text(b, label, 14, true, 0xFFFFFF));
  return b;
}

/* ---------- info ---------- */
static String bytes3(uint32_t v) {
  char b[12];
  snprintf(b, sizeof b, "%02X %02X %02X", (unsigned)(v & 0xFF), (unsigned)((v >> 8) & 0xFF), (unsigned)((v >> 16) & 0xFF));
  return b;
}

static String info_text() {
  uint32_t id04 = lcd.getPanel()->readCommand(0x04, 1, 3), idd3 = lcd.getPanel()->readCommand(0xD3, 1, 3);
  const char* guess = (id04 & 0xFFFFFF) == 0x528585 ? "похоже на ST7789"
                    : (idd3 & 0xFFFFFF) == 0x419300 ? "похоже на ILI9341" : "не опознан";
  char b[640];
  snprintf(b, sizeof b,
           "Дисплей: ID(04h) %s, ID(D3h) %s — %s\n"
           "В прошивке: %s, инверсия %s, %s, поворот %d\n"
           "Чип: %s rev %d, ядер %d, %lu МГц\n"
           "Flash: %lu МБ, PSRAM: %lu КБ\n"
           "MAC: %012llX",
           bytes3(id04).c_str(), bytes3(idd3).c_str(), guess, opt.ili9341 ? "ILI9341" : "ST7789",
           opt.invert ? "вкл" : "выкл", opt.bgr ? "BGR" : "RGB", opt.rotation, ESP.getChipModel(), ESP.getChipRevision(),
           ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz(), (unsigned long)(ESP.getFlashChipSize() >> 20),
           (unsigned long)(ESP.getPsramSize() >> 10), ESP.getEfuseMac());
  return b;
}

/* ---------- navigation ---------- */
static void next_screen() { pending = (current + 1) % S_COUNT; }

static void tap_zone(int zone) {  // 0 top, 1 middle, 2 bottom third
  switch (current) {
    case S_COLORS:
      if (zone == 0) { opt.invert = !opt.invert; apply_panel(); pending = S_COLORS; }
      else if (zone == 1) { opt.bgr = !opt.bgr; apply_panel(); pending = S_COLORS; }
      else next_screen();
      break;
    case S_ROTATE:
      if (zone == 0) { opt.rotation ^= 2; apply_panel(); pending = S_ROTATE; }
      else if (zone == 1) { opt.rotation ^= 4; apply_panel(); pending = S_ROTATE; }
      else next_screen();
      break;
    case S_TOUCH: calib_requested = true; break;
    case S_FONTS:
    case S_CARDS:
      if (zone < 2) { light_theme = !light_theme; pending = current; }
      else next_screen();
      break;
    case S_USB: pending = S_INFO; break;
    default: next_screen();
  }
}

static void on_click(lv_event_t*) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  tap_zone(p.y * 3 / cyd::H);
}

/* ---------- screens ---------- */
static void build_info(lv_obj_t* scr) {
  fill(scr, DARK.bg);
  lv_obj_t* c = column(scr, 8, 6);
  text(c, "agent-display · тест железа", 16, true, DARK.fg);
  wrap(text(c, info_text().c_str(), 12, false, DARK.fg));
  live = text(c, "", 12, false, DARK.wait);
  wrap(text(c, "Закрой датчик света пальцем — цифра LDR должна измениться.", 12, false, DARK.mut));
  text(c, "Тап — следующий тест (1/7)", 12, true, DARK.work);
  screen_timer = lv_timer_create([](lv_timer_t*) {
    lv_label_set_text_fmt(live, "LDR: %d   heap: %lu КБ   %lu с", analogRead(cyd::PIN_LDR),
                          (unsigned long)(ESP.getFreeHeap() >> 10), millis() / 1000);
  }, 250, nullptr);
}

static void build_colors(lv_obj_t* scr) {
  fill(scr, 0x000000);
  struct { uint32_t c; const char* name; uint32_t ink; } bars[] = {
    {0xFF0000, "Красный", 0xFFFFFF}, {0x00FF00, "Зелёный", 0x000000}, {0x0000FF, "Синий", 0xFFFFFF},
    {0xFFFFFF, "Белый", 0x000000}, {0x808080, "Серый", 0x000000}, {0x000000, "Чёрный", 0xFFFFFF}};
  char head[200];
  snprintf(head, sizeof head, "2/7 · инверсия %s · %s\nТап: верх — инверсия, середина — RGB/BGR, низ — дальше",
           opt.invert ? "вкл" : "выкл", opt.bgr ? "BGR" : "RGB");
  lv_obj_set_pos(wrap(text(scr, head, 12, false, 0xFFFFFF), cyd::W - 8), 4, 4);
  for (int i = 0; i < 6; i++) {
    lv_obj_t* b = fill(box(scr), bars[i].c);
    lv_obj_set_pos(b, 0, 44 + i * 46);
    lv_obj_set_size(b, cyd::W, 46);
    lv_obj_center(text(b, bars[i].name, 14, true, bars[i].ink));
  }
}

static void build_rotate(lv_obj_t* scr) {
  fill(scr, DARK.bg);
  lv_obj_align(text(scr, "↑ ВЕРХ", 24, true, DARK.wait), LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_align(text(scr, "USB здесь ↓", 16, true, DARK.wait), LV_ALIGN_BOTTOM_MID, 0, -8);
  const char* corner[] = {"1", "2", "3", "4"};
  const lv_align_t al[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_LEFT};
  for (int i = 0; i < 4; i++)
    lv_obj_align(text(scr, corner[i], 20, true, DARK.work), al[i], (i == 0 || i == 3) ? 6 : -6, i < 2 ? 4 : -4);
  char b[260];
  snprintf(b, sizeof b, "3/7 · поворот %d\n\nЦифры 1-2-3-4 идут по часовой стрелке от левого верхнего угла, текст не зеркальный, "
           "USB снизу.\n\nТап: верх — перевернуть, середина — отзеркалить, низ — дальше", opt.rotation);
  lv_obj_center(wrap(text(scr, b, 12, false, DARK.fg), cyd::W - 40));
}

static lv_obj_t* touch_label;
static lv_obj_t* touch_dots[100];
static int touch_dot_n;

static void on_draw(lv_event_t* e) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_obj_t*& d = touch_dots[touch_dot_n++ % 100];
  if (d) lv_obj_delete(d);
  d = fill(box((lv_obj_t*)lv_event_get_current_target(e)), DARK.wait);
  lv_obj_set_size(d, 6, 6);
  lv_obj_set_style_radius(d, 3, 0);
  lv_obj_set_pos(d, p.x - 3, p.y - 3);
  lv_label_set_text_fmt(touch_label, "x = %d, y = %d", (int)p.x, (int)p.y);
}

static void build_touch(lv_obj_t* scr) {
  fill(scr, DARK.bg);
  if (!touch_calibrated) {
    lv_obj_t* c = column(scr, 12, 10);
    text(c, "4/7 · Калибровка тача", 16, true, DARK.fg);
    wrap(text(c, "Тапни, и в углах по очереди появятся стрелки. Нажимай стилусом точно в кончик каждой стрелки.",
              14, false, DARK.fg), cyd::W - 24);
    return;
  }
  memset(touch_dots, 0, sizeof touch_dots);
  touch_dot_n = 0;
  lv_obj_remove_event_cb(scr, on_click);
  lv_obj_add_event_cb(scr, on_draw, LV_EVENT_PRESSING, nullptr);
  lv_obj_t* h = wrap(text(scr, "4/7 · Рисуй стилусом: точка должна быть прямо под ним, и у краёв тоже.",
                          12, false, DARK.fg), cyd::W - 16);
  lv_obj_set_pos(h, 8, 6);
  touch_label = text(scr, "", 12, true, DARK.wait);
  lv_obj_set_pos(touch_label, 8, 40);
  lv_obj_align(button(scr, "Заново", [](lv_event_t*) { touch_calibrated = false; calib_requested = true; }),
               LV_ALIGN_BOTTOM_LEFT, 8, -8);
  lv_obj_align(button(scr, "Дальше", [](lv_event_t*) { next_screen(); }), LV_ALIGN_BOTTOM_RIGHT, -8, -8);
}

static void build_fonts(lv_obj_t* scr) {
  const Theme& t = th();
  fill(scr, t.bg);
  lv_obj_t* c = column(scr, 8, 5);
  text(c, light_theme ? "5/7 · Шрифты (светлая)" : "5/7 · Шрифты (тёмная)", 12, false, t.mut);
  char b[120];
  for (int px : {10, 11, 12, 13, 14, 16}) {
    snprintf(b, sizeof b, "%d px: Рефакторинг хаба — 4 мин", px);
    dots(text(c, b, px, false, t.fg), cyd::W - 16);
  }
  for (int px : {12, 13, 14, 16}) {
    snprintf(b, sizeof b, "%d px: Миграция БД на Postgres", px);
    dots(text(c, b, px, true, t.fg), cyd::W - 16);
  }
  wrap(text(c, "Ёё «ёлка» № 5 … — ↑↓ ←→ 0123456789", 14, false, t.fg));
  text(c, "Тап: верх/середина — тема, низ — дальше", 12, false, t.mut);
}

static lv_obj_t* blink_card;
static bool blink_on;

static void card(lv_obj_t* scr, int y, uint32_t c, bool dim, bool wait, const char* name, const char* tm, const char* act, int ctx) {
  const Theme& t = th();
  lv_obj_t* o = fill(box(scr), wait ? lv_color_to_u32(lv_color_mix(hex(t.wait), hex(t.panel), t.tint)) & 0xFFFFFF : t.panel);
  lv_obj_set_pos(o, 4, y);
  lv_obj_set_size(o, cyd::W - 8, 50);
  lv_obj_set_style_radius(o, 8, 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, hex(c), 0);
  lv_obj_set_style_border_opa(o, dim ? t.frame_dim : t.frame, 0);
  lv_obj_set_pos(marquee(o, name, 14, true, t.fg, 140), 7, 3);
  lv_obj_align(text(o, tm, 11, false, t.mut), LV_ALIGN_TOP_RIGHT, -7, 5);
  lv_obj_t* a = dots(text(o, act, 12, false, wait ? t.wait_ink : t.mut), 170);
  lv_obj_set_height(a, lv_font_get_line_height(font(12)));
  lv_obj_set_pos(a, 7, 21);
  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", ctx);
  lv_obj_align(text(o, pct, 11, false, t.mut), LV_ALIGN_TOP_RIGHT, -7, 22);
  lv_obj_t* bar = fill(box(o), t.panel2);
  lv_obj_set_pos(bar, 7, 40);
  lv_obj_set_size(bar, cyd::W - 26, 3);
  lv_obj_t* fillbar = fill(box(bar), ctx >= 90 ? 0xFF5D5D : t.fg, ctx >= 90 ? LV_OPA_COVER : 140);
  lv_obj_set_size(fillbar, (cyd::W - 26) * ctx / 100, 3);
  if (wait) blink_card = o;
}

static void build_cards(lv_obj_t* scr) {
  const Theme& t = th();
  fill(scr, t.bg);
  lv_obj_t* hdr = box(scr);
  lv_obj_set_size(hdr, cyd::W, 30);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(hdr, 1, 0);
  lv_obj_set_style_border_color(hdr, hex(t.line), 0);
  lv_obj_align(text(hdr, "Агенты  3", 13, true, t.fg), LV_ALIGN_LEFT_MID, 8, 0);
  lv_obj_align(text(hdr, "16:20", 12, false, t.mut), LV_ALIGN_RIGHT_MID, -10, 0);
  card(scr, 34, t.work, false, false, "agent-display", "4 мин", "Edit firmware/src/ui.cpp", 42);
  card(scr, 88, t.wait, false, true, "Рефакторинг хаба", "1 мин", "Bash: git push origin main", 67);
  card(scr, 142, t.subs, false, false, "shop-backend", "6 мин", "Ждёт субагентов: 3", 23);
  card(scr, 196, t.done, true, false, "Миграция БД на Postgres", "12 мин назад", "Готов", 91);
  lv_obj_t* note = wrap(text(scr, light_theme ? "6/7 · светлая тема" : "6/7 · тёмная тема", 12, false, t.mut));
  lv_obj_set_pos(note, 8, 254);
  lv_obj_set_pos(text(scr, "Тап: верх/середина — тема, низ — дальше", 12, false, t.mut), 8, 276);
  lv_obj_t* ftr = box(scr);
  lv_obj_set_size(ftr, cyd::W, 20);
  lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_align(text(ftr, "5ч 42%   ↻18:36        Нед 87%   ↻ср 21:20", 11, false, t.mut), LV_ALIGN_LEFT_MID, 8, 0);
  blink_on = true;
  screen_timer = lv_timer_create([](lv_timer_t*) {
    const Theme& t = th();
    blink_on = !blink_on;
    lv_obj_set_style_border_opa(blink_card, blink_on ? t.frame : 56, 0);
    lv_obj_set_style_bg_color(blink_card, blink_on ? lv_color_mix(hex(t.wait), hex(t.panel), t.tint) : hex(t.panel), 0);
  }, 500, nullptr);
}

static void build_usb(lv_obj_t* scr) {
  fill(scr, DARK.bg);
  lv_obj_t* c = column(scr, 10, 8);
  text(c, "7/7 · Скорость USB", 16, true, DARK.fg);
  wrap(text(c, "Плата принимает снимки JSON, как от хаба, разбирает их и отвечает. Тест запускается с компа.",
            12, false, DARK.mut), cyd::W - 20);
  live = wrap(text(c, "", 14, false, DARK.fg), cyd::W - 20);
  text(c, "Тап — в начало", 12, false, DARK.work);
  screen_timer = lv_timer_create([](lv_timer_t*) {
    uint32_t ms = usb.t_last - usb.t_first;
    lv_label_set_text_fmt(live, "Строк: %lu\nБайт: %lu\nОшибок: %lu\nСкорость: %lu КБ/с\nРазбор последней: %lu мс",
                          (unsigned long)usb.lines, (unsigned long)usb.bytes, (unsigned long)usb.errors,
                          (unsigned long)(ms ? usb.bytes / ms : 0), (unsigned long)(usb.last_parse_us / 1000));
  }, 250, nullptr);
}

static void show(int n) {
  if (screen_timer) { lv_timer_delete(screen_timer); screen_timer = nullptr; }
  live = nullptr;
  lv_obj_t* old = lv_screen_active();
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, on_click, LV_EVENT_CLICKED, nullptr);
  current = n;
  switch (n) {
    case S_INFO: build_info(scr); break;
    case S_COLORS: build_colors(scr); break;
    case S_ROTATE: build_rotate(scr); break;
    case S_TOUCH: build_touch(scr); break;
    case S_FONTS: build_fonts(scr); break;
    case S_CARDS: build_cards(scr); break;
    case S_USB: build_usb(scr); break;
  }
  lv_screen_load(scr);
  if (old) lv_obj_delete(old);
  lv_refr_now(nullptr);  // render now, so the heap numbers include the glyphs of this screen
  Serial.printf("screen %d heap %lu min %lu\n", n, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
}

static void calibrate() {
  lcd.fillScreen(TFT_BLACK);
  lcd.calibrateTouch(touch_cal, TFT_YELLOW, TFT_BLACK, 20);
  touch_calibrated = true;
  Serial.print("touch_cal");
  for (uint16_t v : touch_cal) Serial.printf(" %u", v);
  Serial.println();
}

/* ---------- serial: JSON lines are a speed test, anything else is a command ---------- */
static char line[32 * 1024 + 1];
static size_t line_len;
static bool line_overflow;

static void handle_json(size_t len) {
  if (!usb.lines) usb.t_first = millis();
  uint32_t t0 = micros();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, (const char*)line, len);
  usb.last_parse_us = micros() - t0;
  usb.t_last = millis();
  usb.lines++;
  usb.bytes += len + 1;
  if (err) usb.errors++;
  Serial.printf("ack %lu %u %lu %s %u\n", (unsigned long)usb.lines, (unsigned)len, (unsigned long)usb.last_parse_us,
                err ? err.c_str() : "ok", (unsigned)doc["sessions"].size());
}

static void handle_command(String cmd) {
  cmd.trim();
  if (cmd == "next") next_screen();
  else if (cmd == "prev") pending = (current + S_COUNT - 1) % S_COUNT;
  else if (cmd.startsWith("screen ")) pending = constrain(cmd.substring(7).toInt(), 0, S_COUNT - 1);
  else if (cmd == "inv") { opt.invert = !opt.invert; apply_panel(); pending = current; }
  else if (cmd == "bgr") { opt.bgr = !opt.bgr; apply_panel(); pending = current; }
  else if (cmd == "panel ili") { opt.ili9341 = true; apply_panel(); pending = current; }
  else if (cmd == "panel st") { opt.ili9341 = false; apply_panel(); pending = current; }
  else if (cmd.startsWith("rot ")) { opt.rotation = cmd.substring(4).toInt() & 7; apply_panel(); pending = current; }
  else if (cmd == "theme") { light_theme = !light_theme; pending = current; }
  else if (cmd == "usbreset") { memset(&usb, 0, sizeof usb); }
  else if (cmd == "info") { Serial.println(info_text()); return; }
  else if (cmd == "ldr") {  // 8 s of light sensor readings, 4 per second
    for (int i = 0; i < 32; i++) {
      Serial.printf("ldr raw %d mv %lu\n", analogRead(cyd::PIN_LDR), (unsigned long)analogReadMilliVolts(cyd::PIN_LDR));
      delay(250);
    }
    return;
  }
  else { Serial.printf("err unknown command: %s\n", cmd.c_str()); return; }
  Serial.printf("ok %s\n", cmd.c_str());
}

static void serial_poll() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == '\n') {
      if (line_overflow) { usb.errors++; Serial.println("err line too long"); }
      else if (line_len && line[0] == '{') handle_json(line_len);
      else if (line_len) { line[line_len] = 0; handle_command(line); }
      line_len = 0;
      line_overflow = false;
    } else if (ch != '\r') {
      if (line_len < sizeof line - 1) line[line_len++] = (char)ch;
      else line_overflow = true;
    }
  }
}

void setup() {
  Serial.setRxBufferSize(16 * 1024);
  Serial.begin(921600);
  for (int pin : cyd::PIN_LED) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }  // RGB LED off
  analogSetPinAttenuation(cyd::PIN_LDR, ADC_0db);

  apply_panel();
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t* disp = lv_display_create(cyd::W, cyd::H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, sizeof buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_cb);

  Serial.println("hello hwtest");
  Serial.println(info_text());
}

void loop() {
  serial_poll();
  if (calib_requested) {
    calib_requested = false;
    calibrate();
    pending = S_TOUCH;
  }
  if (pending >= 0) {
    int n = pending;
    pending = -1;
    show(n);
  }
  lv_timer_handler();
  delay(2);
}
