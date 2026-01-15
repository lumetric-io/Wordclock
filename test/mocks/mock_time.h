#ifndef MOCK_TIME_H
#define MOCK_TIME_H

#include <time.h>

/**
 * @brief Helper to create test time structures
 */
inline struct tm createTestTime(int hour, int minute, int second = 0) {
    struct tm t = {};
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_mday = 1;
    t.tm_mon = 0;
    t.tm_year = 2024 - 1900;
    return t;
}

#endif // MOCK_TIME_H

