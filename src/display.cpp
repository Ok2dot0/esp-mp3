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

// True when the colon-formatted MAC is in the no-colon known list.
bool isKnownMac(const String &formattedMac, const std::vector<String> &knownMacs)
{
  String plain = formattedMac;
  plain.replace(":", "");
  plain.toLowerCase();
  for (const auto &known : knownMacs)
  {
    if (plain == known)
      return true;
  }
  return false;
}

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
void Screen::showPlayer(const String &title, const String &artist, int volume,
                        const String &btStatus)
{
  String sig = title + "|" + artist + "|" + String(volume) + "|" + btStatus;
  if (sig == lastTrack_)
    return;
  Serial.printf("[DSP] player repaint: '%s' heap=%u\n", title.c_str(), (unsigned)ESP.getFreeHeap());
  lastTrack_ = sig;
  lastVolume_ = volume;
  lastBt_ = btStatus;
  lastListSig_ = "\x01";
  lastTracksSig_ = "\x01";
  repaintPlayer(title, artist, volume, btStatus);
  Serial.println("[DSP] player repaint done");
}

void Screen::showTracks(const std::vector<String> &labels, int highlight,
                        const String &header)
{
  String sig = String(highlight) + "|" + header + "|";
  for (const auto &l : labels)
    sig += l + ";";
  if (sig == lastTracksSig_)
    return;
  lastTracksSig_ = sig;
  lastTrack_ = "\x01";
  lastListSig_ = "\x01";
  repaintTracks(labels, highlight, header);
}

void Screen::repaintPlayer(const String &title, const String &artist, int volume,
                           const String &btStatus)
{
  lcd.fillRect(0, 0, lcd.width(), 120, TFT_BLACK);
  lcd.setCursor(0, 0);
  lcd.println(title.isEmpty() ? String("...") : title);
  lcd.setCursor(0, 20);
  lcd.println(artist);
  lcd.setCursor(0, 50);
  lcd.printf("Volume: %d\n", volume);
  lcd.setCursor(0, 70);
  lcd.println(btStatus);
}

void Screen::repaintTracks(const std::vector<String> &labels, int highlight,
                           const String &header)
{
  constexpr int kTop = 20;
  constexpr int kRowH = 18;
  constexpr int kMaxRows = 6;
  lcd.fillRect(0, 0, lcd.width(), 240, TFT_BLACK);
  lcd.setCursor(0, 0);
  lcd.println(header);
  int count = (int)labels.size();
  int offset = 0;
  if (highlight >= kMaxRows)
    offset = highlight - kMaxRows + 1;
  if (offset > count - kMaxRows)
    offset = count - kMaxRows;
  if (offset < 0)
    offset = 0;
  int rows = count - offset;
  if (rows > kMaxRows)
    rows = kMaxRows;
  for (int i = 0; i < rows; ++i)
  {
    String label = labels[(size_t)(offset + i)];
    if (label.length() > 24)
      label = label.substring(0, 24);
    int y = kTop + i * kRowH;
    if (offset + i == highlight)
    {
      lcd.fillRect(0, (uint16_t)y, lcd.width(), (uint16_t)kRowH, TFT_YELLOW);
      lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
      lcd.setCursor(4, y + 1);
      lcd.println(label);
      lcd.setTextColor(TFT_YELLOW);
    }
    else
    {
      lcd.setCursor(4, y + 1);
      lcd.println(label);
    }
  }
  lcd.setCursor(0, 220);
  lcd.println("Boot:play");
}

void Screen::message(const String &line1, const String &line2)
{
  // Invalidate both view caches so the next regular repaint happens.
  lastTrack_ = "\x02";
  lastListSig_ = "\x02";
  lcd.fillRect(0, 0, lcd.width(), 120, TFT_BLACK);
  lcd.setCursor(0, 0);
  lcd.println(line1);
  if (!line2.isEmpty())
  {
    lcd.setCursor(0, 20);
    lcd.println(line2);
  }
}

void Screen::showDevices(const std::vector<BtDevice> &devices, int selected,
                      const String &footer, const std::vector<String> &knownMacs)
{
  // Signature covers everything visible; unchanged screen = zero traffic.
  String sig = String(selected) + "|" + footer + "|";
  for (const auto &d : devices)
    sig += String(isKnownMac(d.mac, knownMacs) ? '*' : ' ') + d.name + "," + d.mac + ";";
  if (sig == lastListSig_)
    return;
  lastListSig_ = sig;
  // Invalidate the now-playing cache so switching views repaints.
  lastTrack_ = "\x01";
  repaintDevices(devices, selected, footer, knownMacs);
}

void Screen::repaintDevices(const std::vector<BtDevice> &devices, int selected,
                            const String &footer, const std::vector<String> &knownMacs)
{
  constexpr int kTop = 20;
  constexpr int kRowH = 18;
  lcd.fillRect(0, 0, lcd.width(), 240, TFT_BLACK);
  lcd.setCursor(0, 0);
  lcd.println("Bluetooth:");

  if (devices.empty())
  {
    lcd.setCursor(0, kTop);
    lcd.println("No devices found");
  }
  else
  {
    // Keep the selection visible in a window of kMaxRows.
    int count = (int)devices.size();
    int offset = 0;
    if (selected >= kMaxRows)
      offset = selected - kMaxRows + 1;
    if (offset > count - kMaxRows)
      offset = count - kMaxRows;
    if (offset < 0)
      offset = 0;
    int rows = count - offset;
    if (rows > kMaxRows)
      rows = kMaxRows;
    for (int i = 0; i < rows; ++i)
    {
      const BtDevice &d = devices[(size_t)(offset + i)];
      int y = kTop + i * kRowH;
      String label = d.name.isEmpty() ? d.mac : d.name;
      if (isKnownMac(d.mac, knownMacs))
        label = "* " + label;
      if (label.length() > 22)
        label = label.substring(0, 22);
      if (offset + i == selected)
      {
        lcd.fillRect(0, (uint16_t)y, lcd.width(), (uint16_t)kRowH, TFT_YELLOW);
        lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
        lcd.setCursor(4, y + 1);
        lcd.println(label);
        lcd.setTextColor(TFT_YELLOW);
      }
      else
      {
        lcd.setCursor(4, y + 1);
        lcd.println(label);
      }
    }
  }

  lcd.setCursor(0, 200);
  lcd.println(footer);
  lcd.setCursor(0, 220);
  lcd.println("Boot:connect");
}
