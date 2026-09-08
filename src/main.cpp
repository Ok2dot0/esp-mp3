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

// Pairing UI state: raw wheel motion, consumed by loopBtMenu().
// When connected (or BT is off) the wheel drives volume instead.
int wheelMotion = 0;
String selectedMac;
int listScrollRemainder = 0;

// Bluetooth master switch. Off = idle: link dropped, list UI hidden,
// module left alone. Toggled with a long BOOT press.
bool btEnabled = true;

enum class BootPress
{
  None,
  Short, // press + release
  Long   // held >= 1.5 s
};

// Debounced BOOT button (active LOW). Poll every loop; each press
// reports exactly once, on release (Short) or on hold timeout (Long).
BootPress pollBootButton()
{
  constexpr unsigned long kDebounceMs = 40;
  constexpr unsigned long kLongMs = 1500;
  static bool lastLevel = HIGH;
  static unsigned long changeAt = 0;
  static bool armed = false;
  static bool longFired = false;
  bool level = digitalRead(Pins::BTN_BOOT);
  unsigned long now = millis();
  if (level != lastLevel)
  {
    lastLevel = level;
    changeAt = now;
  }
  if (level == HIGH)
  {
    BootPress press = (armed && !longFired) ? BootPress::Short : BootPress::None;
    armed = false;
    longFired = false;
    return press;
  }
  if (now - changeAt < kDebounceMs)
    return BootPress::None;
  armed = true;
  if (!longFired && now - changeAt >= kLongMs)
  {
    longFired = true;
    return BootPress::Long;
  }
  return BootPress::None;
}

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
    Serial.printf("[Wheel] %s pos=%3u\n",
                  state.touching ? "TOUCH" : "FREE ",
                  state.position);
  });

  wheel.begin(clickWheelISR);
  Serial.println("[Wheel] Click wheel listener started.");
}

void setupButtons()
{
  pinMode(Pins::BTN_BOOT, INPUT_PULLUP);
}

