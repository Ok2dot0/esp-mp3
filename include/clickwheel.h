#pragma once

#include <Arduino.h>
#include <functional>

// Decoder for the iPod 5th-gen click wheel serial protocol.
// Frames arrive via GPIO interrupt (handleEdge); call update() from
// the main loop to decode complete frames into State reports.
class ClickWheel
{
public:
  struct State
  {
    bool touching;
    uint8_t position;
    uint8_t delta;
    uint8_t statusByte;
  };

  using ReportCallback = std::function<void(const State &)>;

  ClickWheel(uint8_t clkPin, uint8_t dataPin);

  void begin(void (*isr)());
  void onReport(ReportCallback cb);

  // Decodes one pending frame (if any) and fires the report callback.
  void update(bool printRaw = false);

  // Call from the GPIO ISR on every falling clock edge.
  void IRAM_ATTR handleEdge();

  uint32_t edgeCount() const { return edgeCount_; }

private:
  uint8_t clkPin_;
  uint8_t dataPin_;
  ReportCallback onReport_ = nullptr;

  volatile uint8_t shiftByte_ = 0;
  volatile uint8_t bitCount_ = 0;
  volatile uint8_t byteIndex_ = 0;
  volatile uint8_t frame_[4] = {0, 0, 0, 0};
  volatile bool frameReady_ = false;

  volatile uint32_t lastEdgeUs_ = 0;
  volatile uint32_t edgeCount_ = 0;
  uint8_t lastPosition_ = 0;
  static constexpr uint32_t kFrameGapTimeoutUs = 1500;
};
