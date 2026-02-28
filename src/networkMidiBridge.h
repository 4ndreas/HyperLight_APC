#pragma once

#include <Arduino.h>

struct NetworkIpConfig {
  bool dhcp;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;
};

struct NetworkMidiTransportConfig {
  bool unicast;
  IPAddress targetIp;
  uint16_t port;
};

struct NetworkMidiHeartbeatConfig {
  bool enabled;
  String name;
  IPAddress monitorHost;
  uint16_t monitorPort;
  uint16_t intervalSeconds;
};

using NetToUsbShortHandler = bool (*)(uint8_t status, uint8_t data1, uint8_t data2);
using NetToUsbSysExHandler = bool (*)(const uint8_t* data, uint16_t len);

void NetworkMidi_Setup();
void NetworkMidi_Loop();
void NetworkMidi_SendFromUsb(const uint8_t* data, size_t len);
void NetworkMidi_SetUsbTxHandlers(NetToUsbShortHandler shortHandler, NetToUsbSysExHandler sysExHandler);
bool NetworkMidi_SaveConfig(const NetworkIpConfig& cfg);
NetworkIpConfig NetworkMidi_GetConfig();
bool NetworkMidi_SaveTransportConfig(const NetworkMidiTransportConfig& cfg);
NetworkMidiTransportConfig NetworkMidi_GetTransportConfig();
bool NetworkMidi_SaveHeartbeatConfig(const NetworkMidiHeartbeatConfig& cfg);
NetworkMidiHeartbeatConfig NetworkMidi_GetHeartbeatConfig();
bool NetworkMidi_IsLinkUp();
IPAddress NetworkMidi_GetLocalIP();
