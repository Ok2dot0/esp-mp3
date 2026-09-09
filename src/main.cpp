// esp-mp3: iPod-style ESP32-S3 music player.
//
// Modules (see include/ + src/):
//   pins         - central pin map
//   filesystem   - SD card + audio file discovery
//   bluetooth    - BtService adapter over ESP32-KCX-BT-EMITTER + state
//   display      - LCD screens (player, browser, bluetooth saved+found)
//   audio_player - I2S playback, volume, metadata
//
// Input is the BOOT button only (short/long press); the click wheel
// lives on the hw/clickwheel branch until the touchscreen arrives.
// main.cpp only owns the module instances and wires them together.

#include "Arduino.h"
#include "audio_player.h"
#include "bluetooth.h"
#include "display.h"
#include "filesystem.h"
#include "pins.h"

namespace
{

KcxController bt(Pins::KCX_RX, Pins::KCX_TX, Pins::KCX_LINK, Pins::KCX_MODE_DUMMY);
Screen screen;
AudioPlayer player;

// UI views, cycled with a long BOOT press.
enum class View
{
  Player,
  Tracks,
  Bt,
  Off
};
View view = View::Player;

// Playlist state: index of the playing track, cursor of the browser.
size_t currentTrack = 0;
size_t trackCursor = 0;

// Pairing UI state.
String selectedMac;

void playFileAt(size_t index);
void playNext();

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

void setupButtons()
{
  pinMode(Pins::BTN_BOOT, INPUT_PULLUP);
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
      playFileAt(0);
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

void setupBluetooth()
{
  // Pumps the library for ms milliseconds so queued commands and their
  // multi-line answers run to completion (one command at a time).
  auto pump = [](unsigned long ms)
  {
    unsigned long start = millis();
    while (millis() - start < ms)
    {
      bt.loop();
      delay(10);
    }
  };

  bt.onDeviceFound([](const BtDevice &dev)
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
  bt.sendCommand("AT+GMR?");
  bt.sendCommand("AT+BT_MODE?");
  // Pairings persist in module flash across ESP reboots on purpose:
  // the module holds (and re-links) remembered devices by itself.
  // Only a factory-fresh, empty table gets the sentinel so it cannot
  // grab the first device found before the user picks anything.
  // Pacing matters throughout: the library queues one command at a time.
  bt.queryLinks();
  pump(1500);
  // The mem JSON callback may need one more round trip on slow boots.
  if (bt.savedDevices().empty())
  {
    bt.queryLinks();
    pump(1000);
  }
  if (bt.savedDevices().empty())
  {
    Serial.println("[BT] Table empty, storing sentinel guard.");
    bt.storeDevice(KcxController::kSentinelMac);
    pump(600);
    bt.queryLinks();
    pump(1000);
  }
  else
  {
    Serial.printf("[BT] Table holds %u device(s).\n", (unsigned)bt.savedDevices().size());
  }
  // Sync with a link the module may still hold (an ESP reboot does not
  // drop it): the LINK pin heals the flag in bt.loop() within ~2 s.
  pump(2500);
  if (!bt.connected() && bt.hasRealEntries())
  {
    // Down but remembered devices exist: reboot the module so IT
    // fast-relinks them without pairing mode, then re-sync.
    Serial.println("[BT] Rebooting module to trigger relink...");
    bt.resetModule();
    pump(1500);
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
}

// Playlist: play index (wraps), advance on EOF via takeRepeat().
void playFileAt(size_t index)
{
  if (FileSystem::tracks.empty())
    return;
  currentTrack = index % FileSystem::tracks.size();
  trackCursor = currentTrack;
  const Track &t = FileSystem::tracks[currentTrack];
  player.playFile(t.path.c_str(), t.name.c_str());
}

void playNext()
{
  if (FileSystem::tracks.empty())
    return;
  playFileAt(currentTrack + 1);
}

void loopAudio()
{
  if (player.takeRepeat())
    playNext();
}

const char *viewName(View v)
{
  switch (v)
  {
  case View::Player: return "Player";
  case View::Tracks: return "Tracks";
  case View::Bt: return "BT";
  case View::Off: return "Off";
  }
  return "?";
}

void enterView(View v)
{
  view = v;
  Serial.printf("[UI] View: %s\n", viewName(v));
  if (v == View::Bt && !bt.connected())
  {
    Serial.println("[BT] Starting scan...");
    bt.startScan();
  }
  if (v == View::Off && bt.connected())
  {
    Serial.println("[BT] Leaving BT, disconnecting.");
    bt.disconnect();
  }
}

void loopDisplay()
{
  if (view == View::Bt && !bt.connected())
    return; // Device list owns the screen (see loopBtMenu).
  if (view == View::Tracks)
    return; // Track browser owns the screen (see loopTracks).
  String title = player.metaTitle();
  if (title.isEmpty())
    title = FileSystem::tracks.empty() ? "No tracks found." : FileSystem::tracks[currentTrack].name;
  String btline = (!bt.connected() && view == View::Off) ? String("BT: off") : bt.statusText();
  screen.showPlayer(title, player.metaArtist(), player.volume(), btline);
}

// Combined saved-then-scanned list backing the BT view. Saved entries
// stay visible even when out of range (the module links them on sight);
// scanned entries skip dupes of saved ones and the sentinel guard.
void combinedBtList(std::vector<BtDevice> &out)
{
  out.clear();
  auto plainOf = [](const String &mac)
  {
    String p = mac;
    p.replace(":", "");
    p.toLowerCase();
    return p;
  };
  for (const auto &d : bt.savedDevices())
  {
    if (plainOf(d.mac) == "deadbeefcafe")
      continue;
    out.push_back(d);
  }
  for (const auto &d : bt.seenDevices())
  {
    bool dupe = false;
    for (const auto &s : bt.savedDevices())
    {
      if (plainOf(s.mac) == plainOf(d.mac))
      {
        dupe = true;
        break;
      }
    }
    if (!dupe)
      out.push_back(d);
  }
}

// Index of the selected device in the combined list (-1 if none).
int selectedDeviceIndex()
{
  std::vector<BtDevice> all;
  combinedBtList(all);
  for (size_t i = 0; i < all.size(); ++i)
  {
    if (all[i].mac == selectedMac)
      return (int)i;
  }
  return -1;
}

void setSelectedDevice(int index)
{
  std::vector<BtDevice> all;
  combinedBtList(all);
  if (all.empty())
  {
    selectedMac = "";
    return;
  }
  if (index < 0)
    index = 0;
  if (index >= (int)all.size())
    index = (int)all.size() - 1;
  if (all[(size_t)index].mac != selectedMac)
  {
    selectedMac = all[(size_t)index].mac;
    Serial.printf("[BT] Selected: %s\n", all[(size_t)index].name.c_str());
  }
}

// Bluetooth view: SHORT connects (or rescans when empty).
void loopBtMenu(BootPress press)
{
  if (bt.connected())
    return; // Link owns the view; now-playing shows the peer name.

  // Forget devices gone quiet: the module re-reports visible ones, so
  // only switched-off/out-of-range entries vanish (no ghosts). Margin
  // kept wide so the selection never flickers.
  size_t dropped = bt.pruneDevices(30000);
  if (dropped > 0)
    Serial.printf("[BT] Forgot %u stale device(s).\n", (unsigned)dropped);
  std::vector<BtDevice> all;
  combinedBtList(all);
  int sel = selectedDeviceIndex();
  if (sel < 0)
    sel = 0;
  setSelectedDevice(sel);
  sel = selectedDeviceIndex();
  // Rebuild after setSelectedDevice clamped the cursor.
  combinedBtList(all);

  if (press == BootPress::Short)
  {
    if (sel >= 0 && (size_t)sel < all.size())
    {
      Serial.printf("[BTN] BOOT pressed, sel=%d.\n", sel);
      Serial.printf("[BT] Connecting to %s (%s)...\n",
                    all[(size_t)sel].name.c_str(), all[(size_t)sel].mac.c_str());
      bt.connectByMac(all[(size_t)sel].mac);
    }
    else
    {
      // Nothing to pick: kick a fresh scan instead of idling.
      Serial.println("[BTN] BOOT pressed on empty list, rescanning.");
      bt.startScan();
    }
  }

  screen.showBt(bt.savedDevices(), bt.seenDevices(), sel,
                bt.statusText(), bt.connected(), bt.peerName());
}

// Track browser view: SHORT plays the cursor and steps it forward,
// so repeated presses walk through the library.
void loopTracks(BootPress press)
{
  const auto &tracks = FileSystem::tracks;
  if (!tracks.empty() && trackCursor >= tracks.size())
    trackCursor = 0;
  if (press == BootPress::Short && !tracks.empty())
  {
    Serial.printf("[BTN] Playing track %u.\n", (unsigned)trackCursor);
    playFileAt(trackCursor);
    trackCursor = (trackCursor + 1) % tracks.size();
  }

  std::vector<String> labels;
  labels.reserve(tracks.size());
  for (size_t i = 0; i < tracks.size(); ++i)
    labels.push_back(String(i == currentTrack ? "> " : "  ") + tracks[i].name);
  String header = "Tracks " + String((unsigned)(tracks.empty() ? 0 : trackCursor + 1)) +
                  "/" + String((unsigned)tracks.size());
  screen.showTracks(labels, (int)trackCursor, header);
}

// Routes one BOOT press by view. LONG always cycles views.
void loopUi()
{
  BootPress press = pollBootButton();
  if (press == BootPress::Long)
  {
    View next = View::Player;
    if (view == View::Player)
      next = View::Tracks;
    else if (view == View::Tracks)
      next = View::Bt;
    else if (view == View::Bt)
      next = View::Off;
    enterView(next);
    return;
  }

  if (view == View::Tracks)
  {
    loopTracks(press);
    return;
  }
  if (view == View::Bt)
  {
    loopBtMenu(press);
    return;
  }
  // Player + Off views: SHORT skips, or disconnects an active link.
  if (press == BootPress::Short)
  {
    if (bt.connected())
    {
      // Note: the module re-links remembered devices by itself, so it
      // may come straight back if the peer is still around.
      Serial.println("[BT] Disconnect requested.");
      bt.disconnect();
    }
    else
    {
      playNext();
    }
  }
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
        String track = "-";
        if (!FileSystem::tracks.empty() && currentTrack < FileSystem::tracks.size())
          track = FileSystem::tracks[currentTrack].name;
        Serial.printf("[STATE] view=%s bt=%s peer='%s' pending=%d seen=%u saved=%u sel='%s' mode=%s ver=%s\n",
                      viewName(view), bt.connected() ? "UP" : "DOWN",
                      bt.peerName().c_str(),
                      (int)bt.connectPending(), (unsigned)bt.seenDeviceCount(),
                      (unsigned)bt.savedDevices().size(), selectedMac.c_str(),
                      bt.mode().c_str(), bt.version().c_str());
        Serial.printf("[STATE] vol=%d track=%u/%u '%s' meta='%s - %s'\n",
                      player.volume(), (unsigned)currentTrack,
                      (unsigned)FileSystem::tracks.size(), track.c_str(),
                      player.metaArtist().c_str(), player.metaTitle().c_str());
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
      if (cmd == "next")
      {
        playNext();
        return;
      }
      if (cmd.startsWith("play "))
      {
        playFileAt((size_t)cmd.substring(5).toInt());
        return;
      }
      if (cmd == "btp")
      {
        bt.dumpProtocol();
        return;
      }
      if (cmd == "btmem")
      {
        bt.queryLinks();
        return;
      }
      if (cmd == "btdel")
      {
        bt.deleteSaved();
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
  setupButtons();
  setupBluetooth();
}

void loop()
{
  player.update();
  bt.loop();
  loopAudio();
  loopUi();
  loopDisplay();
  loopSerialCommands();
  vTaskDelay(1);
}
