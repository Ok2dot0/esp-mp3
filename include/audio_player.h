#pragma once

#include <Arduino.h>
#include "Audio.h"

// Audio playback: I2S output, volume, and track metadata.
// Repeat/advance policy lives in main: update() only reports EOF via
// takeRepeat(); main decides what plays next.
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
  void setVolume(int v) {
    if (v < kMinVolume)
      v = kMinVolume;
    if (v > kMaxVolume)
      v = kMaxVolume;
    volume_ = v;
  }
  const String &currentLabel() const { return currentLabel_; }
  // Vorbis/ID3 metadata of the current file (empty until heard).
  const String &metaTitle() const { return metaTitle_; }
  const String &metaArtist() const { return metaArtist_; }
  const String &metaAlbum() const { return metaAlbum_; }

  // True once per finished track (consume to advance the playlist).
  bool takeRepeat();

  // Services the decoder and applies pending volume.
  void update();

private:
  static AudioPlayer *self_;

  static void onAudioInfo(Audio::msg_t msg);
  void handleInfo(Audio::msg_t msg);
  void handleMetadata(const String &tag);

  int volume_ = 12;
  volatile int wheelDelta_ = 0;
  int volRemainder_ = 0;
  volatile bool repeat_ = false;
  String currentPath_;
  String currentLabel_;
  String metaTitle_;
  String metaArtist_;
  String metaAlbum_;
};
