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
// Pairing model (per KCX reference behavior): the module keeps an
// auto-link table (up to 10 MACs). With a non-empty table it connects
// only to listed devices; with an empty table it takes the first
// device found. There is no direct-dial command, so "connect" means
// "store in the table and (re)scan". Tracks connection state so the
// UI can show it without extra wiring.
class KcxController
{
public:
  // Fake table entry that matches no real device. A non-empty auto-link
  // table makes the module link ONLY listed devices, so keeping this
  // sentinel stored gates promiscuous first-found auto-connects while
  // the user picks from the scan list.
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
  // silently dropped link (headphones walked away) is noticed.
  void update();
  // Pump traffic for ms milliseconds (lets multi-line answers arrive
  // before the next command: the module handles one command at a time).
  void pump(unsigned long ms);

  void sendCommand(const String &cmd);
  void requestVersion();
  // Ask the module for its auto-link table (answers parsed into
  // linkedMacs()).
  void queryLinks();
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
  // Consecutive disagreeing STATUS polls. A single poll can sample the
  // link mid-transition (stale), so only repeated agreement flips the
  // state. Real CONNECT/DISCONNECT lines always act immediately.
  uint8_t statusMismatch_ = 0;
  String peerName_;
  String lastFoundName_;
  unsigned long lastStatusPoll_ = 0;

  void parseLine(const String &line);
  void setConnected(bool connected, const String &detail);
};
