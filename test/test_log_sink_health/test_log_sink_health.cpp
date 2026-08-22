#include <gtest/gtest.h>

// The real state machine, with no substitutions: it has no hardware
// dependencies at all, which is the point of it living in its own header
// rather than inside log.cpp. What runs here is what runs on a clock.
#include "../../src/log_sink_health.h"

namespace {

TEST(LogSinkHealth, StartsHealthyAndMayAlwaysAttempt) {
    LogSinkHealth h;
    EXPECT_TRUE(h.healthy);
    EXPECT_EQ(0u, h.failures);
    EXPECT_TRUE(h.shouldAttempt(0));
    EXPECT_TRUE(h.shouldAttempt(1000000));
}

// The regression this whole change exists for. The old code set
// fileSinkEnabled = false on a failed open and nothing anywhere set it back,
// so the sink was gone until the next reboot.
TEST(LogSinkHealth, AFailureIsNotPermanent) {
    LogSinkHealth h;
    h.noteFailure(10000, LOG_SINK_RETRY_INTERVAL_MS);
    EXPECT_FALSE(h.healthy);
    EXPECT_FALSE(h.shouldAttempt(10000));
    EXPECT_TRUE(h.shouldAttempt(10000 + LOG_SINK_RETRY_INTERVAL_MS));
}

TEST(LogSinkHealth, RetryIsThrottledUntilTheIntervalHasPassed) {
    LogSinkHealth h;
    h.noteFailure(50000, 60000);
    EXPECT_FALSE(h.shouldAttempt(50001));
    EXPECT_FALSE(h.shouldAttempt(109999));
    EXPECT_TRUE(h.shouldAttempt(110000));
    EXPECT_TRUE(h.shouldAttempt(200000));
}

TEST(LogSinkHealth, SuccessClearsTheThrottleImmediately) {
    LogSinkHealth h;
    h.noteFailure(50000, 60000);
    ASSERT_FALSE(h.shouldAttempt(50001));
    h.noteSuccess();
    EXPECT_TRUE(h.healthy);
    EXPECT_TRUE(h.shouldAttempt(50001));
}

// Incidents, not attempts. A sink that is already down and fails another
// reopen has not broken twice; a counter that ticked per attempt would climb
// by one a minute forever and read on the fleet dashboard as an escalating
// fault when it is one unchanged one.
TEST(LogSinkHealth, RepeatedFailuresWhileDownCountOnce) {
    LogSinkHealth h;
    h.noteFailure(1000, 60000);
    h.noteFailure(61000, 60000);
    h.noteFailure(121000, 60000);
    EXPECT_EQ(1u, h.failures);
}

TEST(LogSinkHealth, RecoveryThenAnotherFailureCountsTwice) {
    LogSinkHealth h;
    h.noteFailure(1000, 60000);
    h.noteSuccess();
    h.noteFailure(500000, 60000);
    EXPECT_EQ(2u, h.failures);
}

// A recovered sink is still a sink that dropped out. This is why the heartbeat
// carries the count and not just a boolean: with only logSinkOk, a clock that
// lost an hour of logs at 03:00 and recovered is indistinguishable from one
// that never faltered.
TEST(LogSinkHealth, RecoveryDoesNotEraseTheHistory) {
    LogSinkHealth h;
    h.noteFailure(1000, 60000);
    h.noteSuccess();
    EXPECT_TRUE(h.healthy);
    EXPECT_EQ(1u, h.failures);
}

// millis() wraps every 49.7 days, and a clock that has been up that long is
// exactly the one being asked to log an intermittent fault. With a plain
// nowMs >= retryAtMs comparison the retry would be deferred for 49 days.
TEST(LogSinkHealth, RetryStillFiresAcrossTheMillisRollover) {
    const unsigned long nearWrap = 0xFFFFFFFFUL - 10000UL;
    LogSinkHealth h;
    h.noteFailure(nearWrap, 60000);   // retryAtMs wraps past zero

    EXPECT_FALSE(h.shouldAttempt(nearWrap + 1000));
    EXPECT_FALSE(h.shouldAttempt(nearWrap + 9999));   // still before the wrap
    EXPECT_TRUE(h.shouldAttempt(nearWrap + 60000));   // due, having wrapped
    EXPECT_TRUE(h.shouldAttempt(nearWrap + 70000));
}

TEST(LogSinkHealth, ADueRetryStaysDueUntilItIsTaken) {
    LogSinkHealth h;
    h.noteFailure(1000, 60000);
    ASSERT_TRUE(h.shouldAttempt(61000));
    // No state change from asking. The caller decides, and if the open fails
    // again it is noteFailure() that pushes the deadline out.
    EXPECT_TRUE(h.shouldAttempt(61000));
    h.noteFailure(61000, 60000);
    EXPECT_FALSE(h.shouldAttempt(61001));
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
