#pragma once

#include <Arduino.h>

void NetworkMidi_Setup();
void NetworkMidi_Loop();
void NetworkMidi_SendFromUsb(const uint8_t* data, size_t len);
