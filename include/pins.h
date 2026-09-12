#pragma once

#include <Arduino.h>

// Central pin map. Change wiring here; every module picks it up.
//
// Red 2.8" ILI9341+XPT2046 V1.1 wiring (board labels -> ESP32-S3):
//  VCC  -> 3V3              GND  -> GND
//  CS   -> 15               RESET-> 39 (*1)
//  DC   -> 2                SDI  -> 42 (shared with T_DIN)
//  SCK  -> 8  (shared with T_CLK)
//  LED  -> emitter follower on 38 (*2)
//  SDO  -> 13 (shared with T_DO)
//  T_CS -> 41               T_IRQ-> 40 (input, pullup)
//  Onboard SD_* -> leave N/C (external SD on 1/9/3/10 keeps audio glitch-free).
//
// *1 RESET: 39 + 10k to 3V3 + 100nF to GND at the board pin.
//     Dedicated GPIO beats tying to 3V3/EN (those miss soft reboots -> white screen).
// *2 Backlight: 38 --1k--> base BC337/2N2222, collector -> 3V3, emitter -> LED.
//     GPIO sources ~2mA only; backlight current comes from the rail.
//     Direct GPIO->LED exceeds pin spec (~60mA). 5kHz PWM, full-on if tied to 3V3.
namespace Pins
{
// SD card on its own SPI3/HSPI bus (see FileSystem).
constexpr uint8_t SD_SCK = 1;
constexpr uint8_t SD_MISO = 9;
constexpr uint8_t SD_MOSI = 3;
constexpr uint8_t SD_CS = 10;

// KCX BT emitter module (UART + LINK status).
// UART crosses over: ESP RX <- KCX TX, ESP TX -> KCX RX. The
// ESP32-KCX-BT-EMITTER library hardcodes Serial2 for this pair.
constexpr uint8_t KCX_RX = 14;
constexpr uint8_t KCX_TX = 17;
// LINK: KCX LINK/LED pin, HIGH while a BT link is up.
constexpr uint8_t KCX_LINK = 11;
// MODE: intentionally NOT wired (module stays in TX/emitter mode).
// The library refuses to init with a -1 pin, so this is a spare GPIO
// used as a dummy output and left physically unconnected.
constexpr uint8_t KCX_MODE_DUMMY = 12;

// I2S DAC.
constexpr uint8_t DAC_BCK = 6;
constexpr uint8_t DAC_WS = 7;
constexpr uint8_t DAC_DIN = 16;

// iPod click wheel (rotation only - the module has no buttons).
constexpr uint8_t CLICK_CLK = 4;
constexpr uint8_t CLICK_DATA = 5;
constexpr uint8_t CLICK_WAKE = 18;
constexpr uint8_t CLICK_RESET = 21;

// Board BOOT button, active LOW (strapping pin: keep HIGH at reset).
constexpr uint8_t BTN_BOOT = 0;

// LCD + touch on shared SPI2 (owned by LovyanGFX).
constexpr uint8_t LCD_SCLK = 8;
constexpr uint8_t LCD_MOSI = 42; // shared with T_DIN
constexpr uint8_t LCD_MISO = 13; // shared with T_DO
constexpr uint8_t LCD_DC = 2;
constexpr uint8_t LCD_CS = 15;
constexpr uint8_t LCD_RST = 39; // 10k to 3V3 + 100nF to GND at board pin
constexpr uint8_t LCD_BL = 38;  // PWM via follower, see header note
constexpr uint8_t TOUCH_CS = 41;
constexpr uint8_t TOUCH_IRQ = 40; // input with pullup, LOW = touched
} // namespace Pins
