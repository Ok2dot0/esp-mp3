#include "display.h"
#include "pins.h"
#include "ui_theme.h"
#include <LovyanGFX.hpp>
#include <lvgl.h>

// Driver + Aero UI in one file. Restyle via ui_theme.h only.

namespace {
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;

public:
  LGFX() {
    auto bcfg = bus_.config();
    bcfg.spi_host = SPI2_HOST;
    bcfg.spi_mode = 0;
    bcfg.freq_write = 40000000;
    bcfg.freq_read = 16000000;
    bcfg.pin_sclk = Pins::LCD_SCLK;
    bcfg.pin_mosi = Pins::LCD_MOSI;
    bcfg.pin_miso = Pins::LCD_MISO;
    bcfg.pin_dc = Pins::LCD_DC;
    bus_.config(bcfg);
    panel_.setBus(&bus_);

    auto pcfg = panel_.config();
    pcfg.pin_cs = Pins::LCD_CS;
    pcfg.pin_rst = Pins::LCD_RST;
    pcfg.rgb_order = false; // true if red/blue look swapped
    panel_.config(pcfg);

    auto lcfg = light_.config();
    lcfg.pin_bl = Pins::LCD_BL;
    lcfg.freq = 5000;
    lcfg.pwm_channel = 7;
    light_.config(lcfg);
    panel_.setLight(&light_);

    auto tcfg = touch_.config();
    tcfg.spi_host = SPI2_HOST; // shares SCK/MOSI/MISO, own CS
    tcfg.pin_sclk = Pins::LCD_SCLK;
    tcfg.pin_mosi = Pins::LCD_MOSI;
    tcfg.pin_miso = Pins::LCD_MISO;
    tcfg.pin_cs = Pins::TOUCH_CS;
    tcfg.pin_int = Pins::TOUCH_IRQ;
    tcfg.freq = 2000000;
    tcfg.bus_shared = true;
    touch_.config(tcfg);
    panel_.setTouch(&touch_);
    setPanel(&panel_);
  }
};

LGFX lcd;
Screen *g_self = nullptr;
uint16_t buf1[320 * 24]; // partial tiles; blocking flush, audio-safe
uint16_t buf2[320 * 24];

void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  lcd.pushImage(a->x1, a->y1, (uint32_t)(a->x2 - a->x1 + 1),
                (uint32_t)(a->y2 - a->y1 + 1), (const uint16_t *)px);
  lv_display_flush_ready(d);
}

void touch_cb(lv_indev_t *, lv_indev_data_t *d) {
  if (digitalRead(Pins::TOUCH_IRQ) == HIGH) {
    d->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    d->point.x = x;
    d->point.y = y;
    d->state = LV_INDEV_STATE_PRESSED;
  } else {
    d->state = LV_INDEV_STATE_RELEASED;
  }
}

String plainMac(const String &m) {
  String p = m;
  p.replace(":", "");
  p.toLowerCase();
  return p;
}

// Widgets, built once.
lv_obj_t *status_, *title_, *artist_, *album_, *vol_, *volPct_, *playLbl_;
lv_obj_t *btLine_, *trackHead_, *trackList_, *btStat_, *btPeer_, *btList_;
lv_obj_t *btAction_, *btActionLbl_, *overlay_, *msg1_, *msg2_, *tabview_;

void firePlay() {
  if (g_self && g_self->events().play)
    g_self->events().play();
}
void fireNext() {
  if (g_self && g_self->events().next)
    g_self->events().next();
}
void firePrev() {
  if (g_self && g_self->events().prev)
    g_self->events().prev();
}
void fireBtAction() {
  if (g_self && g_self->events().btAction)
    g_self->events().btAction();
}
void onVol(lv_event_t *e) {
  if (!g_self || !g_self->events().volume)
    return;
  g_self->events().volume((int)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)));
}
void onTrack(lv_event_t *e) {
  if (!g_self || !g_self->events().pickTrack)
    return;
  g_self->events().pickTrack((int)(intptr_t)lv_event_get_user_data(e));
}
void onBt(lv_event_t *e) {
  if (!g_self || !g_self->events().pickBt)
    return;
  g_self->events().pickBt((int)(intptr_t)lv_event_get_user_data(e));
}
void onPlayBtn(lv_event_t *) { firePlay(); }
void onNextBtn(lv_event_t *) { fireNext(); }
void onPrevBtn(lv_event_t *) { firePrev(); }
void onBtActionBtn(lv_event_t *) { fireBtAction(); }

