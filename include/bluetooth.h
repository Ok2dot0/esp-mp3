#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

#include "KCX_BT_Emitter.h"

struct BtDevice
{
  String name;
  String mac; // Formatted with colons, e.g. "ab:b0:49:ed:c2:50".
  unsigned long lastSeen = 0; // millis() of the last sighting.
};

// Thin adapter over schreibfaul1/ESP32-KCX-BT-EMITTER.
//
// Link model (manufacturer manual + library behavior):
// - The module links table entries on scan sightings and fast-relinks
//   the last device on its own boot.
// - Connection state comes from the LINK pin interrupt
//   (kcx_bt_status), not AT+STATUS? polling.
// - Scan results arrive as kcx_bt_scanItems JSON (last 3 sightings),
//   saved table arrives as kcx_bt_memItems JSON (up to 10 entries).
// - Empty table = module grabs the first device found, so a sentinel
//   guard entry is stored on factory-fresh modules until the user
//   picks something (see main.cpp setupBluetooth).
class BtService
{
public:
  // Fake table entry that matches no real device. Stored only when the
  // table would otherwise be empty, so a fresh module cannot grab the
  // first device found before the user picks anything.
  static constexpr const char *kSentinelMac = "deadbeefcafe";

  using DeviceCallback = std::function<void(const BtDevice &)>;
  using StatusCallback = std::function<void(bool, const String &)>;

  BtService(uint8_t rxPin, uint8_t txPin, uint8_t linkPin, uint8_t modeDummyPin);

  void begin();
  // Services the library (drains Serial2, runs the 1 s ticker jobs).
  // Call every loop().
  void loop();
  // Block until the module answers OK+ (or timeout). Returns true when
  // the library reported "KCX_BT_Emitter found".
  bool waitReady(unsigned long timeoutMs = 8000);
  bool ready() const { return ready_; }

  // Library callbacks land here (called from bluetooth.cpp weak fns).
  void handleInfo(const char *info, const char *val);
  void handleStatus(bool connected);
  void handleMemItems(const char *json);
  void handleScanItems(const char *json);
  void handleModeChanged(const char *mode);

  void sendCommand(const String &cmd);
  // Ask the module for its auto-link table (answers parsed into
  // savedDevices()/linkedMacs()).
  void queryLinks();
  // Reboot the module (POWER ON). On boot it fast-relinks remembered
  // devices without pairing mode.
  void resetModule();
  // Ask the module to (re)start discovery. When disconnected the module
  // scans by itself; this just kicks it after entering the BT view or
  // when the list is empty.
  void startScan();
  void disconnect();
  // Delete the whole auto-link table (the module has no single-entry
  // delete). Caller should re-store the sentinel guard afterwards.
  void deleteSaved();
  // Store the device in the module's auto-link table. No-op when
  // already stored. The module links it on sight by itself.
  void storeDevice(const String &macNoColons);
  // Store the device (unless known) and wait for the module to link it
  // on sight. No extra scan is kicked: the module scans continuously.
  void connectByMac(const String &rawMac);
  // Drops scan entries not re-seen for maxAgeMs. Returns dropped count.
  // The module re-reports visible devices, so only gone ones vanish.
  size_t pruneDevices(unsigned long maxAgeMs);

  void onDeviceFound(DeviceCallback cb);
  void onConnectionChange(StatusCallback cb);

  // Protocol log passthrough (last ~100 RX/TX lines). Returns nullptr
  // when elementNr is out of range.
  const char *protocolLine(uint16_t elementNr);
  void dumpProtocol() const;

  // UI-facing state.
  bool connected() const { return connected_; }
  bool connectPending() const { return connectPending_; }
  const String &peerName() const { return peerName_; }
  const String &mode() const { return mode_; }
  const String &version() const { return version_; }
  // Live scan sightings (accumulated from scan JSON, pruned by age).
  const std::vector<BtDevice> &seenDevices() const { return seen_; }
  size_t seenDeviceCount() const { return seen_.size(); }
  // Saved auto-link table as full devices (name may be empty when the
  // module only reported a MAC).
  const std::vector<BtDevice> &savedDevices() const { return saved_; }
  // Auto-link table as plain hex MACs without colons (compat helper).
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
  KCX_BT_Emitter emitter_;
  std::vector<BtDevice> seen_;
  std::vector<BtDevice> saved_;
  std::vector<String> linkedMacs_;
  DeviceCallback onDeviceFound_ = nullptr;
  StatusCallback onStatusChange_ = nullptr;

  bool connected_ = false;
  bool connectPending_ = false;
  bool ready_ = false;
  bool scanning_ = false;
  String peerName_;
  String version_;
  String mode_ = "TX";

  void setConnected(bool connected, const String &detail);
  void rememberSighting(const String &name, const String &formattedMac);
  void rebuildLinkedMacs();
  static String formatMac(const String &macNoColons);
  static String stripColonsLower(const String &mac);
  // Very small JSON parser for the library's fixed shapes:
  // [{"name":"..","addr":".."}, ...] and [{"addr":"..","name":".."}, ...].
  // Returns false when nothing parseable was found.
  bool parsePairs(const char *json, bool addrFirst,
                  std::vector<std::pair<String, String>> &out);
};

// Compatibility alias so display.h/main.cpp keep readable names.
using KcxController = BtService;
