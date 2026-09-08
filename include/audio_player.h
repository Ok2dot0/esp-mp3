#pragma once

#include <Arduino.h>
#include "Audio.h"

// Audio playback: I2S output, volume with smoothed wheel input,
// and auto-repeat of the current track on EOF.
class AudioPlayer
{
public:
  static constexpr int kMinVolume = 0;
  static constexpr int kMaxVolume = 21;

  AudioPlayer();

  void begin(uint8_t bckPin, uint8_t wsPin, uint8_t dinPin);
  void playFile(const char *path, const char *label);

  // Feed raw click-wheel motion units; converted to volume steps here.
  void addWheelMotion(int delta);

  int volume() const { return volume_; }
  const String &currentLabel() const { return currentLabel_; }

  // Services the decoder, applies pending volume, repeats on EOF.
  void update();

private:
  static AudioPlayer *self_;

  static void onAudioInfo(Audio::msg_t msg);
  void handleInfo(Audio::msg_t msg);

  int volume_ = 12;
  volatile int wheelDelta_ = 0;
  int volRemainder_ = 0;
  volatile bool repeat_ = false;
  String currentPath_;
  String currentLabel_;
};
