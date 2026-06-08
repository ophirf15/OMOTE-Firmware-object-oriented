#include "ble_scene.hpp"

#include <Arduino.h>

#include "HardwareFactory.hpp"
#include "device_settings.hpp"
#include "editor_sync_mode.hpp"

#if OMOTE_BLE

namespace ble_scene {

namespace {

bool sSceneArmed = false;
bool sEditorSyncActive = false;
bool sBlePowered = false;
bool sBleInitPending = false;
bool sSettingsPairingPending = false;
uint32_t sBleInitNotBeforeMs = 0;
uint32_t sArmedAtMs = 0;

static constexpr uint32_t kMinHeapForBleInit = 30000;
static constexpr uint32_t kBleInitDelayMs = 2000;
static constexpr uint32_t kAutoStartDelayMs = 8000;
static constexpr uint32_t kInitRetryMs = 2000;
static constexpr uint32_t kMaxInitFails = 8;
static uint32_t sLastInitAttemptMs = 0;
static uint32_t sInitFailCount = 0;

BleHandlerInterface *handler() {
  auto ble = HardwareFactory::getAbstract().ble();
  return ble.get();
}

bool mayRunBle() {
  return sSceneArmed && !sEditorSyncActive && !editor_sync_mode::isActive();
}

void shutdownBle() {
  sBleInitPending = false;
  sSettingsPairingPending = false;
  sBleInitNotBeforeMs = 0;
  sArmedAtMs = 0;
  sLastInitAttemptMs = 0;
  sInitFailCount = 0;
  if (!sBlePowered)
    return;

  auto *ble = handler();
  if (ble) {
    ble->disconnectClients();
    ble->stopAdvertising();
    ble->shutdown();
  }
  sBlePowered = false;
#ifndef IS_SIMULATOR
  Serial.printf("[BLE] shutdown heap=%u\n", ESP.getFreeHeap());
#endif
}

} // namespace

void armSceneBle(bool enabled) {
  if (sSceneArmed == enabled)
    return;
  sSceneArmed = enabled;
  if (!enabled) {
    shutdownBle();
  } else {
    sArmedAtMs = millis();
#ifndef IS_SIMULATOR
    Serial.printf("[BLE] armed (idle) heap=%u\n", ESP.getFreeHeap());
#endif
  }
}

void disarmSceneBle() { armSceneBle(false); }

bool sceneBleArmed() { return sSceneArmed; }

void requestBleStart() {
  if (!mayRunBle() || sBlePowered)
    return;
  sBleInitNotBeforeMs = millis() + kBleInitDelayMs;
  sBleInitPending = true;
#ifndef IS_SIMULATOR
  Serial.printf("[BLE] start requested heap=%u\n", ESP.getFreeHeap());
#endif
}

void markBleRunning() {
  sBlePowered = true;
  sBleInitPending = false;
  sSettingsPairingPending = false;
  sBleInitNotBeforeMs = 0;
  sArmedAtMs = 0;
}

void requestSettingsPairing() {
  if (sSettingsPairingPending && sBleInitPending)
    return;
  sSettingsPairingPending = true;
  sBleInitNotBeforeMs = millis();
  sBleInitPending = true;
  sInitFailCount = 0;
  sLastInitAttemptMs = 0;
#ifndef IS_SIMULATOR
  Serial.printf("[BLE] settings pairing requested heap=%u\n", ESP.getFreeHeap());
#endif
}

bool settingsPairingPending() { return sSettingsPairingPending; }

void setEditorSyncActive(bool active) {
  if (sEditorSyncActive == active)
    return;
  sEditorSyncActive = active;
  if (active)
    shutdownBle();
}

bool editorSyncActive() { return sEditorSyncActive; }

static void tryStartBle() {
  const bool settingsPairing = sSettingsPairingPending;
  if (sBlePowered) {
    if (settingsPairing) {
      if (auto *ble = handler())
        ble->startPairingMode();
      sSettingsPairingPending = false;
    }
    sBleInitPending = false;
    return;
  }
  if (!settingsPairing && !mayRunBle()) {
    sBleInitPending = false;
    return;
  }
  if (millis() < sBleInitNotBeforeMs)
    return;
  if (ESP.getFreeHeap() < kMinHeapForBleInit) {
#ifndef IS_SIMULATOR
    static uint32_t sLastLowHeapLogMs = 0;
    const uint32_t now = millis();
    if (now - sLastLowHeapLogMs > 3000) {
      sLastLowHeapLogMs = now;
      Serial.printf("[BLE] waiting for heap (%u < %u)\n", ESP.getFreeHeap(), kMinHeapForBleInit);
    }
#endif
    return;
  }

  auto *ble = handler();
  if (!ble) {
    sBleInitPending = false;
    sSettingsPairingPending = false;
    return;
  }

  const uint32_t now = millis();
  if (now - sLastInitAttemptMs < kInitRetryMs)
    return;
  sLastInitAttemptMs = now;

  ble->setProfile(device_settings::currentConst().bleProfile);
  ble->init();
  if (!ble->isInitialized()) {
    sInitFailCount++;
#ifndef IS_SIMULATOR
    if (sInitFailCount == 1 || sInitFailCount >= kMaxInitFails) {
      Serial.printf("[BLE] init failed (%u/%u) heap=%u\n", sInitFailCount, kMaxInitFails,
                    ESP.getFreeHeap());
    }
#endif
    if (sInitFailCount >= kMaxInitFails) {
      sBleInitPending = false;
      sSettingsPairingPending = false;
#ifndef IS_SIMULATOR
      Serial.println("[BLE] init gave up — free RAM or restart device");
#endif
    }
    return;
  }
  sInitFailCount = 0;
  if (settingsPairing)
    ble->startPairingMode();
  else
    ble->onWake();
  sBlePowered = true;
  sBleInitPending = false;
  sSettingsPairingPending = false;
  sArmedAtMs = 0;
#ifndef IS_SIMULATOR
  Serial.printf("[BLE] running heap=%u%s\n", ESP.getFreeHeap(), settingsPairing ? " (pairing)" : "");
#endif
}

void onDisplayWake(uint32_t windowMs) {
  if (!sBlePowered)
    return;
  if (auto *ble = handler())
    ble->onWake(windowMs);
}

void loop() {
  if (sBleInitPending)
    tryStartBle();

  if (!sBlePowered) {
    if (mayRunBle() && !sBleInitPending && sArmedAtMs != 0 &&
        millis() - sArmedAtMs >= kAutoStartDelayMs)
      requestBleStart();
    return;
  }

  if (editor_sync_mode::isActive()) {
    setEditorSyncActive(true);
    return;
  }
  if (auto *ble = handler())
    ble->taskLoop();
}

} // namespace ble_scene

#else // !OMOTE_BLE

namespace ble_scene {

void armSceneBle(bool) {}
void disarmSceneBle() {}
bool sceneBleArmed() { return false; }
void requestBleStart() {}
void markBleRunning() {}
void requestSettingsPairing() {}
bool settingsPairingPending() { return false; }
void setEditorSyncActive(bool) {}
bool editorSyncActive() { return false; }
void onDisplayWake(uint32_t) {}
void loop() {}

} // namespace ble_scene

#endif // OMOTE_BLE
