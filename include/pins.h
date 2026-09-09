#pragma once

#include <Arduino.h>

// Central pin map. Change wiring here; every module picks it up.
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
  // LINK: KCX LINK/LED pin, HIGH while a BT link is up. Wired, uses the
  // library's CHANGE interrupt (replaces the old AT+STATUS? polling).
  constexpr uint8_t KCX_LINK = 11;
  // MODE: intentionally NOT wired (module stays in TX/emitter mode).
  // The library refuses to init with a -1 pin, so this is a spare GPIO
  // used as a dummy output and left physically unconnected. Never call
  // setMode()/changeMode() (they drive this pin + RESET the module).
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

  // Board BOOT button, active LOW. Used to confirm pairing etc.
  // (Also a strapping pin: keep HIGH at reset for normal boot.)
  constexpr uint8_t BTN_BOOT = 0;

  // LCD on SPI2 (owned by LovyanGFX).
  constexpr uint8_t LCD_SCLK = 8;
  constexpr uint8_t LCD_MOSI = 42;
  constexpr uint8_t LCD_MISO = 13;
  constexpr uint8_t LCD_DC = 2;
  constexpr uint8_t LCD_CS = 15;
  constexpr uint8_t LCD_BL = -1; // No backlight pin wired.
}
