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

void NetworkMidi_Setup();
void NetworkMidi_Loop();
void NetworkMidi_SendFromUsb(const uint8_t* data, size_t len);
bool NetworkMidi_SaveConfig(const NetworkIpConfig& cfg);
NetworkIpConfig NetworkMidi_GetConfig();
bool NetworkMidi_IsLinkUp();
IPAddress NetworkMidi_GetLocalIP();
