#pragma once

#include <Arduino.h>

// Commands handed down by the fleet portal (P4.10).
//
// There is no second connection and no broker. The heartbeat response body has
// always been {"ok": true} and this firmware has always read it into a String
// and thrown it away; the portal now puts an optional `commands` array in that
// same body. So the downlink costs one authenticated TLS round trip that the
// clock was already making once an hour, and latency is up to an hour.
//
//   { "ok": true,
//     "commands": [ { "id": 41, "kind": "set_log_level",
//                     "args": { "level": "error" } } ] }
//
// Nothing is acknowledged. The portal decides a command is done by looking at
// what the next beat reports anyway (`logLevel` for set_log_level,
// `logDeleteOnBoot` for set_log_delete_on_boot, `uptime` for reboot), which is
// why every command must be idempotent: a pending one is
// handed over again on every single beat until the portal sees it took, or it
// expires. Applying the same command twenty times must be indistinguishable
// from applying it once.
//
// The whitelist in the .cpp is the blast radius. Nothing the customer can see
// (brightness, colour, language, dialect, night mode) is commandable, and that
// is a policy line rather than a technical one: those settings belong to the
// person who bought the clock. What is commandable is diagnostic only: the log
// threshold, whether logs survive a boot, and a restart.
//
// Design: lumetric/docs/clock-commands.md.

/**
 * Parse the heartbeat response body and apply any commands it carries.
 * Called from sendHeartbeat() on the success path, with the body that was
 * already read and discarded before.
 *
 * Silent about anything it does not like. The beat itself has already
 * succeeded by the time this runs, so a malformed or oversized body is not a
 * reason to report the heartbeat as failed.
 */
void deviceCommandsHandleResponse(const String& body);

/**
 * Drive anything a command scheduled for later, currently the reboot.
 *
 * Belongs near the top of loop(), NOT in runtimeHandleOnlineServices(), which
 * returns early when Wi-Fi is down: a reboot that was accepted at 22:00 must
 * still happen at 04:00 if the network dropped in between.
 *
 * @param nowMs current millis(), which doubles as the uptime guard
 */
void deviceCommandsTick(unsigned long nowMs);

#ifdef PIO_UNIT_TESTING
// Test seams. The two things this module does that a native build cannot are
// read the wall clock and restart the chip, so both are substituted rather
// than mocked out of existence: the scheduling decision is the part worth
// testing and it is made entirely from these two.
void deviceCommandsTestReset();
void deviceCommandsTestSetLocalTime(bool valid, int hour, int minute);
int deviceCommandsTestRestartCount();
bool deviceCommandsTestRebootArmed();
#endif
