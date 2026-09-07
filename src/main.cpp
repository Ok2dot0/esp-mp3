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

// Click-wheel button bits (see ClickWheel::update).
constexpr uint8_t kBtnCenter = 0x01;
constexpr uint8_t kBtnDown = 0x08;
constexpr uint8_t kBtnUp = 0x10;

// Pairing UI state: raw wheel motion and buttons, consumed by loopBtMenu().
// When connected the wheel drives volume instead (see loop routing below).
int wheelMotion = 0;
uint8_t wheelButtons = 0;
String selectedMac;
int listScrollRemainder = 0;

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
    // Convert wheel motion into volume/scroll input. Only count motion
    // while the finger stays down: a fresh touch has no reference
    // position, so using it would turn every re-touch into a jump.
    static uint8_t prevPos = 0;
    static bool prevTouch = false;
    if (state.touching && prevTouch)
    {
      // int8_t cast keeps the 0..255 wraparound direction-aware.
      int d = (int8_t)(state.position - prevPos);
      // Clamp spikes from noisy frames; the accumulator keeps the rest.
      if (d > 10) d = 10;
      if (d < -10) d = -10;
      wheelMotion += d;
    }
    prevPos = state.position;
    prevTouch = state.touching;
    wheelButtons = state.buttons;
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
    // Listed on screen; the user picks what to connect (see loopBtMenu).
    Serial.printf("[BT] Found: %-25s | MAC: %s\n", dev.name.c_str(), dev.mac.c_str());
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
  // The device list owns the screen until something is connected.
  if (!bt.connected())
    return;
  String track = FileSystem::tracks.empty() ? "No tracks found." : FileSystem::tracks[0].name;
  screen.show(track, player.volume(), bt.statusText());
}

// Index of the selected device in the current scan results (-1 if none).
int selectedDeviceIndex()
{
  const auto &devs = bt.seenDevices();
  for (size_t i = 0; i < devs.size(); ++i)
  {
    if (devs[i].mac == selectedMac)
      return (int)i;
  }
  return -1;
}

void setSelectedDevice(int index)
{
  const auto &devs = bt.seenDevices();
  if (devs.empty())
  {
    selectedMac = "";
    return;
  }
  if (index < 0)
    index = 0;
  if (index >= (int)devs.size())
    index = (int)devs.size() - 1;
  if (devs[(size_t)index].mac != selectedMac)
  {
    selectedMac = devs[(size_t)index].mac;
    Serial.printf("[BT] Selected: %s\n", devs[(size_t)index].name.c_str());
  }
}

// Pairing UI: scroll the scan list with the wheel or Up/Down, Center to
// connect. While connected the wheel goes to volume and Center
// disconnects back to the list.
void loopBtMenu()
{
  if (bt.connected())
  {
    player.addWheelMotion(wheelMotion);
    wheelMotion = 0;
    static uint8_t prevButtonsConnected = 0;
    uint8_t pressed = wheelButtons & ~prevButtonsConnected;
    prevButtonsConnected = wheelButtons;
    if (pressed & kBtnCenter)
    {
      Serial.println("[BT] Disconnect requested.");
      bt.disconnect();
    }
    return;
  }

  const auto &devs = bt.seenDevices();
  int sel = selectedDeviceIndex();
  if (sel < 0)
    sel = 0;
  setSelectedDevice(sel);
  sel = selectedDeviceIndex();

  // Wheel scroll: one entry per 8 motion units, remainder kept.
  listScrollRemainder += wheelMotion;
  wheelMotion = 0;
  int steps = listScrollRemainder / 8;
  if (steps != 0)
  {
    listScrollRemainder -= steps * 8;
    setSelectedDevice(sel + steps);
    sel = selectedDeviceIndex();
  }

  // Button scroll + confirm (rising edges only).
  static uint8_t prevButtons = 0;
  uint8_t pressed = wheelButtons & ~prevButtons;
  prevButtons = wheelButtons;
  if (pressed & kBtnUp)
    setSelectedDevice(sel - 1);
  if (pressed & kBtnDown)
    setSelectedDevice(sel + 1);
  sel = selectedDeviceIndex();
  if ((pressed & kBtnCenter) && sel >= 0)
  {
    Serial.printf("[BT] Connecting to %s (%s)...\n",
                  devs[(size_t)sel].name.c_str(), devs[(size_t)sel].mac.c_str());
    bt.connectByMac(devs[(size_t)sel].mac);
  }

  screen.showDevices(devs, sel, bt.statusText());
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
  loopBtMenu();
  loopDisplay();
  loopSerialCommands();
  vTaskDelay(1);
}
