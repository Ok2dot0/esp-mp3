#include "clickwheel.h"

ClickWheel::ClickWheel(uint8_t clkPin, uint8_t dataPin)
    : clkPin_(clkPin), dataPin_(dataPin) {}

void ClickWheel::begin(void (*isr)())
{
  pinMode(clkPin_, INPUT_PULLUP);
  pinMode(dataPin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(clkPin_), isr, FALLING);
}

void ClickWheel::onReport(ReportCallback cb)
{
  onReport_ = cb;
}

void ClickWheel::update(bool printRaw)
{
  if (!frameReady_)
    return;

  uint8_t frame[4];

  noInterrupts();
  for (uint8_t i = 0; i < 4; ++i)
    frame[i] = frame_[i];
  frameReady_ = false;
  interrupts();

  if (printRaw)
  {
    Serial.printf("[Wheel] raw: %02X %02X %02X %02X\n",
                  frame[0], frame[1], frame[2], frame[3]);
  }

  State state;
  state.delta = frame[2] - lastPosition_;
  lastPosition_ = frame[2];
  state.position = frame[2];
  state.touching = (frame[3] & 0x40) != 0;
  state.statusByte = frame[3];

  if (onReport_)
    onReport_(state);
}

void IRAM_ATTR ClickWheel::handleEdge()
{
  uint32_t now = micros();

  if ((byteIndex_ != 0 || bitCount_ != 0) &&
      (now - lastEdgeUs_) > kFrameGapTimeoutUs)
  {
    shiftByte_ = 0;
    bitCount_ = 0;
    byteIndex_ = 0;
  }
  lastEdgeUs_ = now;
  edgeCount_ = edgeCount_ + 1;

  uint8_t bit = static_cast<uint8_t>(digitalRead(dataPin_));
  shiftByte_ |= static_cast<uint8_t>(bit << bitCount_);
  bitCount_ = bitCount_ + 1;

  if (bitCount_ < 8)
    return;

  if (byteIndex_ == 0 && shiftByte_ != 0x1A)
  {
    shiftByte_ = 0;
    bitCount_ = 0;
    return;
  }

  frame_[byteIndex_] = shiftByte_;
  byteIndex_ = byteIndex_ + 1;
  shiftByte_ = 0;
  bitCount_ = 0;

  if (byteIndex_ == 4)
  {
    byteIndex_ = 0;
    frameReady_ = true;
  }
}
