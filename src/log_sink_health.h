#pragma once

#include <stdint.h>

// Health of the log file sink, as a small pure state machine.
//
// It exists as its own type for one reason: before this, the sink's failure
// handling was a single line inside ensureLogFile() reading
// `fileSinkEnabled = false`, and nothing anywhere set it back. The only re-arm
// was logEnableFileSink(), which runs at boot and from the clear-logs route. So
// one failed open cost a clock its log file until somebody restarted it, with
// no trace: the RAM ring buffer kept answering /log, both log settings kept
// reporting correctly, and every heartbeat kept arriving. That is exactly what
// happened to 192.168.20.131 on 2026-08-22, and it took a byte-level growth
// check to see it at all (tools/test-log-features.sh, phase 2).
//
// A failure must therefore be recoverable and it must be countable. Splitting
// the decision out of log.cpp is what makes it testable natively, since the
// non-test half of log.cpp is compiled out under PIO_UNIT_TESTING and its
// dependencies are LittleFS and the wall clock.
struct LogSinkHealth {
  bool healthy = true;
  uint32_t failures = 0;       // incidents since boot, not attempts
  unsigned long retryAtMs = 0;

  // Counted once per incident. A sink that is already down and fails another
  // reopen has not broken twice, and a counter that ticked per attempt would
  // read on the fleet dashboard as an escalating fault when it is one
  // unchanged one. What the count answers is "did this clock's logging ever
  // drop out since it booted", which is a question a single healthy/unhealthy
  // flag cannot answer once the sink has recovered.
  void noteFailure(unsigned long nowMs, unsigned long retryIntervalMs) {
    if (healthy) {
      failures++;
    }
    healthy = false;
    retryAtMs = nowMs + retryIntervalMs;
  }

  void noteSuccess() {
    healthy = true;
  }

  // May the sink try to open its file right now?
  //
  // Always yes while healthy, so the ordinary path (first line after boot, and
  // every date rollover) pays nothing for this. While unhealthy it is throttled,
  // because a clock at debug level logs several times a second and one failure
  // would otherwise become a filesystem operation per line: a directory scan
  // and an open, forever, on a device that is already unhappy.
  bool shouldAttempt(unsigned long nowMs) const {
    if (healthy) {
      return true;
    }
    // Signed difference, not nowMs >= retryAtMs. millis() wraps every 49.7
    // days and a clock that has been up that long is precisely the one being
    // asked to log an intermittent fault; the naive comparison would stop
    // retrying for 49 days across the wrap.
    return (long)(nowMs - retryAtMs) >= 0;
  }
};

// Fixed, not escalating. A backoff would make the failure count harder to read
// against the hourly heartbeat (how long has it been down? depends how long it
// had been down) and buys nothing here: the retry is one open() against a local
// filesystem, and the fault it recovers from is either transient or permanent,
// never load-related.
static const unsigned long LOG_SINK_RETRY_INTERVAL_MS = 60000UL;