void white(lv_obj_t *o) {
  lv_obj_set_style_text_color(o, lv_color_hex(0xFFFFFF), 0);
}
void bubble(int x, int y, int r) {
  lv_obj_t *o = lv_obj_create(lv_scr_act());
  lv_obj_set_size(o, r * 2, r * 2);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_10, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *rowBtn(lv_obj_t *parent, const String &t, bool sel, int idx,
                 lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, Theme::ROW_H);
  lv_obj_set_style_radius(b, Theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(sel ? Theme::ACCENT : Theme::CARD), 0);
  if (!sel)
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, t.c_str());
  lv_obj_set_style_text_color(l, lv_color_hex(sel ? Theme::CARD : Theme::INK), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  return b;
}

lv_obj_t *bigBtn(lv_obj_t *parent, const char *t, lv_event_cb_t cb) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_height(b, Theme::ROW_H);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_radius(b, Theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(Theme::CARD), 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, lv_color_hex(Theme::INK), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void listStyle(lv_obj_t *list) {
  lv_obj_set_width(list, lv_pct(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_gap(list, 4, 0);
}

void buildUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(Theme::BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(Theme::BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
  bubble(230, 30, 46);
  bubble(20, 130, 30);
  bubble(120, 180, 18);

  status_ = lv_label_create(scr);
  lv_obj_set_width(status_, lv_pct(100));
  lv_obj_set_pos(status_, 4, 2);
  white(status_);

  tabview_ = lv_tabview_create(scr);
  lv_tabview_set_tab_bar_position(tabview_, LV_DIR_BOTTOM);
  lv_obj_set_pos(tabview_, 0, 20);
  lv_obj_set_size(tabview_, 320, 220);
  lv_obj_set_style_bg_opa(tabview_, LV_OPA_TRANSP, 0);
  lv_obj_t *tPlay = lv_tabview_add_tab(tabview_, "Now");
  lv_obj_t *tTracks = lv_tabview_add_tab(tabview_, "Tracks");
  lv_obj_t *tBt = lv_tabview_add_tab(tabview_, "BT");
  for (auto *t : {tPlay, tTracks, tBt}) {
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(t, 4, 0);
  }

  title_ = lv_label_create(tPlay);
  lv_obj_set_style_text_font(title_, &lv_font_montserrat_20, 0);
  white(title_);
  artist_ = lv_label_create(tPlay);
  white(artist_);
  album_ = lv_label_create(tPlay);
  white(album_);
  lv_obj_t *row = lv_obj_create(tPlay);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  bigBtn(row, "|<", onPrevBtn);
  lv_obj_t *pb = bigBtn(row, "Play", onPlayBtn);
  playLbl_ = lv_obj_get_child(pb, 0);
  bigBtn(row, ">|", onNextBtn);
  vol_ = lv_slider_create(tPlay);
  lv_slider_set_range(vol_, 0, 21);
  lv_obj_set_width(vol_, lv_pct(100));
  lv_obj_add_event_cb(vol_, onVol, LV_EVENT_VALUE_CHANGED, nullptr);
  volPct_ = lv_label_create(tPlay);
  white(volPct_);
  btLine_ = lv_label_create(tPlay);
  white(btLine_);

  trackHead_ = lv_label_create(tTracks);
  white(trackHead_);
  trackList_ = lv_obj_create(tTracks);
  listStyle(trackList_);

  btStat_ = lv_label_create(tBt);
  white(btStat_);
  btPeer_ = lv_label_create(tBt);
  lv_obj_set_style_text_font(btPeer_, &lv_font_montserrat_16, 0);
  white(btPeer_);
  btList_ = lv_obj_create(tBt);
  listStyle(btList_);
  btAction_ = lv_btn_create(tBt);
  lv_obj_set_width(btAction_, lv_pct(100));
  lv_obj_set_height(btAction_, Theme::ROW_H);
  lv_obj_set_style_radius(btAction_, Theme::RADIUS, 0);
  lv_obj_set_style_bg_color(btAction_, lv_color_hex(Theme::ACCENT), 0);
  btActionLbl_ = lv_label_create(btAction_);
  lv_label_set_text(btActionLbl_, "Connect");
  lv_obj_set_style_text_color(btActionLbl_, lv_color_hex(Theme::CARD), 0);
  lv_obj_center(btActionLbl_);
  lv_obj_add_event_cb(btAction_, onBtActionBtn, LV_EVENT_CLICKED, nullptr);

  overlay_ = lv_obj_create(scr);
  lv_obj_set_size(overlay_, 320, 240);
  lv_obj_set_pos(overlay_, 0, 0);
  lv_obj_set_style_bg_color(overlay_, lv_color_hex(Theme::INK), 0);
  lv_obj_set_style_bg_opa(overlay_, LV_OPA_90, 0);
  lv_obj_set_style_border_width(overlay_, 0, 0);
  lv_obj_t *spin = lv_spinner_create(overlay_);
  lv_spinner_set_anim_params(spin, 1000, 60);
  lv_obj_center(spin);
  msg1_ = lv_label_create(overlay_);
  lv_obj_align(msg1_, LV_ALIGN_CENTER, 0, -40);
  white(msg1_);
  msg2_ = lv_label_create(overlay_);
  lv_obj_align(msg2_, LV_ALIGN_CENTER, 0, 40);
  white(msg2_);
  lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void hideOverlay() { lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN); }

void paintBtRows(const std::vector<String> &labels, int selected,
                 const String &action) {
  lv_obj_clean(btList_);
  for (size_t i = 0; i < labels.size(); ++i) {
    lv_obj_t *b = rowBtn(btList_, labels[i], (int)i == selected, (int)i, onBt);
    if ((int)i == selected)
      lv_obj_scroll_to_view(b, LV_ANIM_OFF);
  }
  lv_label_set_text(btActionLbl_, action.c_str());
}
} // namespace

void Screen::begin() {
  g_self = this;
  lcd.init();
  lcd.setRotation(3);
  lcd.setColorDepth(16);
  lcd.setBrightness(255);
  pinMode(Pins::TOUCH_IRQ, INPUT_PULLUP);

  lv_init();
  lv_display_t *disp = lv_display_create(320, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);
  lv_indev_set_display(indev, disp);
  buildUi();
}

void Screen::tick() {
  unsigned long now = millis(); // throttled so audio never starves
  if (now - lastTick_ < 5)
    return;
  lastTick_ = now;
  lv_timer_handler();
}

void Screen::setBrightness(uint8_t b) { lcd.setBrightness(b); }

void Screen::showPlayer(const String &title, const String &artist, int volume,
                        const String &btStatus, const String &album,
                        bool playing) {
  String sig =
      title + "|" + artist + "|" + album + "|" + String(volume) + "|" +
      btStatus + "|" + (playing ? "1" : "0");
  if (sig == lastPlayer_)
    return;
  lastPlayer_ = sig;
  hideOverlay();
  lv_label_set_text(title_, title.isEmpty() ? "..." : title.c_str());
  lv_label_set_text(artist_, artist.c_str());
  lv_label_set_text(album_, album.c_str());
  lv_slider_set_value(vol_, volume, LV_ANIM_OFF);
  lv_label_set_text(volPct_, ("Vol " + String(volume)).c_str());
  lv_label_set_text(playLbl_, playing ? "Pause" : "Play");
  lv_label_set_text(btLine_, btStatus.c_str());
  lv_label_set_text(status_, btStatus.c_str());
}

void Screen::showTracks(const std::vector<String> &labels, int highlight,
                        const String &header) {
  String sig = String(highlight) + "|" + header + "|";
  for (auto &l : labels)
    sig += l + ";";
  if (sig == lastTracks_)
    return;
  lastTracks_ = sig;
  hideOverlay();
  lv_label_set_text(trackHead_, header.c_str());
  lv_obj_clean(trackList_);
  for (size_t i = 0; i < labels.size(); ++i) {
    lv_obj_t *b = rowBtn(trackList_, labels[i], (int)i == highlight, (int)i, onTrack);
    if ((int)i == highlight)
      lv_obj_scroll_to_view(b, LV_ANIM_OFF);
  }
}

void Screen::message(const String &line1, const String &line2) {
  lastPlayer_ = "\x02";
  lastBt_ = "\x02";
  lv_label_set_text(msg1_, line1.c_str());
  lv_label_set_text(msg2_, line2.c_str());
  lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void Screen::showBt(const std::vector<BtDevice> &saved,
                    const std::vector<BtDevice> &scanned, int selected,
                    const String &status, bool connected, const String &peer) {
  String sig = String(selected) + "|" + status + "|" +
               (connected ? "1" : "0") + peer + "|";
  for (auto &d : saved)
    sig += "S" + d.name + "," + d.mac + ";";
  sig += "|";
  for (auto &d : scanned)
    sig += "F" + d.name + "," + d.mac + ";";
  if (sig == lastBt_)
    return;
  lastBt_ = sig;
  hideOverlay();
  lv_label_set_text(status_, status.c_str());
  lv_label_set_text(btStat_, status.c_str());
  if (connected) {
    lv_label_set_text(btPeer_, peer.isEmpty() ? "audio sink" : peer.c_str());
    lv_obj_clean(btList_);
    paintBtRows({}, -1, "Disconnect");
    return;
  }
  lv_label_set_text(btPeer_, "");
  std::vector<String> labels;
  for (auto &d : saved) {
    if (plainMac(d.mac) == "deadbeefcafe")
      continue;
    String l = d.name.isEmpty() ? d.mac : d.name;
    labels.push_back("* " + l.substring(0, 20));
  }
  for (auto &d : scanned) {
    bool dupe = false;
    for (auto &s : saved)
      if (plainMac(s.mac) == plainMac(d.mac))
        dupe = true;
    if (dupe)
      continue;
    String l = d.name.isEmpty() ? d.mac : d.name;
    labels.push_back(l.substring(0, 22));
  }
  if (labels.empty())
    lv_label_set_text(btStat_, "Scanning... no devices yet");
  paintBtRows(labels, selected, labels.empty() ? "Rescan" : "Connect");
}

void Screen::showDevices(const std::vector<BtDevice> &devices, int selected,
                         const String &footer,
                         const std::vector<String> &knownMacs) {
  String sig = String(selected) + "|" + footer + "|";
  for (auto &d : devices)
    sig += d.name + "," + d.mac + ";";
  if (sig == lastBt_)
    return;
  lastBt_ = sig;
  hideOverlay();
  lv_label_set_text(status_, footer.c_str());
  lv_label_set_text(btStat_, footer.c_str());
  lv_label_set_text(btPeer_, "");
  std::vector<String> labels;
  for (auto &d : devices) {
    String l = d.name.isEmpty() ? d.mac : d.name;
    bool known = false;
    for (auto &k : knownMacs)
      if (plainMac(d.mac) == k)
        known = true;
    labels.push_back((known ? String("* ") : String("")) + l.substring(0, 22));
  }
  paintBtRows(labels, selected, "Connect");
}
