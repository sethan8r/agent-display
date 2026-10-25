#pragma once
// ESP32-2432S028 "Cheap Yellow Display", two-USB version (CYD2USB).
// Display on HSPI, touch (XPT2046) on its own SPI, backlight GPIO21, light sensor GPIO34, RGB LED 4/16/17 (active low).
#include <LovyanGFX.hpp>

namespace cyd {

constexpr int PIN_LDR = 34;
constexpr int PIN_LED[3] = {4, 16, 17};
constexpr int W = 240, H = 320;  // portrait

struct PanelOpt {
  bool ili9341 = false;  // CYD2USB usually has ST7789; the original CYD has ILI9341
  bool invert = false;
  bool bgr = false;
  uint8_t rotation = 0;  // 0/2 portrait, +4 = mirrored
};

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7789 st7789_;
  lgfx::Panel_ILI9341 ili9341_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;

 public:
  LGFX() {
    auto b = bus_.config();
    b.spi_host = SPI2_HOST;  // HSPI
    b.spi_mode = 0;
    b.freq_write = 40000000;
    b.freq_read = 16000000;
    b.spi_3wire = false;
    b.use_lock = true;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = 14;
    b.pin_mosi = 13;
    b.pin_miso = 12;
    b.pin_dc = 2;
    bus_.config(b);

    auto l = light_.config();
    l.pin_bl = 21;
    l.invert = false;
    l.freq = 12000;
    l.pwm_channel = 7;
    light_.config(l);

    auto t = touch_.config();
    t.spi_host = SPI3_HOST;  // VSPI
    t.freq = 1000000;
    t.pin_sclk = 25;
    t.pin_mosi = 32;
    t.pin_miso = 39;
    t.pin_cs = 33;
    t.pin_int = 36;
    t.bus_shared = false;
    t.x_min = 300; t.x_max = 3900;  // rough defaults, replaced by calibration
    t.y_min = 200; t.y_max = 3800;
    touch_.config(t);

    for (lgfx::Panel_Device* p : {(lgfx::Panel_Device*)&st7789_, (lgfx::Panel_Device*)&ili9341_}) {
      p->setBus(&bus_);
      p->setLight(&light_);
      p->setTouch(&touch_);
    }
    apply({});
  }

  // pick the panel driver and color options; call init() afterwards
  void apply(const PanelOpt& o) {
    lgfx::Panel_Device* p = o.ili9341 ? (lgfx::Panel_Device*)&ili9341_ : (lgfx::Panel_Device*)&st7789_;
    auto c = p->config();
    c.pin_cs = 15;
    c.pin_rst = -1;
    c.pin_busy = -1;
    c.panel_width = W;
    c.panel_height = H;
    c.readable = true;
    c.invert = o.invert;
    c.rgb_order = o.bgr;
    c.dlen_16bit = false;
    c.bus_shared = false;
    c.offset_rotation = 0;
    p->config(c);
    setPanel(p);
    setRotation(o.rotation);
  }
};

}  // namespace cyd
