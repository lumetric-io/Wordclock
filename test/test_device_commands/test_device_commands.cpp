#include <gtest/gtest.h>

#include "../mocks/mock_arduino.h"
#include "../mocks/mock_log.h"

// Deliberately not mocks/mock_log.cpp: its setLogLevel() is a no-op, and
// whether the threshold actually moved is the whole assertion for the
// set_log_level command. This stands in for the real one minus the NVS write.
LogLevel LOG_LEVEL = LOG_LEVEL_ERROR;
void log(String, int) {}
void logln(String, int) {}
void setLogLevel(LogLevel level) { LOG_LEVEL = level; }

// The real command handler, compiled natively. Its two hardware dependencies
// (the wall clock and ESP.restart()) are substituted inside the .cpp under
// PIO_UNIT_TESTING, so what runs here is the same parsing and the same
// scheduling decision that runs on a clock.
#include "../../src/device_commands.cpp"

namespace {

class DeviceCommandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        deviceCommandsTestReset();
        LOG_LEVEL = LOG_LEVEL_ERROR;
    }
    void TearDown() override { deviceCommandsTestReset(); }

    // Past the 15 minute uptime guard.
    static const unsigned long UP = 20UL * 60UL * 1000UL;
};

// ---------------------------------------------------------------- envelope

TEST_F(DeviceCommandsTest, PlainOkBodyDoesNothing) {
    deviceCommandsHandleResponse("{\"ok\":true}");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_ERROR);
    EXPECT_FALSE(deviceCommandsTestRebootArmed());
}

TEST_F(DeviceCommandsTest, EmptyCommandsArrayDoesNothing) {
    deviceCommandsHandleResponse("{\"ok\":true,\"commands\":[]}");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_ERROR);
    EXPECT_FALSE(deviceCommandsTestRebootArmed());
}

// A malformed body must be survivable in silence: the beat that carried it has
// already been recorded server-side by the time this runs.
TEST_F(DeviceCommandsTest, MalformedBodyIsIgnored) {
    deviceCommandsHandleResponse("{\"ok\":true,\"commands\":[{\"kind\":");
    deviceCommandsHandleResponse("");
    deviceCommandsHandleResponse("not json at all");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_ERROR);
    EXPECT_FALSE(deviceCommandsTestRebootArmed());
}

// The 4 kB cap is a guard against the portal, not against reality. A body over
// it is refused whole rather than truncated.
TEST_F(DeviceCommandsTest, OversizedBodyIsRefused) {
    String body = "{\"ok\":true,\"commands\":[{\"id\":1,\"kind\":\"set_log_level\","
                  "\"args\":{\"level\":\"debug\"}}],\"pad\":\"";
    for (int i = 0; i < 4200; i++) body = body + "x";
    body = body + "\"}";

    deviceCommandsHandleResponse(body);
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_ERROR);
}

// The whitelist is the blast radius: a kind this firmware does not implement
// does nothing at all, however well formed it is.
TEST_F(DeviceCommandsTest, UnknownKindIsIgnored) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":9,\"kind\":\"set_brightness\",\"args\":{\"value\":255}}]}");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_ERROR);
    EXPECT_FALSE(deviceCommandsTestRebootArmed());
}

TEST_F(DeviceCommandsTest, AtMostFourCommandsAreApplied) {
    // Five commands, the fifth of which would arm a reboot. The portal hands
    // over at most four, so the fifth must never be reached.
    deviceCommandsHandleResponse(
        "{\"commands\":["
        "{\"id\":1,\"kind\":\"nop_a\",\"args\":{}},"
        "{\"id\":2,\"kind\":\"nop_b\",\"args\":{}},"
        "{\"id\":3,\"kind\":\"nop_c\",\"args\":{}},"
        "{\"id\":4,\"kind\":\"nop_d\",\"args\":{}},"
        "{\"id\":5,\"kind\":\"reboot\",\"args\":{\"at\":\"now\"}}]}");
    EXPECT_FALSE(deviceCommandsTestRebootArmed());
}

// --------------------------------------------------------- set_log_level

TEST_F(DeviceCommandsTest, SetLogLevelApplies) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":41,\"kind\":\"set_log_level\","
        "\"args\":{\"level\":\"debug\"}}]}");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_DEBUG);
}

TEST_F(DeviceCommandsTest, EveryLevelNameIsUnderstood) {
    struct { const char* name; LogLevel expected; } cases[] = {
        {"debug", LOG_LEVEL_DEBUG},
        {"info",  LOG_LEVEL_INFO},
        {"warn",  LOG_LEVEL_WARN},
        {"error", LOG_LEVEL_ERROR},
    };
    for (const auto& c : cases) {
        LOG_LEVEL = LOG_LEVEL_INFO;
        String body = String("{\"commands\":[{\"id\":1,\"kind\":\"set_log_level\","
                             "\"args\":{\"level\":\"") + c.name + "\"}}]}";
        deviceCommandsHandleResponse(body);
        EXPECT_EQ(LOG_LEVEL, c.expected) << c.name;
    }
}

// The command is re-sent on every beat until the portal sees the new level on
// a later beat, so applying it repeatedly has to be indistinguishable from
// applying it once.
TEST_F(DeviceCommandsTest, SetLogLevelIsIdempotent) {
    const char* body = "{\"commands\":[{\"id\":41,\"kind\":\"set_log_level\","
                       "\"args\":{\"level\":\"warn\"}}]}";
    for (int i = 0; i < 20; i++) deviceCommandsHandleResponse(body);
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_WARN);
}

TEST_F(DeviceCommandsTest, UnknownLevelLeavesTheThresholdAlone) {
    LOG_LEVEL = LOG_LEVEL_INFO;
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":1,\"kind\":\"set_log_level\","
        "\"args\":{\"level\":\"verbose\"}}]}");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_INFO);

    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":2,\"kind\":\"set_log_level\",\"args\":{}}]}");
    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_INFO);
}

