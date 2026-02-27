#include "networkMidiBridge.h"

#include <ETH.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WiFiUdp.h>
#include <usbh_midi.h>

#include "config.h"

extern USBH_MIDI Midi;

namespace {
WiFiUDP g_midiUdp;
IPAddress g_multicastIp(
    NETWORK_MIDI_MCAST_A,
    NETWORK_MIDI_MCAST_B,
    NETWORK_MIDI_MCAST_C,
    NETWORK_MIDI_MCAST_D);

bool g_udpStarted = false;
bool g_linkLogged = false;
bool g_spiffsReady = false;
NetworkIpConfig g_cfg = {
    true,
    IPAddress(0, 0, 0, 0),
    IPAddress(0, 0, 0, 0),
    IPAddress(0, 0, 0, 0),
    IPAddress(0, 0, 0, 0),
    IPAddress(0, 0, 0, 0)};

constexpr const char* kCfgPath = "/net.cfg";

class MidiStreamParser {
 public:
  void reset() {
    runningStatus_ = 0;
    expectedDataBytes_ = 0;
    dataIndex_ = 0;
    inSysEx_ = false;
  }

  void push(uint8_t byte) {
    if (byte >= 0xF8) {
      sendToUsb(byte, 0, 0);
      return;
    }

    if (byte & 0x80) {
      if (byte == 0xF0) {
        inSysEx_ = true;
        runningStatus_ = 0;
        expectedDataBytes_ = 0;
        dataIndex_ = 0;
        return;
      }

      if (byte == 0xF7) {
        inSysEx_ = false;
        return;
      }

      inSysEx_ = false;
      runningStatus_ = byte;
      expectedDataBytes_ = statusDataBytes(byte);
      dataIndex_ = 0;

      if (expectedDataBytes_ == 0) {
        sendToUsb(byte, 0, 0);
      }
      return;
    }

    if (inSysEx_) {
      return;
    }

    if (runningStatus_ == 0 || expectedDataBytes_ == 0) {
      return;
    }

    data_[dataIndex_++] = byte;
    if (dataIndex_ >= expectedDataBytes_) {
      uint8_t d1 = data_[0];
      uint8_t d2 = (expectedDataBytes_ > 1) ? data_[1] : 0;
      sendToUsb(runningStatus_, d1, d2);
      dataIndex_ = 0;
    }
  }

 private:
  static uint8_t statusDataBytes(uint8_t status) {
    if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0) {
      uint8_t high = status & 0xF0;
      return (high == 0xC0 || high == 0xD0) ? 1 : 2;
    }

    switch (status) {
      case 0xF1:
      case 0xF3:
        return 1;
      case 0xF2:
        return 2;
      case 0xF6:
        return 0;
      default:
        return 0;
    }
  }

  static void sendToUsb(uint8_t status, uint8_t data1, uint8_t data2) {
    if (!Midi) {
      return;
    }

    uint8_t msg[3] = {status, data1, data2};
    Midi.SendData(msg, 0);
  }

  uint8_t runningStatus_ = 0;
  uint8_t expectedDataBytes_ = 0;
  uint8_t dataIndex_ = 0;
  uint8_t data_[2] = {0, 0};
  bool inSysEx_ = false;
};

MidiStreamParser g_parser;

fs::FS* getConfigFs() {
  if (SD.cardType() != CARD_NONE) {
    return &SD;
  }

  if (!g_spiffsReady) {
    g_spiffsReady = SPIFFS.begin(true);
  }

  if (g_spiffsReady) {
    return &SPIFFS;
  }

  return nullptr;
}

bool parseIp(const String& text, IPAddress& out) {
  int a = -1, b = -1, c = -1, d = -1;
  if (sscanf(text.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }

  if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
    return false;
  }

  out = IPAddress(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d));
  return true;
}

void trimLine(String& s) {
  s.trim();
}

