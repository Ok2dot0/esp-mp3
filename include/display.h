#pragma once

#include <Arduino.h>

// Owns the ILI9341 LCD and paints the player UI.
// Repaints only when content actually changes, so the screen never blinks.
class Screen
{
public:
  void begin();

  // Shows the now-playing state. Any argument that did not change since
  // the last call costs zero SPI traffic.
  void show(const String &trackName, int volume, const String &btStatus);

private:
  String lastTrack_;
  int lastVolume_ = -1;
  String lastBt_;

  void repaint(const String &trackName, int volume, const String &btStatus);
};
