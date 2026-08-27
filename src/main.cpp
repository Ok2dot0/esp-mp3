#include <SPI.h>
#include <SD.h>
#include <vector>
#include <functional>
#include "Arduino.h"
#include "Audio.h"

namespace Pins {
  constexpr uint8_t SD_SCK   = 1;
  constexpr uint8_t SD_MISO  = 9;
  constexpr uint8_t SD_MOSI  = 3;
  constexpr uint8_t SD_CS    = 10;

  constexpr uint8_t KCX_RX   = 14;
  constexpr uint8_t KCX_TX   = 17;

  constexpr uint8_t DAC_BCK  = 6;
  constexpr uint8_t DAC_WS   = 7;
  constexpr uint8_t DAC_DIN  = 16;

  constexpr uint8_t CLICK_CLK   = 4;
  constexpr uint8_t CLICK_DATA  = 5;
  constexpr uint8_t CLICK_WAKE  = 18;
  constexpr uint8_t CLICK_RESET = 21;
}

Audio audio;
volatile bool shouldRepeatTrack = false;

void audioInfoCallback(Audio::msg_t m) {
  if (m.s && m.msg) {
    Serial.printf("[Audio %s] %s\n", m.s, m.msg);
  }
  
  if (m.e == Audio::evt_eof) {
    Serial.println("[Audio] EOF event detected!");
    shouldRepeatTrack = true;
  }
}

struct BtDevice {
  String name;
  String mac;
};

class KcxController {
public:
  using DeviceCallback = std::function<void(const BtDevice&)>;
  using StatusCallback = std::function<void(bool, const String&)>;

  KcxController(uint8_t rxPin, uint8_t txPin)
      : _rxPin(rxPin), _txPin(txPin), _serial(1) {}

  void begin(uint32_t baud = 115200) {
    _serial.begin(baud, SERIAL_8N1, _rxPin, _txPin);
    delay(100);
    while (_serial.available()) _serial.read();
  }

  void update() {
    while (_serial.available()) {
      char c = static_cast<char>(_serial.read());

      if (c == '\r') continue;

      if (c == '\n') {
        _rxBuffer.trim();
        if (_rxBuffer.length() > 0) parseLine(_rxBuffer);
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
    _seenMacs.clear();
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

  void onDeviceFound(DeviceCallback cb) {
    _onDeviceFound = cb;
  }

  void onConnectionChange(StatusCallback cb) {
    _onStatusChange = cb;
  }

private:
  uint8_t _rxPin;
  uint8_t _txPin;
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

      String formattedMac;
      for (size_t i = 0; i < mac.length(); ++i) {
        formattedMac += mac[i];
        if ((i % 2 == 1) && (i + 1 < mac.length())) formattedMac += ':';
      }

      if (_onDeviceFound) _onDeviceFound({name, formattedMac});
      return;
    }

    if (line.indexOf("CONNECTED") >= 0 ||
        line.indexOf("CON MATCH") >= 0 ||
        line.startsWith("CONNECT=>")) {
      if (_onStatusChange) _onStatusChange(true, line);
      return;
    }

    if (line.indexOf("DISCONNECT") >= 0 || line == "OK+DISCON") {
      if (_onStatusChange) _onStatusChange(false, line);
      return;
    }

    if (line.indexOf("OK+VERS:") >= 0) {
      Serial.printf("[KCX] Firmware: %s\n", line.c_str());
    }
  }
};

KcxController bt(Pins::KCX_RX, Pins::KCX_TX);

class ClickWheel {
public:
  struct State {
    bool touching;
    uint8_t position;
    uint8_t buttons;
    bool btnCenter;
    bool btnRight;
    bool btnLeft;
    bool btnDown;
    bool btnUp;
    uint8_t statusByte;
  };

  using ReportCallback = std::function<void(const State&)>;

  ClickWheel(uint8_t clkPin, uint8_t dataPin)
      : _clkPin(clkPin), _dataPin(dataPin) {}

  void begin(void (*isr)()) {
    pinMode(_clkPin, INPUT_PULLUP);
    pinMode(_dataPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_clkPin), isr, FALLING);
  }

  void onReport(ReportCallback cb) {
    _onReport = cb;
  }

