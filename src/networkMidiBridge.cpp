#include "networkMidiBridge.h"

#include <ETH.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WiFiUdp.h>
#include <sys/time.h>

#include "config.h"

namespace {
WiFiUDP g_midiUdp;
WiFiUDP g_heartbeatUdp;
NetToUsbShortHandler g_shortHandler = nullptr;
NetToUsbSysExHandler g_sysExHandler = nullptr;
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
NetworkMidiTransportConfig g_transportCfg = {
    false,
    IPAddress(NETWORK_MIDI_MCAST_A, NETWORK_MIDI_MCAST_B, NETWORK_MIDI_MCAST_C, NETWORK_MIDI_MCAST_D),
    NETWORK_MIDI_PORT};
NetworkMidiHeartbeatConfig g_heartbeatCfg = {
    true,
    "wt32_bridge",
    IPAddress(0, 0, 0, 0),
    21930,
    1};
uint32_t g_lastHeartbeatMs = 0;
uint32_t g_usbToNetPackets = 0;
uint32_t g_netToUsbPackets = 0;

constexpr const char* kCfgPath = "/net.cfg";

class MidiStreamParser {
 public:
  void reset() {
    runningStatus_ = 0;
    expectedDataBytes_ = 0;
    dataIndex_ = 0;
    inSysEx_ = false;
    sysExLen_ = 0;
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
        sysExLen_ = 0;
        appendSysExByte(byte);
        return;
      }

      if (byte == 0xF7) {
        if (inSysEx_) {
          appendSysExByte(byte);
          flushSysEx();
        }
        inSysEx_ = false;
        return;
      }

      if (inSysEx_) {
        // Unexpected status during SysEx: flush what we have and continue with new status.
        flushSysEx();
        inSysEx_ = false;
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
      appendSysExByte(byte);
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
  void appendSysExByte(uint8_t byte) {
    if (sysExLen_ < sizeof(sysExBuf_)) {
      sysExBuf_[sysExLen_++] = byte;
      return;
    }

    // Overflow protection: try to send the partial message and reset.
    flushSysEx();
    inSysEx_ = false;
  }

  void flushSysEx() {
    if (sysExLen_ < 2 || sysExBuf_[0] != 0xF0 || sysExBuf_[sysExLen_ - 1] != 0xF7) {
      sysExLen_ = 0;
      return;
    }

    if (g_sysExHandler == nullptr) {
      sysExLen_ = 0;
      return;
    }

#if MIDI_DEBUG_IN
    Serial.printf("USB_OUT_SYSEX: len=%u\r\n", sysExLen_);
#endif
    if (g_sysExHandler(sysExBuf_, sysExLen_)) {
      g_netToUsbPackets++;
    }
    sysExLen_ = 0;
  }

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
    if (g_shortHandler == nullptr) {
      return;
    }

    uint8_t msg[3] = {status, data1, data2};
#if MIDI_DEBUG_IN
    Serial.printf("USB_OUT: %02X %02X %02X\r\n", msg[0], msg[1], msg[2]);
#endif
    if (g_shortHandler(msg[0], msg[1], msg[2])) {
      g_netToUsbPackets++;
    }
  }

  uint8_t runningStatus_ = 0;
  uint8_t expectedDataBytes_ = 0;
  uint8_t dataIndex_ = 0;
  uint8_t data_[2] = {0, 0};
  bool inSysEx_ = false;
  uint16_t sysExLen_ = 0;
  uint8_t sysExBuf_[1024] = {0};
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

void restartMidiUdpSocket() {
  if (g_udpStarted) {
    g_midiUdp.stop();
  }
  g_udpStarted = false;
}

void loadConfig() {
  g_cfg = {
      true,
      IPAddress(0, 0, 0, 0),
      IPAddress(0, 0, 0, 0),
      IPAddress(255, 255, 255, 0),
      IPAddress(0, 0, 0, 0),
      IPAddress(0, 0, 0, 0)};
  g_transportCfg = {
      false,
      IPAddress(NETWORK_MIDI_MCAST_A, NETWORK_MIDI_MCAST_B, NETWORK_MIDI_MCAST_C, NETWORK_MIDI_MCAST_D),
      NETWORK_MIDI_PORT};
  g_heartbeatCfg = {
      true,
      "wt32_bridge",
      IPAddress(0, 0, 0, 0),
      21930,
      1};

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
    } else if (key == "midi_mode") {
      g_transportCfg.unicast = (val == "unicast");
    } else if (key == "midi_target") {
      parseIp(val, g_transportCfg.targetIp);
    } else if (key == "midi_port") {
      int p = val.toInt();
      if (p > 0 && p <= 65535) {
        g_transportCfg.port = static_cast<uint16_t>(p);
      }
    } else if (key == "heartbeat_enabled") {
      g_heartbeatCfg.enabled = (val != "0");
    } else if (key == "heartbeat_name") {
      g_heartbeatCfg.name = val;
    } else if (key == "heartbeat_target_host") {
      parseIp(val, g_heartbeatCfg.monitorHost);
    } else if (key == "heartbeat_port") {
      int p = val.toInt();
      if (p > 0 && p <= 65535) {
        g_heartbeatCfg.monitorPort = static_cast<uint16_t>(p);
      }
    } else if (key == "heartbeat_interval_s") {
      int s = val.toInt();
      if (s > 0 && s <= 3600) {
        g_heartbeatCfg.intervalSeconds = static_cast<uint16_t>(s);
      }
    }
  }

