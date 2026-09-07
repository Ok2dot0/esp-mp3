#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

struct BtDevice
{
  String name;
  String mac;
};

// Driver for the KCX BT emitter module (AT commands over UART).
// Tracks connection state so the UI can show it without extra wiring.
class KcxController
{
public:
  using DeviceCallback = std::function<void(const BtDevice &)>;
  using StatusCallback = std::function<void(bool, const String &)>;

  KcxController(uint8_t rxPin, uint8_t txPin);

  void begin(uint32_t baud = 115200);
  void update();

  void sendCommand(const String &cmd);
  void requestVersion();
  void startScan(bool clearMemory = false);
  void disconnect();
  void connectByMac(const String &rawMac);
  void connectByName(const String &name);

  void onDeviceFound(DeviceCallback cb);
  void onConnectionChange(StatusCallback cb);

  // UI-facing state.
  bool connected() const { return connected_; }
  bool connectPending() const { return connectPending_; }
  const String &peerName() const { return peerName_; }
  const std::vector<BtDevice> &seenDevices() const { return seen_; }
  size_t seenDeviceCount() const { return seen_.size(); }
  String statusText() const;

private:
  uint8_t rxPin_;
  uint8_t txPin_;
  HardwareSerial serial_;
  String rxBuffer_;
  std::vector<BtDevice> seen_;
  DeviceCallback onDeviceFound_ = nullptr;
  StatusCallback onStatusChange_ = nullptr;

  bool connected_ = false;
  bool connectPending_ = false;
  String peerName_;
  String lastFoundName_;

  void parseLine(const String &line);
};
