#include "bluetooth.h"

KcxController::KcxController(uint8_t rxPin, uint8_t txPin)
    : rxPin_(rxPin), txPin_(txPin), serial_(1) {}

void KcxController::begin(uint32_t baud)
{
  serial_.begin(baud, SERIAL_8N1, rxPin_, txPin_);
  delay(100);
  while (serial_.available())
    serial_.read();
}

void KcxController::update()
{
  while (serial_.available())
  {
    char c = static_cast<char>(serial_.read());

    if (c == '\r')
      continue;

    if (c == '\n')
    {
      rxBuffer_.trim();
      if (rxBuffer_.length() > 0)
        parseLine(rxBuffer_);
      rxBuffer_ = "";
    }
    else
    {
      rxBuffer_ += c;
    }
  }
}

void KcxController::sendCommand(const String &cmd)
{
  serial_.print(cmd + "\r\n");
}

void KcxController::requestVersion()
{
  sendCommand("AT+GMR?");
}

void KcxController::startScan(bool clearMemory)
{
  if (clearMemory)
  {
    sendCommand("AT+DELADD=ALL");
    delay(50);
  }
  seen_.clear();
  connectPending_ = false;
  sendCommand("AT+DISCON");
}

void KcxController::disconnect()
{
  sendCommand("AT+DISCON");
}

void KcxController::connectByMac(const String &rawMac)
{
  String cleanMac = rawMac;
  cleanMac.replace(":", "");
  connectPending_ = true;
  sendCommand("AT+CONADD=" + cleanMac);
}

void KcxController::connectByName(const String &name)
{
  sendCommand("AT+CONNAME=" + name);
}

void KcxController::onDeviceFound(DeviceCallback cb)
{
  onDeviceFound_ = cb;
}

void KcxController::onConnectionChange(StatusCallback cb)
{
  onStatusChange_ = cb;
}

String KcxController::statusText() const
{
  if (connected_)
    return "BT: " + peerName_;
  if (connectPending_)
    return "BT: connecting...";
  if (!seen_.empty())
    return "BT: scan (" + String(seen_.size()) + " found)";
  return "BT: scanning...";
}

void KcxController::parseLine(const String &line)
{
  if (line.indexOf("MacAdd:") >= 0 && line.indexOf("Name:") >= 0)
  {
    int macIdx = line.indexOf("MacAdd:");
    int nameIdx = line.indexOf("Name:");

    String mac = line.substring(macIdx + 7, nameIdx);
    mac.replace(",", "");
    mac.trim();

    String name = line.substring(nameIdx + 5);
    name.trim();
    lastFoundName_ = name;

    for (const auto &seen : seen_)
    {
      if (seen.mac == mac)
        return;
    }

    String formattedMac;
    for (size_t i = 0; i < mac.length(); ++i)
    {
      formattedMac += mac[i];
      if ((i % 2 == 1) && (i + 1 < mac.length()))
        formattedMac += ':';
    }
    seen_.push_back({name, formattedMac});

    if (onDeviceFound_)
      onDeviceFound_({name, formattedMac});
    return;
  }

  if (line.indexOf("CONNECTED") >= 0 ||
      line.indexOf("CON MATCH") >= 0 ||
      line.startsWith("CONNECT=>"))
  {
    connected_ = true;
    connectPending_ = false;
    if (!lastFoundName_.isEmpty())
      peerName_ = lastFoundName_;
    if (onStatusChange_)
      onStatusChange_(true, line);
    return;
  }

  if (line.indexOf("DISCONNECT") >= 0 || line == "OK+DISCON")
  {
    connected_ = false;
    connectPending_ = false;
    if (onStatusChange_)
      onStatusChange_(false, line);
    return;
  }

  if (line.indexOf("OK+VERS:") >= 0)
  {
    Serial.printf("[KCX] Firmware: %s\n", line.c_str());
  }
}
