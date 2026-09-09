#include "bluetooth.h"

namespace
{

BtService *s_instance = nullptr;

String formatMacImpl(const String &macNoColons)
{
  String formatted;
  formatted.reserve(macNoColons.length() + 5);
  for (size_t i = 0; i < macNoColons.length(); ++i)
  {
    formatted += macNoColons[i];
    if ((i % 2 == 1) && (i + 1 < macNoColons.length()))
      formatted += ':';
  }
  return formatted;
}

} // namespace

// Library weak callbacks: forward to the owning BtService instance.
void kcx_bt_info(const char *info, const char *val)
{
  Serial.printf("[KCX] %s %s\n", info ? info : "", val ? val : "");
  if (s_instance)
    s_instance->handleInfo(info, val);
}

void kcx_bt_status(bool status)
{
  if (s_instance)
    s_instance->handleStatus(status);
}

void kcx_bt_memItems(const char *jsonItems)
{
  Serial.printf("[BT] mem: %s\n", jsonItems ? jsonItems : "null");
  if (s_instance)
    s_instance->handleMemItems(jsonItems);
}

void kcx_bt_scanItems(const char *jsonItems)
{
  Serial.printf("[BT] scan: %s\n", jsonItems ? jsonItems : "null");
  if (s_instance)
    s_instance->handleScanItems(jsonItems);
}

void kcx_bt_modeChanged(const char *m)
{
  Serial.printf("[BT] mode: %s\n", m ? m : "?");
  if (s_instance)
    s_instance->handleModeChanged(m);
}

BtService::BtService(uint8_t rxPin, uint8_t txPin, uint8_t linkPin, uint8_t modeDummyPin)
    : emitter_(rxPin, txPin, linkPin, modeDummyPin)
{
}

void BtService::begin()
{
  s_instance = this;
  emitter_.begin();
  // Kick the alive check; the library answers via kcx_bt_info("KCX_BT_Emitter found").
  emitter_.userCommand("AT+");
}

void BtService::loop()
{
  emitter_.loop();
  // Keep the ISR-driven flag honest: if the LINK pin disagrees with the
  // cached state for a while (missed edge), heal it here. The library
  // only notifies on edges, so this covers e.g. a link that came up
  // while the ESP was rebooting.
  static unsigned long lastHeal = 0;
  if (millis() - lastHeal > 2000)
  {
    lastHeal = millis();
    bool pinUp = emitter_.isConnected();
    if (pinUp != connected_)
      setConnected(pinUp, pinUp ? "LINK pin HIGH" : "LINK pin LOW");
  }
}

bool BtService::waitReady(unsigned long timeoutMs)
{
  unsigned long start = millis();
  while (!ready_ && millis() - start < timeoutMs)
  {
    loop();
    delay(50);
  }
  return ready_;
}

void BtService::handleInfo(const char *info, const char *val)
{
  if (!info)
    return;
  String i(info);
  if (i.indexOf("KCX_BT_Emitter found") >= 0)
  {
    ready_ = true;
    return;
  }
  if (i.startsWith("Version"))
  {
    if (val)
      version_ = String(val);
    return;
  }
  if (i.startsWith("Status ->"))
  {
    scanning_ = (val && String(val).indexOf("Scan") >= 0);
    return;
  }
  if (i.startsWith("scanned:"))
  {
    // Per-device line, e.g. "MacAdd:ab..,Name:Foo". The JSON callback
    // below carries the same data; parse it there instead.
    return;
  }
}

void BtService::handleStatus(bool connected)
{
  scanning_ = false;
  setConnected(connected, connected ? "LINK up" : "LINK down");
}

void BtService::handleMemItems(const char *json)
{
  if (!json)
    return;
  std::vector<std::pair<String, String>> pairs; // (name, addr)
  if (!parsePairs(json, false, pairs))
    return;
  saved_.clear();
  for (auto &p : pairs)
  {
    String addr = stripColonsLower(p.second);
    if (addr.isEmpty())
      continue;
    // The library pads the table to 10 slots with empty entries; skip them.
    if (p.first.isEmpty() && addr.isEmpty())
      continue;
    saved_.push_back({p.first, formatMac(addr), millis()});
  }
  rebuildLinkedMacs();
}

void BtService::handleScanItems(const char *json)
{
  if (!json)
    return;
  std::vector<std::pair<String, String>> pairs; // (name, addr)
  // Scan JSON is addr-first: [{"addr":"..","name":".."}].
  if (!parsePairs(json, true, pairs))
    return;
  for (auto &p : pairs)
  {
    String addr = stripColonsLower(p.second);
    if (addr.length() < 12)
      continue;
    rememberSighting(p.first, formatMac(addr));
  }
}

void BtService::handleModeChanged(const char *m)
{
  if (m)
    mode_ = String(m);
}

void BtService::sendCommand(const String &cmd)
{
  Serial.printf("[KCX] >> %s\n", cmd.c_str());
  emitter_.userCommand(cmd.c_str());
}

void BtService::queryLinks()
{
  emitter_.getVMlinks();
}

void BtService::resetModule()
{
  sendCommand("AT+RESET");
  // POWER ON + first SCAN lines arrive over the next seconds; pump the
  // library so the queue and callbacks run while we wait.
  unsigned long start = millis();
  while (millis() - start < 2500)
  {
    loop();
    delay(10);
  }
}

