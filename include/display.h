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

  // Now-playing view with metadata: title big, artist second row.
  // Any argument that did not change since the last call costs zero
  // SPI traffic, so the screen never blinks.
  void showPlayer(const String &title, const String &artist, int volume,
                  const String &btStatus);

  // Track browser: window of labels with a highlighted cursor row.
  // Labels carry their own markers (e.g. ">" for now playing).
  void showTracks(const std::vector<String> &labels, int highlight,
                  const String &header);

  // Immediate one-shot message (boot progress etc.). Bypasses the
  // change cache; the next show()/showDevices() repaints over it.
  void message(const String &line1, const String &line2 = "");

  // Bluetooth pairing view: discovered devices with a highlighted
  // selection, plus a status footer. knownMacs holds remembered (auto-
  // link table) MACs without colons; those rows get a "*" marker.
  // Same change-only repaint policy.
  void showDevices(const std::vector<BtDevice> &devices, int selected,
                   const String &footer, const std::vector<String> &knownMacs);

private:
  static constexpr int kMaxRows = 6;

  String lastTrack_;
  int lastVolume_ = -1;
  String lastBt_;
  String lastListSig_;
  String lastTracksSig_;

  void repaintPlayer(const String &title, const String &artist, int volume,
                     const String &btStatus);
  void repaintTracks(const std::vector<String> &labels, int highlight,
                     const String &header);
  void repaintDevices(const std::vector<BtDevice> &devices, int selected,
                      const String &footer, const std::vector<String> &knownMacs);
};
