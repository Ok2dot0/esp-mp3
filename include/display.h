#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include "bluetooth.h"

// Touch actions the UI can raise. main.cpp wires these to playback/BT.
struct UiEvents {
  std::function<void()> play = nullptr;
  std::function<void()> next = nullptr;
  std::function<void()> prev = nullptr;
  std::function<void(int)> volume = nullptr;
  std::function<void(int)> pickTrack = nullptr;
  std::function<void(int)> pickBt = nullptr;
  std::function<void()> btAction = nullptr;
};

// Owns LCD + touch + LVGL. Same show* API as before, now pretty.
// Only repaints on change, tick() pumps LVGL without blocking audio.
class Screen {
public:
  void begin();
  void tick();
  void setEvents(const UiEvents &ev) { ev_ = ev; }
  const UiEvents &events() const { return ev_; }
  void setBrightness(uint8_t b);

  void showPlayer(const String &title, const String &artist, int volume,
                  const String &btStatus, const String &album = "",
                  bool playing = true);
  void showTracks(const std::vector<String> &labels, int highlight,
                  const String &header);
  void message(const String &line1, const String &line2 = "");
  void showBt(const std::vector<BtDevice> &saved,
              const std::vector<BtDevice> &scanned, int selected,
              const String &status, bool connected, const String &peer);
  void showDevices(const std::vector<BtDevice> &devices, int selected,
                   const String &footer, const std::vector<String> &knownMacs);

private:
  UiEvents ev_;
  String lastPlayer_, lastTracks_, lastBt_;
  unsigned long lastTick_ = 0;
};
