#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

struct BtDevice
{
  String name;
  String mac; // Formatted with colons, e.g. "ab:b0:49:ed:c2:50".
  unsigned long lastSeen = 0; // millis() of the last scan sighting.
};

// Driver for the KCX BT emitter module (AT commands over UART).
// Link model (manufacturer manual + reference behavior):
// - The module links table entries (MEM_MacAdd) on scan sightings.
// - It fast-relinks the Auto_link_Add ("last connected") device,
//   e.g. on its own boot, without pairing mode.
// - It holds the link independently of the ESP32.
// Consequences honored here: the table is never wiped (persist!),
// Auto_link state is never disturbed (no blind rescans on drops),
// and an ESP reboot syncs with the live state instead of tearing it
// down. Tracks connection state so the UI can show it.
class KcxController
{
public:
  // Fake table entry that matches no real device. Stored only when the
  // table would otherwise be empty, so a fresh module cannot grab the
  // first device found before the user picks anything.
  static constexpr const char *kSentinelMac = "deadbeefcafe";

  using DeviceCallback = std::function<void(const BtDevice &)>;
  using StatusCallback = std::function<void(bool, const String &)>;

  KcxController(uint8_t rxPin, uint8_t txPin);

  void begin(uint32_t baud = 115200);
  // Pings the module until it answers OK+, then asks for its version.
  // The module needs a few seconds after power-on before real commands
  // work (early ones die silently or with CMD ERR), so boot waits here.
  // Returns true once the module answered both.
  bool waitReady(unsigned long timeoutMs = 8000);
  bool ready() const { return ready_; }
  // Pumps UART traffic; also polls link status every few seconds so a
  // silently dropped link (headphones walked away) is noticed. The
  // poll only runs after setPolling(true) (end of setup), so paced
  // setup commands never collide with it.
  void update();
  void setPolling(bool on);
  // Pump traffic for ms milliseconds (lets multi-line answers arrive
  // before the next command: the module handles one command at a time).
  void pump(unsigned long ms);

  void sendCommand(const String &cmd);
  void requestVersion();
  // Ask the module for its auto-link table (answers parsed into
  // linkedMacs()).
  void queryLinks();
  // Reboot the module (answers OK+RESET, POWER ON). On its boot it
  // fast-relinks remembered devices without pairing mode.
  void resetModule();
  // Disconnect + rescan for devices.
  void startScan();
  void disconnect();
  // Store the device in the module's auto-link table without asking it
  // to link right now. No-op when already stored.
  void storeDevice(const String &macNoColons);
  // Store the device (unless known) and wait for the module to link it
  // on sight. No extra scan is kicked: the module scans continuously.
  void connectByMac(const String &rawMac);
  void connectByName(const String &name);
  // Forgets all auto-link pairings (module stops auto-reconnecting).
  void clearPairings();
  // Drops scan entries not re-seen for maxAgeMs. Returns dropped count.
  // The module re-reports visible devices, so only gone ones vanish.
  size_t pruneDevices(unsigned long maxAgeMs);

  void onDeviceFound(DeviceCallback cb);
  void onConnectionChange(StatusCallback cb);

  // UI-facing state.
  bool connected() const { return connected_; }
  bool connectPending() const { return connectPending_; }
  const String &peerName() const { return peerName_; }
  const std::vector<BtDevice> &seenDevices() const { return seen_; }
  size_t seenDeviceCount() const { return seen_.size(); }
  // Auto-link table as plain hex MACs without colons.
  const std::vector<String> &linkedMacs() const { return linkedMacs_; }
  bool isLinked(const String &macNoColons) const;
  // True when the table holds anything but the sentinel: remembered
  // devices worth trying to relink (e.g. via resetModule()).
  bool hasRealEntries() const;
  // Drop all scan sightings (fresh list); the module keeps scanning
  // and its auto-link state on its own.
  void clearSeen();
  String statusText() const;

private:
  static constexpr unsigned long kStatusPollMs = 5000;

  uint8_t rxPin_;
  uint8_t txPin_;
  HardwareSerial serial_;
  String rxBuffer_;
  std::vector<BtDevice> seen_;
  std::vector<String> linkedMacs_;
  DeviceCallback onDeviceFound_ = nullptr;
  StatusCallback onStatusChange_ = nullptr;

  bool connected_ = false;
  bool connectPending_ = false;
  bool ready_ = false;
  bool versionSeen_ = false;
  bool polling_ = false;
  // Consecutive disagreeing STATUS polls. A single poll can sample the
  // link mid-transition (stale), so only repeated agreement flips the
  // state. Real CONNECT/DISCONNECT lines always act immediately.
  uint8_t statusMismatch_ = 0;
  // millis() of the last event-driven link-up. STATUS:0 readings inside
  // the grace window are ignored: the register lags a fresh link by
  // many seconds and would otherwise flap the UI straight back down.
  unsigned long lastLinkUpMs_ = 0;
  static constexpr unsigned long kLinkUpGraceMs = 15000;
  String peerName_;
  String lastFoundName_;
  unsigned long lastStatusPoll_ = 0;

  void parseLine(const String &line);
  void setConnected(bool connected, const String &detail);
  // Records a scan sighting (new or refresh); fires onDeviceFound once
  // per previously unseen MAC.
  void rememberSighting(const String &name, const String &formattedMac);
};
