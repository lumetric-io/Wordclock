#include "update_status.h"

static volatile bool g_updateRunning = false;

bool is_update_running() {
  return g_updateRunning;
}

void set_update_running(bool running) {
  g_updateRunning = running;
}
