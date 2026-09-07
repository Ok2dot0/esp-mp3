#pragma once

#include <Arduino.h>
#include <vector>
#include "bluetooth.h"

// Owns the ILI9341 LCD and paints the player UI.
// Repaints only when content actually changes, so the screen never blinks.
class Screen
{
public:
  void begin();

  // Now-playing view: track, volume, bluetooth status. Any argument that
  // did not change since the last call costs zero SPI traffic.
  void show(const String &trackName, int volume, const String &btStatus);

  // Bluetooth pairing view: discovered devices with a highlighted
  // selection, plus a status footer. Same change-only repaint policy.
  void showDevices(const std::vector<BtDevice> &devices, int selected, const String &footer);

private:
  static constexpr int kMaxRows = 6;

  String lastTrack_;
  int lastVolume_ = -1;
  String lastBt_;
  String lastListSig_;

  void repaint(const String &trackName, int volume, const String &btStatus);
  void repaintDevices(const std::vector<BtDevice> &devices, int selected, const String &footer);
};