// ----------------------------------------------------------------- reboot

TEST_F(DeviceCommandsTest, ScheduledRebootFiresOnlyAtTheTargetMinute) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":7,\"kind\":\"reboot\",\"args\":{\"at\":\"04:00\"}}]}");
    ASSERT_TRUE(deviceCommandsTestRebootArmed());

    deviceCommandsTestSetLocalTime(true, 3, 59);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);

    deviceCommandsTestSetLocalTime(true, 4, 1);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);

    deviceCommandsTestSetLocalTime(true, 4, 0);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
    EXPECT_FALSE(deviceCommandsTestRebootArmed());
}

// Eighteen beats between issuing and 04:00 each re-arm the same target. That
// must stay one reboot, not eighteen.
TEST_F(DeviceCommandsTest, ReArmingTheSameTargetStillFiresOnce) {
    const char* body = "{\"commands\":[{\"id\":7,\"kind\":\"reboot\","
                       "\"args\":{\"at\":\"04:00\"}}]}";
    deviceCommandsTestSetLocalTime(true, 22, 30);
    for (int i = 0; i < 18; i++) {
        deviceCommandsHandleResponse(body);
        deviceCommandsTick(UP);
    }
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);

    deviceCommandsTestSetLocalTime(true, 4, 0);
    deviceCommandsTick(UP);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

TEST_F(DeviceCommandsTest, RebootNowFiresOnTheNextTick) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":8,\"kind\":\"reboot\",\"args\":{\"at\":\"now\"}}]}");
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

// Missing args at all is "now": the portal defaults it, and so does this.
TEST_F(DeviceCommandsTest, RebootWithoutArgsMeansNow) {
    deviceCommandsHandleResponse("{\"commands\":[{\"id\":8,\"kind\":\"reboot\"}]}");
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

// A clock whose RTC is stuck at the target time would otherwise reboot on
// every boot forever. Fifteen minutes of uptime is the price of one reboot.
TEST_F(DeviceCommandsTest, RebootWaitsForFifteenMinutesOfUptime) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":8,\"kind\":\"reboot\",\"args\":{\"at\":\"now\"}}]}");

    deviceCommandsTick(0);
    deviceCommandsTick(14UL * 60UL * 1000UL);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);

    deviceCommandsTick(15UL * 60UL * 1000UL);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

TEST_F(DeviceCommandsTest, UnsyncedClockNeverRebootsOnSchedule) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":7,\"kind\":\"reboot\",\"args\":{\"at\":\"04:00\"}}]}");
    deviceCommandsTestSetLocalTime(false, 4, 0);
    for (int i = 0; i < 100; i++) deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);
    EXPECT_TRUE(deviceCommandsTestRebootArmed());
}

TEST_F(DeviceCommandsTest, UnusableTimeArmsNothing) {
    const char* bad[] = {"4:00", "04-00", "24:00", "04:60", "0a:00", "", "04:0"};
    for (const char* at : bad) {
        String body = String("{\"commands\":[{\"id\":7,\"kind\":\"reboot\","
                             "\"args\":{\"at\":\"") + at + "\"}}]}";
        deviceCommandsHandleResponse(body);
        EXPECT_FALSE(deviceCommandsTestRebootArmed()) << at;
    }

    deviceCommandsTestSetLocalTime(true, 4, 0);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);
}

TEST_F(DeviceCommandsTest, MidnightAndEndOfDayAreValidTargets) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":7,\"kind\":\"reboot\",\"args\":{\"at\":\"00:00\"}}]}");
    deviceCommandsTestSetLocalTime(true, 0, 0);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);

    deviceCommandsTestReset();
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":7,\"kind\":\"reboot\",\"args\":{\"at\":\"23:59\"}}]}");
    deviceCommandsTestSetLocalTime(true, 23, 59);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

// A later command of the same kind replaces the earlier one server-side (the
// partial unique index), so the device must follow the newest target it was
// handed rather than keeping the first.
TEST_F(DeviceCommandsTest, ANewTargetReplacesTheOldOne) {
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":7,\"kind\":\"reboot\",\"args\":{\"at\":\"04:00\"}}]}");
    deviceCommandsHandleResponse(
        "{\"commands\":[{\"id\":8,\"kind\":\"reboot\",\"args\":{\"at\":\"05:30\"}}]}");

    deviceCommandsTestSetLocalTime(true, 4, 0);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);

    deviceCommandsTestSetLocalTime(true, 5, 30);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

// --------------------------------------------------------------- together

TEST_F(DeviceCommandsTest, BothKindsInOneResponse) {
    deviceCommandsHandleResponse(
        "{\"ok\":true,\"commands\":["
        "{\"id\":41,\"kind\":\"set_log_level\",\"args\":{\"level\":\"error\"}},"
        "{\"id\":42,\"kind\":\"reboot\",\"args\":{\"at\":\"04:00\"}}]}");

    EXPECT_EQ(LOG_LEVEL, LOG_LEVEL_ERROR);
    EXPECT_TRUE(deviceCommandsTestRebootArmed());

    deviceCommandsTestSetLocalTime(true, 4, 0);
    deviceCommandsTick(UP);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 1);
}

// Nothing armed is the state on the overwhelming majority of ticks, and it has
// to cost nothing and do nothing.
TEST_F(DeviceCommandsTest, TickWithoutACommandIsInert) {
    deviceCommandsTestSetLocalTime(true, 4, 0);
    for (int i = 0; i < 1000; i++) deviceCommandsTick(UP + i);
    EXPECT_EQ(deviceCommandsTestRestartCount(), 0);
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
