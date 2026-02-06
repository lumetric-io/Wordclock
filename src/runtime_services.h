#pragma once

#include <Arduino.h>

class WebServer;
class StartupSequence;

void runtimeInitOnSetup(bool wifiConnected, WebServer& server);
void runtimeHandleWifiTransitionLogs(bool wifiConnected);
bool runtimeHandleNoWifiLoop(unsigned long nowMs);
void runtimeEnsureOnlineServices(WebServer& server);
void runtimeHandleOnlineServices(WebServer& server);
void runtimeHandlePeriodicSettings(unsigned long nowMs, unsigned long intervalMs);
bool runtimeHandleLedEvents(unsigned long nowMs);
bool runtimeHandleStartupSequence(StartupSequence& startupSequence);
void runtimeHandleWordclockLoop(unsigned long nowMs);
