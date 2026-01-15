#include "wordclock.h"
#include "clock_display.h"
#include "log.h"

void wordclock_setup() {
  // ledState.begin() is initialized in main
  initLeds();
  clockDisplay.reset();
  logInfo("Wordclock setup complete");
}

void wordclock_loop() {
  clockDisplay.update();
}

void wordclock_force_animation_for_time(struct tm* timeinfo) {
  if (!timeinfo) return;
  clockDisplay.forceAnimationForTime(*timeinfo);
}
