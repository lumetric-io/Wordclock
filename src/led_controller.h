#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#ifndef PIO_UNIT_TESTING
#include <Adafruit_NeoPixel.h>
#endif
#include <vector>

// Export the function prototypes:
void initLeds();
void showLeds(const std::vector<uint16_t> &ledIndices);
void showLedsWithBrightness(const std::vector<uint16_t> &ledIndices, 
                            const std::vector<uint8_t> &brightnessMultipliers);
void setLedsSuspended(bool suspended);

#ifdef PIO_UNIT_TESTING
const std::vector<uint16_t>& test_getLastShownLeds();
void test_clearLastShownLeds();
#endif

#endif // LED_CONTROLLER_H