  f.close();
}

bool saveConfigToDisk() {
  fs::FS* fs = getConfigFs();
  if (fs == nullptr) {
    return false;
  }

  File f = fs->open(kCfgPath, FILE_WRITE);
  if (!f) {
    return false;
  }

  f.printf("mode=%s\n", g_cfg.dhcp ? "dhcp" : "static");
  f.printf("ip=%s\n", g_cfg.ip.toString().c_str());
  f.printf("gateway=%s\n", g_cfg.gateway.toString().c_str());
  f.printf("subnet=%s\n", g_cfg.subnet.toString().c_str());
  f.printf("dns1=%s\n", g_cfg.dns1.toString().c_str());
  f.printf("dns2=%s\n", g_cfg.dns2.toString().c_str());
  f.printf("midi_mode=%s\n", g_transportCfg.unicast ? "unicast" : "multicast");
  f.printf("midi_target=%s\n", g_transportCfg.targetIp.toString().c_str());
  f.printf("midi_port=%u\n", g_transportCfg.port);
  f.printf("heartbeat_enabled=%u\n", g_heartbeatCfg.enabled ? 1 : 0);
  f.printf("heartbeat_name=%s\n", g_heartbeatCfg.name.c_str());
  f.printf("heartbeat_target_host=%s\n", g_heartbeatCfg.monitorHost.toString().c_str());
  f.printf("heartbeat_port=%u\n", g_heartbeatCfg.monitorPort);
  f.printf("heartbeat_interval_s=%u\n", g_heartbeatCfg.intervalSeconds);
  f.close();

  return true;
}

void startUdpIfPossible() {
  if (g_udpStarted || !ETH.linkUp()) {
    return;
  }

  bool ok = false;
  if (g_transportCfg.unicast) {
    ok = g_midiUdp.begin(g_transportCfg.port);
    if (ok) {
      Serial.printf(
          "Network MIDI: unicast listen %s:%u\r\n",
          ETH.localIP().toString().c_str(),
          g_transportCfg.port);
    }
  } else {
    ok = g_midiUdp.beginMulticast(g_multicastIp, g_transportCfg.port);
    if (ok) {
      Serial.printf(
          "Network MIDI: multicast %u.%u.%u.%u:%u\r\n",
          g_multicastIp[0], g_multicastIp[1], g_multicastIp[2], g_multicastIp[3],
          g_transportCfg.port);
    }
  }

  if (!ok) {
    Serial.println("Network MIDI: UDP start failed");
    return;
  }

  g_udpStarted = true;
}

IPAddress currentMidiTargetIp() {
  return g_transportCfg.unicast ? g_transportCfg.targetIp : g_multicastIp;
}

