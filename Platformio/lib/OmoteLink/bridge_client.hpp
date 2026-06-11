#pragma once



#include "omote_link.hpp"

#include <cstdint>

#include <string>

#include <vector>



namespace bridge_client {



void init();

void onLinked();

void tick();



bool linked();

bool configSynced();

/** True while manifest or file transfer from the bridge is in progress. */
bool syncInProgress();

/** Request a full config re-pull from the bridge (manual, from remote UI). */
void requestConfigPull();

/** Internal: chain a resync after the UI has processed the current manifest. */
void requestQueuedResync();

/** Push all local config files to the bridge (remote backup → bridge). */
void requestPushToBridge();

/** Clear ESP-NOW bridge peer (e.g. after swapping bridge hardware). */
void forgetBridgeLink();

/** Tell bridge the remote is entering deep/light sleep (best-effort before power down). */
void notifyRemoteSleep();

/** Tell bridge the remote is active again (after wake or link-up). */
void notifyRemoteWake();

/** Ask the bridge to poll one HA entity immediately (climate refresh). */
void requestHaEntityPoll(const std::string &entityId);

/** True if relPath (e.g. Scenes/Scene_Foo.json) was in the last completed bridge sync manifest. */
bool isSyncedConfigFile(const std::string &relPath);

/** Scene JSON paths from the last completed sync manifest. */
std::vector<std::string> syncedSceneFiles();

bool callHa(const std::string &domain, const std::string &service, const std::string &entityId,

            const std::string &serviceDataJson = {});

void subscribeEntities(const std::vector<std::string> &entityIds);



/** Called when bridge pushes HA state (also used internally). */

void applyHaState(const std::string &entityId, const std::string &state);

struct BleRemoteStatus {
  bool valid = false;
  bool initialized = false;
  bool connected = false;
  bool advertising = false;
  bool pairing = false;
  bool sceneArmed = false;
  std::string profile;
};

void applyBleStatus(const omote_link::BleStatusPayload &st);

bool sendBleKey(const std::string &keyName);
bool sendBleControl(uint8_t action, const std::string &profile = {});
void requestBleStatus();

/** After leaving a BLE scene, pull debounced config updates sooner. */
void onBleSceneDisarmed();

const BleRemoteStatus &bleStatus();



} // namespace bridge_client

