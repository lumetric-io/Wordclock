#pragma once

#include "ui_auth.h"
#include "secrets.h"
#include "wordclock.h"
#include "log.h"

// Initialize UI authentication and wordclock setup
// This function initializes UI authentication and performs the wordclock setup.
// Called after display init so everything is properly secured and configured.
inline void initWordclockSystem(UiAuth& uiAuth) {
    uiAuth.begin(UI_DEFAULT_PASS);
    wordclock_setup();
    logInfo("🟢 Wordclock system initialized");
}
