// Public OTA functions (implementation in ota_updater.cpp)
#pragma once

void checkForFirmwareUpdate();
String getUiVersion();

#if SUPPORT_OTA_V2 == 0
void syncFilesFromManifest();
void syncUiFilesFromConfiguredVersion();
#endif