void setupBluetooth()
{  bt.onDeviceFound([](const BtDevice &dev)
  {
    // Listed on screen; the user picks what to connect (see loopBtMenu).
    Serial.printf("[BT] Found: %-25s | MAC: %s\n", dev.name.c_str(), dev.mac.c_str());
  });

  bt.onConnectionChange([](bool connected, const String &)
  {
    if (connected)
    {
      Serial.println("[BT] Status: Connected to audio sink.");
    }
    else
    {
      // Link lost: drop the scan list with it (entries would be ghosts
      // otherwise). Deliberately NO rescan here: the module keeps
      // scanning and its auto-link state on its own, and a blind PAIR
      // could disturb a fast relink in progress.
      Serial.println("[BT] Status: Disconnected.");
      selectedMac = "";
      bt.clearSeen();
    }
  });

  bt.begin();
  Serial.println("[BT] Waiting for module...");
  if (bt.waitReady())
    Serial.println("[BT] Module ready.");
  else
    Serial.println("[BT] Module not answering, continuing anyway.");
  // Pairings persist in module flash across ESP reboots on purpose:
  // the module holds (and re-links) remembered devices by itself.
  // Only a factory-fresh, empty table gets the sentinel so it cannot
  // grab the first device found before the user picks anything.
  // Pacing matters throughout: the module handles one command at a time.
  bt.queryLinks();
  bt.pump(1000);
  if (bt.linkedMacs().empty())
  {
    Serial.println("[BT] Table empty, storing sentinel guard.");
    bt.storeDevice(KcxController::kSentinelMac);
    bt.pump(600);
  }
  else
  {
    Serial.printf("[BT] Table holds %u device(s).\n", (unsigned)bt.linkedMacs().size());
  }
  // Sync with a link the module may still hold (an ESP reboot does not
  // drop it): two paced polls let the debounce heal the flag.
  bt.sendCommand("AT+STATUS?");
  bt.pump(400);
  bt.sendCommand("AT+STATUS?");
  bt.pump(400);
  if (!bt.connected() && bt.hasRealEntries())
  {
    // Down but remembered devices exist: reboot the module so IT
    // fast-relinks them without pairing mode, then re-sync.
    Serial.println("[BT] Rebooting module to trigger relink...");
    bt.resetModule();
    bt.sendCommand("AT+STATUS?");
    bt.pump(400);
    bt.sendCommand("AT+STATUS?");
    bt.pump(400);
  }
  // Rescan only when actually down, never blindly: the module keeps
  // scanning and its auto-link state on its own.
  if (!bt.connected())
  {
    Serial.println("[BT] Starting scan...");
    bt.startScan();
  }
  else
  {
    Serial.println("[BT] Link still up, skipping rescan.");
  }
  bt.setPolling(true);
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
  // The device list owns the screen while pairing with BT on.
  if (btEnabled && !bt.connected())
    return;
  String track = FileSystem::tracks.empty() ? "No tracks found." : FileSystem::tracks[0].name;
  String btline = bt.connected() ? bt.statusText() : String("BT: off");
  screen.show(track, player.volume(), btline);
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

// Bluetooth UI: scroll the scan list with the wheel, short BOOT to
// connect/disconnect, long BOOT to switch pairing on/off (idle).
// The wheel drives volume while connected or while BT is off.
void loopBtMenu()
{
  bool linked = bt.connected();
  if (!btEnabled || linked)
  {
    player.addWheelMotion(wheelMotion);
    wheelMotion = 0;
  }

  BootPress press = pollBootButton();
  if (press == BootPress::Long)
  {
    btEnabled = !btEnabled;
    if (!btEnabled)
    {
      Serial.println("[BTN] Long press: Bluetooth off (idle).");
      bt.disconnect();
    }
    else
    {
      Serial.println("[BTN] Long press: Bluetooth on, scanning.");
      bt.startScan();
    }
  }

  if (linked)
  {
    if (press == BootPress::Short)
    {
      Serial.println("[BTN] BOOT pressed while connected.");
      // Note: the module re-links remembered devices by itself, so it
      // may come straight back if the peer is still around.
      Serial.println("[BT] Disconnect requested.");
      bt.disconnect();
    }
    return;
  }

  if (!btEnabled)
    return; // Idle: screen stays on now-playing (see loopDisplay).

  const auto &devs = bt.seenDevices();
  // Forget devices gone quiet: the module re-reports visible ones about
  // every ~15-20 s, so only switched-off/out-of-range entries vanish
  // (no ghosts). Margin kept wide so the selection never flickers.
  size_t dropped = bt.pruneDevices(30000);
  if (dropped > 0)
    Serial.printf("[BT] Forgot %u stale device(s).\n", (unsigned)dropped);
  int sel = selectedDeviceIndex();
  if (sel < 0)
    sel = 0;
  setSelectedDevice(sel);
  sel = selectedDeviceIndex();

  // Wheel scroll: one entry per 4 motion units, remainder kept.
  listScrollRemainder += wheelMotion;
  wheelMotion = 0;
  int steps = listScrollRemainder / 4;
  if (steps != 0)
  {
    listScrollRemainder -= steps * 4;
    setSelectedDevice(sel + steps);
    sel = selectedDeviceIndex();
  }

  if (press == BootPress::Short)
  {
    if (sel >= 0)
    {
      Serial.printf("[BTN] BOOT pressed, sel=%d.\n", sel);
      Serial.printf("[BT] Connecting to %s (%s)...\n",
                    devs[(size_t)sel].name.c_str(), devs[(size_t)sel].mac.c_str());
      bt.connectByMac(devs[(size_t)sel].mac);
    }
    else
    {
      // Nothing to pick: kick a fresh scan instead of idling.
      Serial.println("[BTN] BOOT pressed on empty list, rescanning.");
      bt.startScan();
    }
  }

  screen.showDevices(devs, sel, bt.statusText(), bt.linkedMacs());
}

void loopSerialCommands()
{
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0)
    {
      if (cmd == "state")
      {
        String track = FileSystem::tracks.empty() ? "-" : FileSystem::tracks[0].name;
        Serial.printf("[STATE] bt=%s/%s peer='%s' pending=%d seen=%u linked=%u sel='%s'\n",
                      btEnabled ? "ON" : "OFF", bt.connected() ? "UP" : "DOWN",
                      bt.peerName().c_str(),
                      (int)bt.connectPending(), (unsigned)bt.seenDeviceCount(),
                      (unsigned)bt.linkedMacs().size(), selectedMac.c_str());
        Serial.printf("[STATE] vol=%d track='%s'\n", player.volume(), track.c_str());
        for (const auto &m : bt.linkedMacs())
          Serial.printf("[STATE] table: %s\n", m.c_str());
        return;
      }
      if (cmd == "tracks")
      {
        Serial.printf("[FS] %u track(s):\n", (unsigned)FileSystem::tracks.size());
        for (size_t i = 0; i < FileSystem::tracks.size(); ++i)
          Serial.printf("[FS] %3u %s\n", (unsigned)i, FileSystem::tracks[i].path.c_str());
        return;
      }
      bt.sendCommand(cmd);
    }
  }
}

} // namespace

void setup()
{
  setupSerial();
  screen.begin();
  screen.message("Starting...", "Mounting SD");
  player.begin(Pins::DAC_BCK, Pins::DAC_WS, Pins::DAC_DIN);
  setupFileSystem();
  screen.message("Starting...", "Waiting for BT");
  setupClickWheel();
  setupButtons();
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
