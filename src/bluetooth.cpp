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

  // Poll the link state so a silently dropped link (device walked away,
  // no UART event) is noticed within seconds. Gated until setup is done
  // so paced setup commands never collide with it.
  if (polling_ && ready_ && millis() - lastStatusPoll_ > kStatusPollMs)
  {
    lastStatusPoll_ = millis();
    sendCommand("AT+STATUS?");
  }
}

void KcxController::setPolling(bool on)
{
  polling_ = on;
  lastStatusPoll_ = millis();
}

void KcxController::pump(unsigned long ms)
{
  unsigned long start = millis();
  while (millis() - start < ms)
  {
    update();
    delay(10);
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

void KcxController::queryLinks()
{
  sendCommand("AT+VMLINK?");
}

void KcxController::startScan()
{
  // AT+PAIR drops any link and (re)starts discovery (answers OK+PAIR,
  // then SCAN... and MacAdd lines). No memory wipe: the auto-link table
  // decides what the module may connect to.
  seen_.clear();
  connectPending_ = false;
  sendCommand("AT+PAIR");
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
  cleanMac.toLowerCase();
  connectPending_ = true;
  if (!isLinked(cleanMac))
    sendCommand("AT+ADDLINKADD=" + cleanMac);
  // No scan kick: the module scans continuously and links stored
  // devices on sight. Pacing matters (one command at a time).
}

void KcxController::storeDevice(const String &macNoColons)
{
  String cleanMac = macNoColons;
  cleanMac.toLowerCase();
  if (!isLinked(cleanMac))
    sendCommand("AT+ADDLINKADD=" + cleanMac);
}

size_t KcxController::pruneDevices(unsigned long maxAgeMs)
{
  size_t before = seen_.size();
  unsigned long now = millis();
  for (auto it = seen_.begin(); it != seen_.end();)
  {
    if (now - it->lastSeen > maxAgeMs)
      it = seen_.erase(it);
    else
      ++it;
  }
  return before - seen_.size();
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

bool KcxController::isLinked(const String &macNoColons) const
{
  String needle = macNoColons;
  needle.toLowerCase();
  for (const auto &mac : linkedMacs_)
  {
    if (mac == needle)
      return true;
  }
  return false;
}

void KcxController::setConnected(bool connected, const String &detail)
{
  if (connected_ == connected)
    return;
  connected_ = connected;
  connectPending_ = false;
  if (onStatusChange_)
    onStatusChange_(connected, detail);
}

String KcxController::statusText() const
{
  if (connected_)
    return "BT: " + (peerName_.isEmpty() ? String("connected") : peerName_);
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
    // Re-sightings refresh the timestamp so present devices survive
    // pruning while gone ones expire.
    for (auto &seen : seen_)
    {
      if (seen.mac == formattedMac)
      {
        seen.lastSeen = millis();
        if (seen.name != name)
          seen.name = name;
        return;
      }
    }
    seen_.push_back({name, formattedMac, millis()});

    if (onDeviceFound_)
      onDeviceFound_({name, formattedMac});
    return;
  }

  // Auto-link table entries, e.g. "MEM_MacAdd 00:abb049edc250".
  if (line.startsWith("MEM_MacAdd"))
  {
    int colon = line.lastIndexOf(':');
    if (colon > 0)
    {
      String mac = line.substring(colon + 1);
      mac.trim();
      mac.toLowerCase();
      if (!mac.isEmpty() && !isLinked(mac))
      {
        linkedMacs_.push_back(mac);
        Serial.printf("[BT] Paired device remembered: %s\n", mac.c_str());
      }
    }
    return;
  }

  // Last auto-linked device, e.g. "Auto_link_Add:abb049edc250" (or null).
  // Counts as a table entry: the module re-links it by itself.
  if (line.startsWith("Auto_link_Add:"))
  {
    String mac = line.substring(14);
    mac.trim();
    mac.toLowerCase();
    if (!mac.isEmpty() && mac != "null" && !isLinked(mac))
    {
      linkedMacs_.push_back(mac);
      Serial.printf("[BT] Paired device remembered: %s\n", mac.c_str());
    }
    return;
  }

  // Link poll answers, e.g. "OK+STATUS:1". Debounced: one poll can
  // catch the link mid-transition, so two in a row must agree before
  // the state flips. CONNECT/DISCONNECT lines bypass this entirely.
  // Fresh links also get a grace window: the register keeps reading 0
  // for many seconds after a real link-up.
  if (line.startsWith("OK+STATUS:"))
  {
    bool up = line.endsWith("1");
    if (up == connected_)
    {
      statusMismatch_ = 0;
    }
    else if (!up && millis() - lastLinkUpMs_ < kLinkUpGraceMs)
    {
      // Ignore: link just came up, register hasn't caught up yet.
    }
    else if (++statusMismatch_ >= 2)
    {
      statusMismatch_ = 0;
      setConnected(up, line);
    }
    return;
  }

  // Connect indications per the KCX protocol (see KCX_BT_Emitter lib):
  // "CONNECT", "CON ONE", "CON LAST", "CON MATCH ADD", "CON:0x...".
  if (line.startsWith("CONNECT") ||
      line.startsWith("CON ONE") ||
      line.startsWith("CON LAST") ||
      line.startsWith("CON MATCH") ||
      line.startsWith("CON:") ||
      line.indexOf("CONNECTED") >= 0)
  {
    statusMismatch_ = 0;
    lastLinkUpMs_ = millis();
    if (!lastFoundName_.isEmpty())
      peerName_ = lastFoundName_;
    setConnected(true, line);
    return;
  }

  if (line.indexOf("DISCONNECT") >= 0 || line == "OK+DISCON")
  {
    statusMismatch_ = 0;
    setConnected(false, line);
    return;
  }

  if (line.indexOf("OK+VERS:") >= 0)
  {
    versionSeen_ = true;
    Serial.printf("[KCX] Firmware: %s\n", line.c_str());
  }
}
