#include "audio_player.h"
#include <SD.h>

namespace
{
// The decoder instance lives here; the ESP32-audioI2S callback is a plain
// function pointer, so the static trampoline below forwards to the owner.
Audio audio;
} // namespace

AudioPlayer *AudioPlayer::self_ = nullptr;

AudioPlayer::AudioPlayer()
{
  self_ = this;
}

void AudioPlayer::begin(uint8_t bckPin, uint8_t wsPin, uint8_t dinPin)
{
  Audio::audio_info_callback = onAudioInfo;
  audio.setPinout(bckPin, wsPin, dinPin);
  audio.setVolume(volume_);
}

void AudioPlayer::playFile(const char *path, const char *label)
{
  currentPath_ = path;
  currentLabel_ = label;
  metaTitle_ = "";
  metaArtist_ = "";
  metaAlbum_ = "";
  bool ok = audio.connecttoFS(SD, path);
  Serial.printf("[Audio] Playing: %s (connect %s)\n", label, ok ? "OK" : "FAILED");
}

bool AudioPlayer::takeRepeat()
{
  noInterrupts();
  bool r = repeat_;
  repeat_ = false;
  interrupts();
  return r;
}

void AudioPlayer::addWheelMotion(int delta)
{
  wheelDelta_ += delta;
}

void AudioPlayer::update()
{
  audio.loop();
  audio.setVolume(volume_);

  // Fractional accumulation: 1 volume step per 4 wheel units. The leftover
  // remainder is kept, so slow turns still register smoothly and fast
  // spins don't overshoot in one jump.
  noInterrupts();
  int dv = wheelDelta_;
  wheelDelta_ = 0;
  interrupts();
  if (dv != 0)
  {
    volRemainder_ += dv;
    int steps = volRemainder_ / 4;
    if (steps != 0)
    {
      volRemainder_ -= steps * 4;
      volume_ += steps;
      if (volume_ < kMinVolume)
        volume_ = kMinVolume;
      if (volume_ > kMaxVolume)
        volume_ = kMaxVolume;
      Serial.printf("[Audio] Volume: %d\n", volume_);
    }
  }
}

void AudioPlayer::onAudioInfo(Audio::msg_t msg)
{
  if (self_)
    self_->handleInfo(msg);
}

void AudioPlayer::handleInfo(Audio::msg_t m)
{
  if (m.s && m.msg)
  {
    Serial.printf("[Audio %s] %s\n", m.s, m.msg);
  }

  if (m.e == Audio::evt_eof)
  {
    Serial.println("[Audio] EOF event detected!");
    repeat_ = true;
  }

  if (m.e == Audio::evt_id3data && m.msg)
    handleMetadata(String(m.msg));
}

// Metadata arrives as plain "KEY=value" (Vorbis comments in FLAC) or
// "Key: value" (ID3). Keep title/artist/album for the display.
void AudioPlayer::handleMetadata(const String &tag)
{
  int sep = tag.indexOf('=');
  if (sep < 0)
    sep = tag.indexOf(':');
  if (sep <= 0)
    return;
  String key = tag.substring(0, sep);
  key.trim();
  key.toUpperCase();
  String value = tag.substring(sep + 1);
  value.trim();
  if (value.isEmpty())
    return;
  if (key == "TITLE")
    metaTitle_ = value;
  else if (key == "ARTIST")
    metaArtist_ = value;
  else if (key == "ALBUM")
    metaAlbum_ = value;
}
