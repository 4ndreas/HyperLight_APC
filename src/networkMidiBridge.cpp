#include "networkMidiBridge.h"

#include <ETH.h>
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
  if (!ETH.begin()) {
    Serial.println("ETH begin failed");
    return;
  }

  g_parser.reset();
  Serial.println("ETH start requested (DHCP)");
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