void BtService::startScan()
{
  // AT+PAIR drops any link and (re)starts discovery. No memory wipe:
  // the auto-link table decides what the module may connect to.
  sendCommand("AT+PAIR");
}

void BtService::disconnect()
{
  sendCommand("AT+DISCON");
}

void BtService::deleteSaved()
{
  emitter_.deleteVMlinks();
}

void BtService::storeDevice(const String &macNoColons)
{
  String cleanMac = stripColonsLower(macNoColons);
  if (cleanMac.isEmpty() || isLinked(cleanMac))
    return;
  emitter_.addLinkAddr(cleanMac.c_str());
}

void BtService::connectByMac(const String &rawMac)
{
  String cleanMac = stripColonsLower(rawMac);
  if (cleanMac.isEmpty())
    return;
  connectPending_ = true;
  if (!isLinked(cleanMac))
    emitter_.addLinkAddr(cleanMac.c_str());
  // No scan kick: the module scans continuously and links stored
  // devices on sight. The queue paces one command at a time.
}

size_t BtService::pruneDevices(unsigned long maxAgeMs)
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

void BtService::onDeviceFound(DeviceCallback cb)
{
  onDeviceFound_ = cb;
}

void BtService::onConnectionChange(StatusCallback cb)
{
  onStatusChange_ = cb;
}

const char *BtService::protocolLine(uint16_t elementNr)
{
  return emitter_.list_protokol(elementNr);
}

void BtService::dumpProtocol() const
{
  // list_protokol is non-const in the library; const_cast is safe here
  // (read-only access to the ring buffer).
  auto *self = const_cast<BtService *>(this);
  uint16_t i = 0;
  while (const char *line = self->emitter_.list_protokol(i))
  {
    Serial.printf("[KCX proto] %s\n", line);
    ++i;
  }
}

bool BtService::isLinked(const String &macNoColons) const
{
  String needle = stripColonsLower(macNoColons);
  for (const auto &mac : linkedMacs_)
  {
    if (mac == needle)
      return true;
  }
  return false;
}

bool BtService::hasRealEntries() const
{
  for (const auto &mac : linkedMacs_)
  {
    if (!mac.equals(kSentinelMac))
      return true;
  }
  return false;
}

void BtService::clearSeen()
{
  seen_.clear();
}

void BtService::setConnected(bool connected, const String &detail)
{
  if (connected_ == connected)
    return;
  connected_ = connected;
  connectPending_ = false;
  (void)detail;
  if (connected)
    Serial.println("[BT] Status: Connected to audio sink.");
  else
  {
    Serial.println("[BT] Status: Disconnected.");
    peerName_ = "";
  }
  if (onStatusChange_)
    onStatusChange_(connected, detail);
}

String BtService::statusText() const
{
  if (connected_)
    return "BT: " + (peerName_.isEmpty() ? String("connected") : peerName_);
  if (connectPending_)
    return "BT: connecting...";
  if (scanning_ || !seen_.empty())
    return "BT: scan (" + String(seen_.size()) + " found)";
  return "BT: scanning...";
}

void BtService::rememberSighting(const String &name, const String &formattedMac)
{
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
  if (seen_.size() >= 25)
    seen_.erase(seen_.begin());
  seen_.push_back({name, formattedMac, millis()});
  if (connected_)
    peerName_ = name;

  if (onDeviceFound_)
    onDeviceFound_({name, formattedMac, millis()});
}

void BtService::rebuildLinkedMacs()
{
  linkedMacs_.clear();
  for (const auto &d : saved_)
  {
    String plain = stripColonsLower(d.mac);
    if (!plain.isEmpty())
      linkedMacs_.push_back(plain);
  }
}

String BtService::formatMac(const String &macNoColons)
{
  return formatMacImpl(macNoColons);
}

String BtService::stripColonsLower(const String &mac)
{
  String out = mac;
  out.replace(":", "");
  out.replace(",", "");
  out.trim();
  out.toLowerCase();
  return out;
}

bool BtService::parsePairs(const char *json, bool addrFirst,
                           std::vector<std::pair<String, String>> &out)
{
  // Parses [{"name":"A","addr":"B"},...] or [{"addr":"B","name":"A"},...]
  // without a JSON dependency. Values are simple (no escaped quotes
  // from the module), so a tiny key/value scanner is enough.
  if (!json)
    return false;
  String s(json);
  int pos = 0;
  bool any = false;
  while (true)
  {
    int nameKey = s.indexOf("\"name\"", pos);
    int addrKey = s.indexOf("\"addr\"", pos);
    if (nameKey < 0 || addrKey < 0)
      break;
    auto valueAfter = [&](int keyPos) -> String
    {
      int colon = s.indexOf(':', (unsigned)keyPos);
      if (colon < 0)
        return "";
      int q1 = s.indexOf('"', (unsigned)(colon + 1));
      if (q1 < 0)
        return "";
      int q2 = s.indexOf('"', (unsigned)(q1 + 1));
      if (q2 < 0)
        return "";
      return s.substring(q1 + 1, q2);
    };
    String name = valueAfter(nameKey);
    String addr = valueAfter(addrKey);
    (void)addrFirst;
    pos = (nameKey > addrKey ? nameKey : addrKey) + 6;
    if (name.isEmpty() && addr.isEmpty())
      continue;
    out.emplace_back(name, addr);
    any = true;
  }
  return any;
}
