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

bool KcxController::waitReady(unsigned long timeoutMs)
{
  unsigned long start = millis();
  sendCommand("AT+");
  while (!ready_ && millis() - start < timeoutMs)
  {
    update();
    delay(50);
  }
  if (!ready_)
    return false;
  sendCommand("AT+GMR?");
  while (!versionSeen_ && millis() - start < timeoutMs)
  {
    update();
    delay(50);
  }
  return versionSeen_;
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
  Serial.printf("[KCX] >> %s\n", cmd.c_str());
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
    // Delete stored auto-link pairings so the module does not reconnect
    // to old devices by itself. This alone re-triggers the scan
    // (module answers Delete_Vmlink + SCAN), so no follow-up command:
    // sending one too fast only yields CMD ERR.
    sendCommand("AT+DELVMLINK");
    delay(500);
  }
  seen_.clear();
  connectPending_ = false;
}

void KcxController::clearPairings()
{
  sendCommand("AT+DELVMLINK");
  delay(500);
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
  sendCommand("AT+ADDLINKADD=" + cleanMac);
}

void KcxController::connectByName(const String &name)
{
  connectPending_ = true;
  sendCommand("AT+ADDLINKNAME=" + name);
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
  Serial.printf("[KCX] << %s\n", line.c_str());
  if (line == "OK+")
  {
    ready_ = true;
    return;
  }
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

    String formattedMac;
    for (size_t i = 0; i < mac.length(); ++i)
    {
      formattedMac += mac[i];
      if ((i % 2 == 1) && (i + 1 < mac.length()))
        formattedMac += ':';
    }

    // Compare formatted MACs: the stored entries keep colons.
    for (const auto &seen : seen_)
    {
      if (seen.mac == formattedMac)
        return;
    }
    seen_.push_back({name, formattedMac});

    if (onDeviceFound_)
      onDeviceFound_({name, formattedMac});
    return;
  }

  // Connect indications per the KCX protocol (see KCX_BT_Emitter lib):
  // "CONNECT", "CON ONE", "CON LAST", "CON MATCH ADD".
  if (line.startsWith("CONNECT") ||
      line.startsWith("CON ONE") ||
      line.startsWith("CON LAST") ||
      line.startsWith("CON MATCH") ||
      line.indexOf("CONNECTED") >= 0)
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
    versionSeen_ = true;
    Serial.printf("[KCX] Firmware: %s\n", line.c_str());
  }
}
