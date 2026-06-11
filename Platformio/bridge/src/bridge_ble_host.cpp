#include "bridge_ble_host.hpp"

#include "ble_handler.hpp"
#include "omote_link.hpp"

#include <Arduino.h>
#include <LittleFS.h>

#ifndef FS_PATH
#define FS_PATH "/littlefs/"
#endif

namespace bridge_ble_host {
namespace {

std::shared_ptr<BleHandlerInterface> sBle;
bool sSceneArmed = false;
bool sPendingStart = false;
bool sPairingPending = false;
uint32_t sPendingStartMs = 0;
std::string sProfile = "generic";

static constexpr uint32_t kMinHeapForBleInit = 28000;
static constexpr uint32_t kStartDelayMs = 500;

std::string readBleProfileFromDisk() {
  File f = LittleFS.open(FS_PATH "DeviceSettings.json", "r");
  if (!f)
    return "generic";
  const String body = f.readString();
  f.close();
  const int key = body.indexOf("\"ble_profile\"");
  if (key < 0)
    return "generic";
  const int q1 = body.indexOf('"', key + 13);
  const int q2 = body.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 <= q1)
    return "generic";
  return body.substring(q1 + 1, q2).c_str();
}

bool ensureBle() {
  if (!sBle)
    sBle = createBleHandler();
  if (!sBle)
    return false;
  if (sBle->isInitialized())
    return true;
  if (ESP.getFreeHeap() < kMinHeapForBleInit) {
    Serial.printf("[bridge_ble] init deferred heap=%u\n", ESP.getFreeHeap());
    return false;
  }
  sBle->setProfile(sProfile);
  sBle->init();
  if (!sBle->isInitialized())
    return false;
  if (sPairingPending)
    sBle->startPairingMode();
  else
    sBle->onWake();
  return true;
}

void scheduleStart(bool pairing) {
  sPairingPending = pairing;
  sPendingStart = true;
  sPendingStartMs = millis() + kStartDelayMs;
}

} // namespace

void init() {
  reloadProfileFromDisk();
  sBle = createBleHandler();
  Serial.println("[bridge_ble] host ready");
}

void reloadProfileFromDisk() {
  sProfile = readBleProfileFromDisk();
  if (sProfile.empty())
    sProfile = "generic";
  if (sBle)
    sBle->setProfile(sProfile);
}

void tick() {
  if (sPendingStart && millis() >= sPendingStartMs) {
    if (ensureBle()) {
      sPendingStart = false;
      Serial.println(sPairingPending ? "[bridge_ble] stack running (pairing)"
                                     : "[bridge_ble] stack running");
    } else {
      sPendingStartMs = millis() + kStartDelayMs;
    }
  }
  if (sBle && sBle->isInitialized())
    sBle->taskLoop();
}

bool available() { return sBle != nullptr; }

void setSceneArmed(bool armed) {
  sSceneArmed = armed;
  if (!armed && sBle && sBle->isInitialized() && !sPairingPending) {
    sBle->disconnectClients();
    sBle->stopAdvertising();
    sBle->shutdown();
    Serial.println("[bridge_ble] disarmed — stack stopped");
  }
}

bool sceneArmed() { return sSceneArmed; }

bool sendKey(const std::string &keyName) {
  if (keyName.empty())
    return false;
  if (!sSceneArmed && !sPairingPending) {
    Serial.println("[bridge_ble] key ignored — scene not armed");
    return false;
  }
  if (!ensureBle()) {
    scheduleStart(false);
    return false;
  }
  sBle->sendKey(keyName);
  Serial.printf("[bridge_ble] key %s\n", keyName.c_str());
  return true;
}

bool control(uint8_t action, const std::string &profile) {
  const auto act = static_cast<omote_link::BleControlAction>(action);
  switch (act) {
  case omote_link::BleControlAction::ArmScene:
    setSceneArmed(true);
    return true;
  case omote_link::BleControlAction::DisarmScene:
    setSceneArmed(false);
    return true;
  case omote_link::BleControlAction::EnsureRunning:
    scheduleStart(false);
    return true;
  case omote_link::BleControlAction::StartPairing:
    if (!profile.empty()) {
      sProfile = profile;
      if (sBle)
        sBle->setProfile(sProfile);
    }
    scheduleStart(true);
    Serial.println("[bridge_ble] pairing scheduled (async init)");
    return true;
  case omote_link::BleControlAction::StopPairing:
    sPairingPending = false;
    if (sBle)
      sBle->stopPairingMode();
    return true;
  case omote_link::BleControlAction::Disconnect:
    if (!sBle || !sBle->isInitialized()) {
      scheduleStart(sPairingPending);
      return true;
    }
    sBle->disconnectClients();
    return true;
  case omote_link::BleControlAction::ForgetBonds:
    if (!sBle || !sBle->isInitialized()) {
      scheduleStart(true);
      return true;
    }
    sBle->forgetBonds();
    return true;
  case omote_link::BleControlAction::SetProfile:
    if (profile.empty())
      return false;
    sProfile = profile;
    reloadProfileFromDisk();
    if (sBle) {
      sBle->setProfile(sProfile);
      sBle->forgetBonds();
    }
    return true;
  default:
    return false;
  }
}

std::string statusJson() {
  if (!sBle)
    return "{\"available\":false}";
  return sBle->statusJson();
}

std::string identityListJson() {
  if (!sBle)
    sBle = createBleHandler();
  if (!sBle)
    return "{\"identities\":[]}";
  return sBle->identityListJson();
}

void sendStatusToRemote(const uint8_t mac[6]) {
  omote_link::BleStatusPayload st = {};
  if (sBle) {
    st.initialized = sBle->isInitialized() ? 1 : 0;
    st.connected = sBle->isConnected() ? 1 : 0;
    st.advertising = sBle->isAdvertising() ? 1 : 0;
    st.pairing = sBle->isPairingMode() ? 1 : 0;
    const std::string prof = sBle->currentProfile();
    st.profileLen = static_cast<uint8_t>(std::min(prof.size(), sizeof(st.profile) - 1));
    memcpy(st.profile, prof.c_str(), st.profileLen);
  }
  st.sceneArmed = sSceneArmed ? 1 : 0;
  omote_link::sendToMac(mac, omote_link::MsgType::BleStatus, &st, sizeof(st));
}

} // namespace bridge_ble_host
