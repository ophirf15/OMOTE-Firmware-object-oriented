#pragma once



#include <cstdint>



namespace omote_link {



enum class Role : uint8_t { Client = 0, Host = 1 };



enum class LinkState : uint8_t {

  Uninitialized = 0,

  Ready,

  Linked,

};



/** OLP message types (version 1). */

enum class MsgType : uint8_t {

  Ping = 1,

  Pong = 2,

  ConfigManifestReq = 10,

  ConfigManifest = 11,

  ConfigFileReq = 12,

  ConfigFileChunk = 13,

  /** Remote → bridge: start receiving a config file write. */
  ConfigFilePushStart = 14,

  /** Remote → bridge: file body chunk (same header as ConfigFileChunk). */
  ConfigFilePushChunk = 15,

  HaCall = 20,

  HaState = 21,

  HaSubscribe = 22,

  ConfigChanged = 23,

  BleSendKey = 30,

  BleControl = 31,

  BleStatusReq = 32,

  BleStatus = 33,

  HaStateAttrs = 34,

  /** Remote → bridge: poll one HA entity immediately. */
  HaPollReq = 35,

};



constexpr uint16_t kMaxPayload = 234;

enum : uint8_t { kMaxHaEntityIdLen = 96 };



struct __attribute__((packed)) HaCallPayload {

  char domain[16];

  char service[24];

  char entityId[kMaxHaEntityIdLen];

  uint8_t dataLen;

  char data[94];

};



struct __attribute__((packed)) HaStatePayload {

  char entityId[kMaxHaEntityIdLen];

  char state[32];

};

/** Chunked HA attributes JSON (bridge → remote, follows HaState). */
struct __attribute__((packed)) HaStateAttrsPayload {
  char entityId[kMaxHaEntityIdLen];
  uint8_t part;
  uint8_t total;
  uint8_t dataLen;
  char data[kMaxPayload - kMaxHaEntityIdLen - 3];
};



enum class BleControlAction : uint8_t {
  StartPairing = 1,
  StopPairing = 2,
  Disconnect = 3,
  ForgetBonds = 4,
  SetProfile = 5,
  ArmScene = 6,
  DisarmScene = 7,
  EnsureRunning = 8,
};

struct __attribute__((packed)) BleSendKeyPayload {
  char key[48];
};

struct __attribute__((packed)) BleControlPayload {
  uint8_t action;
  char profile[32];
};

struct __attribute__((packed)) BleStatusPayload {
  uint8_t initialized;
  uint8_t connected;
  uint8_t advertising;
  uint8_t pairing;
  uint8_t sceneArmed;
  uint8_t profileLen;
  char profile[32];
};

/** Newline-separated entity_id list chunk (remote → bridge). */
struct __attribute__((packed)) HaSubscribePayload {

  uint8_t part;

  uint8_t total;

  uint8_t dataLen;

  char entities[kMaxPayload - 3];

};



struct __attribute__((packed)) ConfigFileReqPayload {

  char path[120];

};



struct __attribute__((packed)) ConfigFileChunkHeader {

  uint16_t offset;

  uint16_t totalSize;

  uint8_t dataLen;

};

struct __attribute__((packed)) ConfigFilePushStartPayload {
  char path[120];
  uint16_t totalSize;
};

struct __attribute__((packed)) HaPollReqPayload {
  char entityId[kMaxHaEntityIdLen];
};



/** Last pong / status from bridge (valid when linked). */

struct BridgeStatus {

  uint8_t bridgeMac[6] = {};

  uint8_t wifiChannel = 0;

  uint32_t bridgeHeap = 0;

  uint32_t lastRxMs = 0;

};



using MessageHandler = void (*)(MsgType type, const uint8_t *payload, uint16_t len, const uint8_t srcMac[6]);



void init(Role role);

void tick();

LinkState state();

const BridgeStatus &bridgeStatus();

uint32_t pingsSent();

uint32_t pongsReceived();



void setMessageHandler(MessageHandler handler);

bool sendToPeer(MsgType type, const void *payload, uint16_t len);

bool sendToMac(const uint8_t mac[6], MsgType type, const void *payload, uint16_t len);

bool peerMac(uint8_t out[6]);

/** Host role: true if a remote has contacted this bridge recently. */
bool hostHasKnownClient();

void onLinkEstablished();



} // namespace omote_link

