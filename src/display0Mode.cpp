#include "display0Mode.h"

namespace {
bool g_display0StatusEnabled = true;
}

bool Display0Mode_IsStatusEnabled() {
  return g_display0StatusEnabled;
}

void Display0Mode_SetStatusEnabled(bool enabled) {
  g_display0StatusEnabled = enabled;
}

void Display0Mode_Toggle() {
  g_display0StatusEnabled = !g_display0StatusEnabled;
}
