#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

struct Track
{
  String path;
  String name;
};

// SD card handling + audio file discovery.
class FileSystem
{
public:
  static std::vector<Track> tracks;

  // Mounts the SD card. Retries with a step-down SPI clock because
  // marginal wiring that fails at 10 MHz often works at 4/1 MHz.
  static bool initSD();

  // Recursively collects playable audio files under rootPath.
  static void scan(const char *rootPath = "/");

private:
  // Dedicated SPI bus: the LCD (LovyanGFX) owns SPI2/FSPI, so the SD
  // card gets SPI3/HSPI to avoid both drivers fighting over one host.
  static SPIClass sdSpi;

  static void scanDirectory(File &dir, uint8_t depth = 0);
};