  void update(bool printRaw = false) {
    if (!_frameReady) return;

    uint8_t frame[4];

    noInterrupts();
    for (uint8_t i = 0; i < 4; ++i) frame[i] = _frame[i];
    _frameReady = false;
    interrupts();

    if (printRaw) {
      Serial.printf("[Wheel] raw: %02X %02X %02X %02X\n",
                    frame[0], frame[1], frame[2], frame[3]);
    }

    State state;
    state.buttons    = frame[1];
    state.btnCenter  = (frame[1] & 0x01) != 0;
    state.btnRight   = (frame[1] & 0x02) != 0;
    state.btnLeft    = (frame[1] & 0x04) != 0;
    state.btnDown    = (frame[1] & 0x08) != 0;
    state.btnUp      = (frame[1] & 0x10) != 0;
    state.position   = frame[2];
    state.touching   = (frame[3] & 0x40) != 0;
    state.statusByte = frame[3];

    if (_onReport) _onReport(state);
  }

  void IRAM_ATTR handleEdge() {
    uint8_t bit = static_cast<uint8_t>(digitalRead(_dataPin));
    _shiftByte |= static_cast<uint8_t>(bit << _bitCount);
    _bitCount = _bitCount + 1;

    if (_bitCount < 8) return;

    if (_byteIndex == 0 && _shiftByte != 0x1A) {
      _shiftByte = 0;
      _bitCount = 0;
      return;
    }

    _frame[_byteIndex] = _shiftByte;
    _byteIndex = _byteIndex + 1;
    _shiftByte = 0;
    _bitCount = 0;

    if (_byteIndex == 4) {
      _byteIndex = 0;
      _frameReady = true;
    }
  }

private:
  uint8_t _clkPin;
  uint8_t _dataPin;
  ReportCallback _onReport = nullptr;

  volatile uint8_t _shiftByte = 0;
  volatile uint8_t _bitCount = 0;
  volatile uint8_t _byteIndex = 0;
  volatile uint8_t _frame[4] = {0, 0, 0, 0};
  volatile bool _frameReady = false;
};

ClickWheel wheel(Pins::CLICK_CLK, Pins::CLICK_DATA);

void IRAM_ATTR clickWheelISR() {
  wheel.handleEdge();
}

bool initSD() {
  SPI.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  return SD.begin(Pins::SD_CS);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== System Starting ===");

  Audio::audio_info_callback = audioInfoCallback;

  pinMode(Pins::CLICK_RESET, OUTPUT);
  digitalWrite(Pins::CLICK_RESET, LOW);
  delay(20);
  digitalWrite(Pins::CLICK_RESET, HIGH);
  delay(50);
  pinMode(Pins::CLICK_WAKE, INPUT_PULLUP);

  if (initSD()) {
    Serial.println("[SD] Initialized successfully.");
  } else {
    Serial.println("[SD] Initialization failed!");
  }

  audio.setPinout(Pins::DAC_BCK, Pins::DAC_WS, Pins::DAC_DIN);
  audio.setVolume(12);
  audio.connecttoFS(SD, "/sample-15s.mp3");
  Serial.println("[Audio] Playing sample-15s.mp3...");

  bt.onDeviceFound([](const BtDevice& dev) {
    Serial.printf("[BT] Found: %-25s | MAC: %s\n",
                  dev.name.c_str(), dev.mac.c_str());
    bt.connectByMac(dev.mac);
  });

  bt.onConnectionChange([](bool connected, const String&) {
    if (connected) {
      Serial.println("[BT] Status: Connected to audio sink.");
    } else {
      Serial.println("[BT] Status: Disconnected / Scanning.");
    }
  });

  bt.begin();
  bt.requestVersion();
  Serial.println("[BT] Starting clean scan...");
  bt.startScan(true);

  wheel.onReport([](const ClickWheel::State& state) {
    Serial.printf("[Wheel] %s pos=%3u | Buttons [Center:%d Menu:%d Play:%d Next:%d Prev:%d]\n",
                  state.touching ? "TOUCH" : "FREE ",
                  state.position,
                  state.btnCenter,
                  state.btnUp,
                  state.btnDown,
                  state.btnRight,
                  state.btnLeft);
  });

  wheel.begin(clickWheelISR);
  Serial.println("[Wheel] Click wheel listener started.");
}

void loop() {
  audio.loop();
  vTaskDelay(1);

  if (shouldRepeatTrack) {
    shouldRepeatTrack = false;
    Serial.println("[Audio] Restarting track...");
    audio.connecttoFS(SD, "/sample-15s.mp3");
  }

  bt.update();
  wheel.update(true);

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) bt.sendCommand(cmd);
  }
}