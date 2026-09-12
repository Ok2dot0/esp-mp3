// Aero look in one place. Tweak these, nothing else.
#pragma once
#include <stdint.h>

namespace Theme {
// Aurora gradient + glass white + ink text.
constexpr uint32_t BG_TOP = 0x0A3D4F;
constexpr uint32_t BG_BOTTOM = 0x3EC6E8;
constexpr uint32_t CARD = 0xFFFFFF;
constexpr uint32_t INK = 0x0B2530;
constexpr uint32_t ACCENT = 0xFF9A3D;
constexpr uint32_t BT_OK = 0x2E9E5B;
constexpr uint32_t BT_BUSY = 0xC98A1B;

constexpr int RADIUS = 12; // card/button corner
constexpr int ROW_H = 48;  // min touch target
} // namespace Theme
