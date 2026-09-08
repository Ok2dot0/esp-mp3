#include "filesystem.h"
#include "pins.h"
#include <algorithm>

std::vector<Track> FileSystem::tracks;
SPIClass FileSystem::sdSpi = SPIClass(HSPI);

bool FileSystem::initSD()
{
  // Deselect both SPI devices before touching the bus so the LCD
  // (already initialized) cannot answer to SD traffic.
  pinMode(Pins::LCD_CS, OUTPUT);
  digitalWrite(Pins::LCD_CS, HIGH);
  pinMode(Pins::SD_CS, OUTPUT);
  digitalWrite(Pins::SD_CS, HIGH);
  // Give the card time to power up after the ESP32 booted.
  delay(500);
  sdSpi.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  static constexpr uint32_t kSpeeds[] = {10000000, 4000000, 1000000};
  for (uint8_t attempt = 0; attempt < 5; ++attempt)
  {
    uint32_t freq = kSpeeds[attempt < 3 ? attempt : 2];
    Serial.printf("[SD] Mount attempt %u at %lu Hz...\n", attempt + 1, (unsigned long)freq);
    digitalWrite(Pins::SD_CS, HIGH);
    delay(100);
    if (SD.begin(Pins::SD_CS, sdSpi, freq))
      return true;
    Serial.println("[SD] Attempt failed, retrying...");
    delay(300);
  }
  return false;
}

void FileSystem::scan(const char *rootPath)
{
  tracks.clear();
  File root = SD.open(rootPath);
  if (!root || !root.isDirectory())
  {
    Serial.printf("[FS] Failed to open directory: %s\n", rootPath);
    return;
  }
  scanDirectory(root);
  // Stable order (FAT readdir order is arbitrary): sort by full path so
  // albums stay together in track order (filenames start with the
  // track number) and indexes are predictable for picking.
  std::sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b)
            { return strcasecmp(a.path.c_str(), b.path.c_str()) < 0; });
}

void FileSystem::scanDirectory(File &dir, uint8_t depth)
{
  (void)depth;
  File entry = dir.openNextFile();
  while (entry)
  {
    String name = String(entry.name());
    int slashIdx = name.lastIndexOf('/');
    String cleanName = (slashIdx >= 0) ? name.substring(slashIdx + 1) : name;

    String lowerName = cleanName;
    lowerName.toLowerCase();

    if (entry.isDirectory())
    {
      if (cleanName != "." && cleanName != ".." && cleanName.indexOf("System Volume") < 0 && cleanName.indexOf("$RECYCLE") < 0)
      {
        scanDirectory(entry, depth + 1);
      }
    }
    else if (lowerName.endsWith(".mp3") || lowerName.endsWith(".wav") || lowerName.endsWith(".flac") || lowerName.endsWith(".m4a"))
    {
      String fullPath = entry.path();
      if (!fullPath.startsWith("/"))
        fullPath = "/" + fullPath;
      tracks.push_back({fullPath, cleanName});
    }
    entry.close();
    entry = dir.openNextFile();
  }
}
