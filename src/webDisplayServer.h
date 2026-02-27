#pragma once

#include <Arduino.h>
#include "SparkFun_RGB_OLED_64x64.h"

void WebDisplayServer_Setup(RGB_OLED_64x64** displays, size_t count);
void WebDisplayServer_Loop();
bool WebDisplayServer_ShowStoredImageOnDisplay(int idx);
