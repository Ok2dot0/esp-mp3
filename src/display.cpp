#include "display.h"
#include "pins.h"
#include <LovyanGFX.hpp>

namespace
{

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

public:
  LGFX(void)
  {
    {
      auto cfg = bus_.config();

      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;

      cfg.pin_sclk = Pins::LCD_SCLK;
      cfg.pin_mosi = Pins::LCD_MOSI;
      cfg.pin_miso = Pins::LCD_MISO;
      cfg.pin_dc = Pins::LCD_DC;

      bus_.config(cfg);
      panel_.setBus(&bus_);
    }

    {
      auto cfg = panel_.config();

      cfg.pin_cs = Pins::LCD_CS;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;

      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;

      panel_.config(cfg);
    }

    {
      auto cfg = light_.config();

      cfg.pin_bl = Pins::LCD_BL;

      light_.config(cfg);
      panel_.setLight(&light_);
    }

    setPanel(&panel_);
  }
};

LGFX lcd;

} // namespace

void Screen::begin()
{
  lcd.init();
  lcd.setRotation(3);
  lcd.setColorDepth(18);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(2);
  lcd.setCursor(20, 20);
  lcd.println("ILI9341 Display Ready!");
}

void Screen::show(const String &trackName, int volume, const String &btStatus)
{
  if (trackName == lastTrack_ && volume == lastVolume_ && btStatus == lastBt_)
    return; // Nothing changed: leave the pixels alone (no blink).
  lastTrack_ = trackName;
  lastVolume_ = volume;
  lastBt_ = btStatus;
  repaint(trackName, volume, btStatus);
}

void Screen::repaint(const String &trackName, int volume, const String &btStatus)
{
  lcd.fillRect(0, 0, lcd.width(), 120, TFT_BLACK);
  lcd.setCursor(0, 0);
  lcd.println("Current Track:");
  lcd.setCursor(0, 20);
  lcd.println(trackName);
  lcd.setCursor(0, 50);
  lcd.printf("Volume: %d\n", volume);
  lcd.setCursor(0, 70);
  lcd.println(btStatus);
}
