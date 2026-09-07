// esp-mp3: iPod-style ESP32-S3 music player.
//
// Modules (see include/ + src/):
//   pins         - central pin map
//   filesystem   - SD card + audio file discovery
//   bluetooth    - KCX BT emitter driver + connection state
//   clickwheel   - iPod click wheel decoder
//   display      - LCD now-playing screen (track, volume, bluetooth)
//   audio_player - I2S playback, volume, auto-repeat
//
// main.cpp only owns the module instances and wires them together.

#include "Arduino.h"
#include "audio_player.h"
#include "bluetooth.h"
#include "clickwheel.h"
#include "display.h"
#include "filesystem.h"
#include "pins.h"

namespace
{

KcxController bt(Pins::KCX_RX, Pins::KCX_TX);
ClickWheel wheel(Pins::CLICK_CLK, Pins::CLICK_DATA);
Screen screen;
AudioPlayer player;

void IRAM_ATTR clickWheelISR()
{
  wheel.handleEdge();
}

void resetClickWheel()
{
  digitalWrite(Pins::CLICK_RESET, LOW);
  delay(20);
  digitalWrite(Pins::CLICK_RESET, HIGH);
  delay(50);
}

void setupSerial()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== System Starting ===");
}

void setupFileSystem()
{
  if (FileSystem::initSD())
  {
    Serial.println("[SD] Initialized successfully.");
    Serial.printf("[SD] Type: %u, size: %llu MB\n",
                  SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL));
    FileSystem::scan("/");
    Serial.printf("[FS] Found %u track(s).\n", (unsigned)FileSystem::tracks.size());
    if (!FileSystem::tracks.empty())
    {
      player.playFile(FileSystem::tracks[0].path.c_str(),
                      FileSystem::tracks[0].name.c_str());
    }
    else
    {
      Serial.println("[FS] No tracks found, nothing to play.");
    }
  }
  else
  {
    Serial.println("[SD] Initialization failed!");
  }
}

void setupClickWheel()
{
  pinMode(Pins::CLICK_RESET, OUTPUT);
  resetClickWheel();
  pinMode(Pins::CLICK_WAKE, INPUT_PULLUP);

  wheel.onReport([](const ClickWheel::State &state)
  {
    // Convert wheel motion into volume input. Only count motion while the
    // finger stays down: a fresh touch has no reference position, so using
    // it would turn every re-touch into a random volume jump.
    static uint8_t prevPos = 0;
    static bool prevTouch = false;
    if (state.touching && prevTouch)
    {
      // int8_t cast keeps the 0..255 wraparound direction-aware.
      int d = (int8_t)(state.position - prevPos);
      // Clamp spikes from noisy frames; the accumulator keeps the rest.
      if (d > 10) d = 10;
      if (d < -10) d = -10;
      player.addWheelMotion(d);
    }
    prevPos = state.position;
    prevTouch = state.touching;
    Serial.printf("[Wheel] %s pos=%3u | Buttons [C:%d U:%d D:%d R:%d L:%d]\n",
                  state.touching ? "TOUCH" : "FREE ",
                  state.position,
                  state.btnCenter,
                  state.btnUp,
                  state.btnDown,
                  state.btnRight,
                  state.btnLeft);
  });

  wheel.begin(clickWheelISR);
  Serial.println("[Wheel] Click wheel listener started.");
}

void setupBluetooth()
{
  bt.onDeviceFound([](const BtDevice &dev)
  {
    Serial.printf("[BT] Found: %-25s | MAC: %s\n", dev.name.c_str(), dev.mac.c_str());
    bt.connectByMac(dev.mac);
  });

  bt.onConnectionChange([](bool connected, const String &)
  {
    if (connected)
      Serial.println("[BT] Status: Connected to audio sink.");
    else
      Serial.println("[BT] Status: Disconnected / Scanning.");
  });

  bt.begin();
  bt.requestVersion();
  Serial.println("[BT] Starting clean scan...");
  bt.startScan(true);
}

void loopClickWheel()
{
  wheel.update(true);

  static unsigned long lastWheelDiag = 0;
  static uint32_t lastDiagEdgeCount = 0;
  if (millis() - lastWheelDiag > 2000)
  {
    lastWheelDiag = millis();
    uint32_t edgesNow = wheel.edgeCount();
    Serial.printf("[Wheel] diag: %lu total edges (+%lu since last) | WAKE=%d\n",
                  (unsigned long)edgesNow,
                  (unsigned long)(edgesNow - lastDiagEdgeCount),
                  digitalRead(Pins::CLICK_WAKE));
    lastDiagEdgeCount = edgesNow;
  }
}

void loopDisplay()
{
  String track = FileSystem::tracks.empty() ? "No tracks found." : FileSystem::tracks[0].name;
  screen.show(track, player.volume(), bt.statusText());
}

void loopSerialCommands()
{
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0)
      bt.sendCommand(cmd);
  }
}

} // namespace

void setup()
{
  setupSerial();
  screen.begin();
  player.begin(Pins::DAC_BCK, Pins::DAC_WS, Pins::DAC_DIN);
  setupFileSystem();
  setupClickWheel();
  setupBluetooth();
}

void loop()
{
  player.update();
  bt.update();
  loopClickWheel();
  loopDisplay();
  loopSerialCommands();
  vTaskDelay(1);
}