void sendHeartbeatIfDue() {
  if (!g_heartbeatCfg.enabled || !ETH.linkUp()) {
    return;
  }

  if (static_cast<uint32_t>(g_heartbeatCfg.monitorHost) == 0 || g_heartbeatCfg.monitorPort == 0) {
    return;
  }

  uint32_t now = millis();
  uint32_t intervalMs = static_cast<uint32_t>(g_heartbeatCfg.intervalSeconds) * 1000UL;
  if (intervalMs == 0 || (now - g_lastHeartbeatMs) < intervalMs) {
    return;
  }
  g_lastHeartbeatMs = now;

  timeval tv = {};
  gettimeofday(&tv, nullptr);
  double ts = static_cast<double>(tv.tv_sec) + (static_cast<double>(tv.tv_usec) / 1000000.0);
  if (tv.tv_sec <= 0) {
    ts = static_cast<double>(now) / 1000.0;
  }

  IPAddress midiDst = currentMidiTargetIp();
  char payload[512];
  snprintf(
      payload,
      sizeof(payload),
      "{\"type\":\"heartbeat\",\"name\":\"%s\",\"bridge_name\":\"%s\",\"target_host\":\"%s\",\"target_port\":%u,\"usb_to_net_packets\":%lu,\"net_to_usb_packets\":%lu,\"ts\":%.3f}",
      g_heartbeatCfg.name.c_str(),
      NETWORK_MIDI_BRIDGE_NAME,
      midiDst.toString().c_str(),
      g_transportCfg.port,
      static_cast<unsigned long>(g_usbToNetPackets),
      static_cast<unsigned long>(g_netToUsbPackets),
      ts);

  if (g_heartbeatUdp.beginPacket(g_heartbeatCfg.monitorHost, g_heartbeatCfg.monitorPort)) {
    g_heartbeatUdp.write(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
    g_heartbeatUdp.endPacket();
  }
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

void NetworkMidi_SetUsbTxHandlers(NetToUsbShortHandler shortHandler, NetToUsbSysExHandler sysExHandler) {
  g_shortHandler = shortHandler;
  g_sysExHandler = sysExHandler;
}

void NetworkMidi_Loop() {
#if NETWORK_MIDI_ENABLE
  if (ETH.linkUp() && !g_linkLogged) {
    g_linkLogged = true;
    Serial.printf("ETH link up, IP: %s\r\n", ETH.localIP().toString().c_str());
  } else if (!ETH.linkUp() && g_linkLogged) {
    g_linkLogged = false;
    restartMidiUdpSocket();
    Serial.println("ETH link down");
  }

  startUdpIfPossible();
  sendHeartbeatIfDue();

  if (!g_udpStarted) {
    return;
  }

  int packetSize = g_midiUdp.parsePacket();
  while (packetSize > 0) {
    IPAddress remoteIp = g_midiUdp.remoteIP();
    bool isOwnPacket = (!g_transportCfg.unicast && remoteIp == ETH.localIP());
#if MIDI_DEBUG_IN
    Serial.printf(
        "NET_IN from %s:%u, bytes=%d\r\n",
        remoteIp.toString().c_str(),
        g_midiUdp.remotePort(),
        packetSize);
#endif

    while (packetSize-- > 0) {
      int value = g_midiUdp.read();
      if (value < 0) {
        break;
      }
      if (!isOwnPacket) {
#if MIDI_DEBUG_IN
        Serial.printf("NET_IN_BYTE: %02X\r\n", static_cast<uint8_t>(value));
#endif
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

  IPAddress dst = g_transportCfg.unicast ? g_transportCfg.targetIp : g_multicastIp;
  if (!g_midiUdp.beginPacket(dst, g_transportCfg.port)) {
    return;
  }

  g_midiUdp.write(data, len);
  g_midiUdp.endPacket();
  g_usbToNetPackets++;
#else
  (void)data;
  (void)len;
#endif
}

bool NetworkMidi_SaveConfig(const NetworkIpConfig& cfg) {
  g_cfg = cfg;
  return saveConfigToDisk();
}

NetworkIpConfig NetworkMidi_GetConfig() {
  return g_cfg;
}

bool NetworkMidi_SaveTransportConfig(const NetworkMidiTransportConfig& cfg) {
  g_transportCfg = cfg;
  if (g_transportCfg.port == 0) {
    g_transportCfg.port = NETWORK_MIDI_PORT;
  }

  restartMidiUdpSocket();
  return saveConfigToDisk();
}

NetworkMidiTransportConfig NetworkMidi_GetTransportConfig() {
  return g_transportCfg;
}

bool NetworkMidi_SaveHeartbeatConfig(const NetworkMidiHeartbeatConfig& cfg) {
  g_heartbeatCfg = cfg;
  if (g_heartbeatCfg.intervalSeconds == 0) {
    g_heartbeatCfg.intervalSeconds = 1;
  }
  if (g_heartbeatCfg.monitorPort == 0) {
    g_heartbeatCfg.monitorPort = 21930;
  }
  if (g_heartbeatCfg.name.length() == 0) {
    g_heartbeatCfg.name = "wt32_bridge";
  }
  g_lastHeartbeatMs = 0;
  return saveConfigToDisk();
}

NetworkMidiHeartbeatConfig NetworkMidi_GetHeartbeatConfig() {
  return g_heartbeatCfg;
}

bool NetworkMidi_IsLinkUp() {
  return ETH.linkUp();
}

IPAddress NetworkMidi_GetLocalIP() {
  return ETH.localIP();
}
