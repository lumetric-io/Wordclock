#pragma once
#include <Arduino.h>
#include <vector>

#include "config.h"
#include "grid_layout.h"
#include "led_controller.h"
#include "log.h"

class StartupSequence {
public:
  enum State {
    SWEEP,
    DONE
  };

  StartupSequence()
    : state(SWEEP),
      sweepIndex(0),
      lastUpdate(0) {}

  void start() {
    state = SWEEP;
    sweepIndex = 0;
    showLeds({});
    lastUpdate = millis();
    logDebug("🔁 Startup: Sweep started");
  }

  void update() {
    unsigned long now = millis();

    switch (state) {
      case SWEEP: {
        const uint16_t totalLeds = getActiveLedCountTotal();
        if (now - lastUpdate >= SWEEP_STEP_MS && sweepIndex < totalLeds) {
          showLeds({ static_cast<uint16_t>(sweepIndex) });
          sweepIndex++;
          lastUpdate = now;
          if (sweepIndex >= totalLeds) {
            logDebug("🔁 Startup: Sweep finished");
            showLeds({});
            state = DONE;
            logInfo("✅ Startup completed");
          }
        } else if (sweepIndex >= totalLeds) {
          logDebug("🔁 Startup: Sweep finished (adjusted)");
          showLeds({});
          state = DONE;
          logInfo("✅ Startup completed");
        }
        break;
      }

      case DONE:
      default:
        break;
    }
  }

  bool isRunning() const { return state != DONE; }

private:
  State state;
  uint16_t sweepIndex;
  unsigned long lastUpdate;
};
