#pragma once



#include <cstdint>



/** Scene-gated BLE lifecycle. Stack starts only on explicit request (key, pairing, delayed reconnect). */

namespace ble_scene {



/** Allow BLE in the current scene without starting the stack yet. */

void armSceneBle(bool enabled);

void disarmSceneBle();

bool sceneBleArmed();



/** Start NimBLE when armed (no-op if already running or not armed). */

void requestBleStart();

/** BLE was started outside scene flow (e.g. Settings pairing). Keeps supervisor alive. */

void markBleRunning();

/** Settings → Start pairing: init when heap allows (works while scene is disarmed). */

void requestSettingsPairing();

bool settingsPairingPending();



void setEditorSyncActive(bool active);

bool editorSyncActive();



void onDisplayWake(uint32_t windowMs = 60000);

void loop();



} // namespace ble_scene

