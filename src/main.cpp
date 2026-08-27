#include <SPI.h>
#include <SD.h>
#include <vector>
#include <functional>
#include "Arduino.h"
#include "AudioTools.h"

namespace Pins {
  constexpr uint8_t SD_SCK   = 1;
  constexpr uint8_t SD_MISO  = 9;
  constexpr uint8_t SD_MOSI  = 3;
  constexpr uint8_t SD_CS    = 10;

  constexpr uint8_t KCX_RX   = 14; // ESP32 RX -> KCX TX
  constexpr uint8_t KCX_TX   = 17; // ESP32 TX -> KCX RX

  constexpr uint8_t DAC_BCK  = 6;
  constexpr uint8_t DAC_WS   = 7;
  constexpr uint8_t DAC_DIN  = 16;
}

AudioInfo audioInfo(44100, 2, 16);
SineWaveGenerator<int16_t> sineWave(28000);
GeneratedSoundStream<int16_t> sound(sineWave);
I2SStream i2s;
StreamCopy copier(i2s, sound);

struct BtDevice {
  String name;
  String mac;
};


class KcxController {
public:
  using DeviceCallback = std::function<void(const BtDevice&)>;
  using StatusCallback = std::function<void(bool connected, const String& details)>;

  KcxController(uint8_t rxPin, uint8_t txPin) 
    : _rxPin(rxPin), _txPin(txPin), _serial(1) {}

  void begin(uint32_t baud = 115200) {
    _serial.begin(baud, SERIAL_8N1, _rxPin, _txPin);
    delay(100);
    while (_serial.available()) _serial.read();
  }

  void update() {
    while (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        _rxBuffer.trim();
        if (_rxBuffer.length() > 0) {
          parseLine(_rxBuffer);
        }
        _rxBuffer = "";
      } else {
        _rxBuffer += c;
      }
    }
  }

  void sendCommand(const String& cmd) {
    _serial.print(cmd + "\r\n");
  }

  void requestVersion() {
    sendCommand("AT+GMR?");
  }

  void startScan(bool clearMemory = false) {
    if (clearMemory) {
      sendCommand("AT+DELADD=ALL");
      delay(50);
    }
    sendCommand("AT+DISCON");
  }

  void disconnect() {
    sendCommand("AT+DISCON");
  }

  void connectByMac(const String& rawMac) {
    String cleanMac = rawMac;
    cleanMac.replace(":", "");
    sendCommand("AT+CONADD=" + cleanMac);
  }

  void connectByName(const String& name) {
    sendCommand("AT+CONNAME=" + name);
  }

  void onDeviceFound(DeviceCallback cb) { _onDeviceFound = cb; }
  void onConnectionChange(StatusCallback cb) { _onStatusChange = cb; }

private:
  uint8_t _rxPin, _txPin;
  HardwareSerial _serial;
  String _rxBuffer;
  std::vector<String> _seenMacs;

  DeviceCallback _onDeviceFound = nullptr;
  StatusCallback _onStatusChange = nullptr;

  void parseLine(const String& line) {
    if (line.indexOf("MacAdd:") >= 0 && line.indexOf("Name:") >= 0) {
      int macIdx = line.indexOf("MacAdd:");
      int nameIdx = line.indexOf("Name:");

      String mac = line.substring(macIdx + 7, nameIdx);
      mac.replace(",", "");
      mac.trim();

      String name = line.substring(nameIdx + 5);
      name.trim();

      for (const auto& seen : _seenMacs) {
        if (seen == mac) return;
      }
      _seenMacs.push_back(mac);

      String formattedMac = "";
      for (size_t i = 0; i < mac.length(); i++) {
        formattedMac += mac[i];
        if (i % 2 == 1 && i < mac.length() - 1) formattedMac += ":";
      }

      if (_onDeviceFound) {
        _onDeviceFound({name, formattedMac});
      }
    }
    else if (line.indexOf("CONNECTED") >= 0 || line.indexOf("CON MATCH") >= 0 || line.startsWith("CONNECT=>")) {
      if (_onStatusChange) _onStatusChange(true, line);
    }
    else if (line.indexOf("DISCONNECT") >= 0 || line == "OK+DISCON") {
      if (_onStatusChange) _onStatusChange(false, line);
    }
    else if (line.indexOf("OK+VERS:") >= 0) {
      Serial.printf("[KCX] Firmware: %s\n", line.c_str());
    }
  }
};

KcxController bt(Pins::KCX_RX, Pins::KCX_TX);


bool initSD() {
  SPI.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  return SD.begin(Pins::SD_CS);
}


void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== System Starting ===");

  if (initSD()) {
    Serial.println("[SD] Initialized successfully.");
  } else {
    Serial.println("[SD] Initialization failed!");
  }

  auto config = i2s.defaultConfig(TX_MODE);
  config.copyFrom(audioInfo);
  config.pin_bck  = Pins::DAC_BCK;
  config.pin_ws   = Pins::DAC_WS;
  config.pin_data = Pins::DAC_DIN;
  i2s.begin(config);

  sineWave.begin(audioInfo, 440);
  Serial.println("[Audio] 440Hz Sine Wave streaming to I2S DAC.");

  bt.onDeviceFound([](const BtDevice& dev) {
    Serial.printf("[BT] Found: %-25s | MAC: %s\n", dev.name.c_str(), dev.mac.c_str());
    bt.connectByMac(dev.mac);
  });

  bt.onConnectionChange([](bool connected, const String& details) {
    if (connected) {
      Serial.println("[BT] Status: Connected to audio sink.");
    } else {
      Serial.println("[BT] Status: Disconnected / Scanning.");
    }
  });

  bt.begin();
  bt.requestVersion();

  Serial.println("[BT] Starting clean scan...");
  bt.startScan(/* clearMemory = */ true);
}

void loop() {
  copier.copy();

  bt.update();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      bt.sendCommand(cmd);
    }
  }
}