void loadConfig() {
  g_cfg = {
      true,
      IPAddress(0, 0, 0, 0),
      IPAddress(0, 0, 0, 0),
      IPAddress(255, 255, 255, 0),
      IPAddress(0, 0, 0, 0),
      IPAddress(0, 0, 0, 0)};

  fs::FS* fs = getConfigFs();
  if (fs == nullptr || !fs->exists(kCfgPath)) {
    return;
  }

  File f = fs->open(kCfgPath, FILE_READ);
  if (!f) {
    return;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    trimLine(line);
    if (line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    int sep = line.indexOf('=');
    if (sep <= 0) {
      continue;
    }

    String key = line.substring(0, sep);
    String val = line.substring(sep + 1);
    trimLine(key);
    trimLine(val);

    if (key == "mode") {
      g_cfg.dhcp = (val != "static");
    } else if (key == "ip") {
      parseIp(val, g_cfg.ip);
    } else if (key == "gateway") {
      parseIp(val, g_cfg.gateway);
    } else if (key == "subnet") {
      parseIp(val, g_cfg.subnet);
    } else if (key == "dns1") {
      parseIp(val, g_cfg.dns1);
    } else if (key == "dns2") {
      parseIp(val, g_cfg.dns2);
    }
  }

  f.close();
}

void startUdpIfPossible() {
  if (g_udpStarted || !ETH.linkUp()) {
    return;
  }

  if (!g_midiUdp.beginMulticast(g_multicastIp, NETWORK_MIDI_PORT)) {
    Serial.println("Network MIDI: UDP multicast start failed");
    return;
  }

  g_udpStarted = true;
  Serial.printf(
      "Network MIDI: listening on %u.%u.%u.%u:%u\r\n",
      g_multicastIp[0], g_multicastIp[1], g_multicastIp[2], g_multicastIp[3],
      NETWORK_MIDI_PORT);
}
}  // namespace

void NetworkMidi_Setup() {
#if NETWORK_MIDI_ENABLE
  loadConfig();

  if (!ETH.begin()) {
    Serial.println("ETH begin failed");
    return;
  }

  if (g_cfg.dhcp) {
    ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    Serial.println("ETH start requested (DHCP)");
  } else {
    ETH.config(g_cfg.ip, g_cfg.gateway, g_cfg.subnet, g_cfg.dns1, g_cfg.dns2);
    Serial.printf("ETH start requested (STATIC %s)\r\n", g_cfg.ip.toString().c_str());
  }

  g_parser.reset();
#endif
}

void NetworkMidi_Loop() {
#if NETWORK_MIDI_ENABLE
  if (ETH.linkUp() && !g_linkLogged) {
    g_linkLogged = true;
    Serial.printf("ETH link up, IP: %s\r\n", ETH.localIP().toString().c_str());
  } else if (!ETH.linkUp() && g_linkLogged) {
    g_linkLogged = false;
    g_udpStarted = false;
    g_midiUdp.stop();
    Serial.println("ETH link down");
  }

  startUdpIfPossible();

  if (!g_udpStarted) {
    return;
  }

  int packetSize = g_midiUdp.parsePacket();
  while (packetSize > 0) {
    IPAddress remoteIp = g_midiUdp.remoteIP();
    bool isOwnPacket = (remoteIp == ETH.localIP());

    while (packetSize-- > 0) {
      int value = g_midiUdp.read();
      if (value < 0) {
        break;
      }
      if (!isOwnPacket) {
        g_parser.push(static_cast<uint8_t>(value));
      }
    }

    packetSize = g_midiUdp.parsePacket();
  }
#endif
}

void NetworkMidi_SendFromUsb(const uint8_t* data, size_t len) {
#if NETWORK_MIDI_ENABLE
  if (!g_udpStarted || !ETH.linkUp() || data == nullptr || len == 0) {
    return;
  }

  if (!g_midiUdp.beginPacket(g_multicastIp, NETWORK_MIDI_PORT)) {
    return;
  }

  g_midiUdp.write(data, len);
  g_midiUdp.endPacket();
#else
  (void)data;
  (void)len;
#endif
}

bool NetworkMidi_SaveConfig(const NetworkIpConfig& cfg) {
  fs::FS* fs = getConfigFs();
  if (fs == nullptr) {
    return false;
  }

  File f = fs->open(kCfgPath, FILE_WRITE);
  if (!f) {
    return false;
  }

  f.printf("mode=%s\n", cfg.dhcp ? "dhcp" : "static");
  f.printf("ip=%s\n", cfg.ip.toString().c_str());
  f.printf("gateway=%s\n", cfg.gateway.toString().c_str());
  f.printf("subnet=%s\n", cfg.subnet.toString().c_str());
  f.printf("dns1=%s\n", cfg.dns1.toString().c_str());
  f.printf("dns2=%s\n", cfg.dns2.toString().c_str());
  f.close();

  g_cfg = cfg;
  return true;
}

NetworkIpConfig NetworkMidi_GetConfig() {
  return g_cfg;
}

bool NetworkMidi_IsLinkUp() {
  return ETH.linkUp();
}

IPAddress NetworkMidi_GetLocalIP() {
  return ETH.localIP();
}
