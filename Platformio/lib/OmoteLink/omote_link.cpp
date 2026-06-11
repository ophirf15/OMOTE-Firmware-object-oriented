#include "omote_link.hpp"



#include <Arduino.h>

#include <WiFi.h>

#include <esp_now.h>

#include <nvs.h>

#include <nvs_flash.h>

#include <cstring>

#include <deque>

#include <freertos/queue.h>
#include <freertos/portmacro.h>



namespace omote_link {

namespace {



constexpr char kMagic0 = 'O';

constexpr char kMagic1 = 'L';

constexpr uint8_t kVersion = 1;

constexpr uint32_t kClientPingMs = 2000;

constexpr uint32_t kLinkTimeoutMs = 8000;



constexpr uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};



struct __attribute__((packed)) Header {

  uint8_t magic0;

  uint8_t magic1;

  uint8_t version;

  uint8_t type;

  uint16_t seq;

  uint16_t payloadLen;

};



struct __attribute__((packed)) PingPayload {

  uint32_t senderHeap;

};



struct __attribute__((packed)) PongPayload {

  uint8_t hostMac[6];

  uint8_t wifiChannel;

  uint32_t hostHeap;

};



Role sRole = Role::Client;

LinkState sState = LinkState::Uninitialized;

BridgeStatus sStatus = {};

uint16_t sSeq = 0;

uint32_t sLastPingMs = 0;

uint32_t sPingsSent = 0;

uint32_t sPongsReceived = 0;

bool sEspNowReady = false;

uint8_t sPeerMac[6] = {};

bool sPeerKnown = false;

MessageHandler sHandler = nullptr;

bool sLinkNotified = false;

uint8_t sLastClientMac[6] = {};

struct QueuedMsg {
  uint8_t type;
  uint8_t srcMac[6];
  uint16_t len;
  uint8_t payload[kMaxPayload];
};

QueueHandle_t sMsgQueue = nullptr;
static constexpr size_t kMsgQueueDepth = 16;

struct OutboundPkt {
  uint8_t type = 0;
  uint8_t dest[6] = {};
  uint16_t len = 0;
  uint8_t payload[kMaxPayload] = {};
  uint8_t retries = 0;
};

std::deque<OutboundPkt> sHighTxQueue;
std::deque<OutboundPkt> sNormTxQueue;
bool sTxInFlight = false;
OutboundPkt sInflightPkt;
bool sHasInflightPkt = false;
uint32_t sLastBleKeyQueuedMs = 0;
portMUX_TYPE sTxMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool sTxKick = false;

static constexpr size_t kHighTxQueueMax = 20;
static constexpr size_t kNormTxQueueMax = 8;
static constexpr uint8_t kMaxTxRetries = 4;
static constexpr uint32_t kPingDeferAfterBleKeyMs = 400;

bool isHighPriorityTx(uint8_t type) {
  switch (static_cast<MsgType>(type)) {
  case MsgType::BleSendKey:
  case MsgType::Pong:
  case MsgType::BleStatus:
  case MsgType::HaState:
  case MsgType::HaStateAttrs:
    return true;
  default:
    return false;
  }
}

/** Must not drop when the TX queue is full — large file sync depends on every chunk. */
bool isReliableTx(uint8_t type) {
  switch (static_cast<MsgType>(type)) {
  case MsgType::ConfigFileChunk:
  case MsgType::ConfigFilePushChunk:
  case MsgType::ConfigManifest:
    return true;
  default:
    return false;
  }
}

void flushTxQueue();

bool ensureEspNow();

void onDataSent(const uint8_t *macAddr, esp_now_send_status_t status) {
  (void)macAddr;
  portENTER_CRITICAL(&sTxMux);
  sTxInFlight = false;
  if (status != ESP_NOW_SEND_SUCCESS && sHasInflightPkt) {
    if (sInflightPkt.retries < kMaxTxRetries) {
      sInflightPkt.retries++;
      auto &q = isHighPriorityTx(sInflightPkt.type) ? sHighTxQueue : sNormTxQueue;
      q.push_front(sInflightPkt);
    } else {
      Serial.printf("[OLP] tx drop type=%u\n", sInflightPkt.type);
    }
  }
  sHasInflightPkt = false;
  portEXIT_CRITICAL(&sTxMux);
  sTxKick = true;
}

void flushTxQueue() {
  if (sTxInFlight)
    return;

  OutboundPkt pkt;
  bool havePkt = false;
  portENTER_CRITICAL(&sTxMux);
  if (!sHighTxQueue.empty()) {
    pkt = sHighTxQueue.front();
    sHighTxQueue.pop_front();
    havePkt = true;
  } else if (!sNormTxQueue.empty()) {
    pkt = sNormTxQueue.front();
    sNormTxQueue.pop_front();
    havePkt = true;
  }
  if (havePkt) {
    sInflightPkt = pkt;
    sHasInflightPkt = true;
    sTxInFlight = true;
  }
  portEXIT_CRITICAL(&sTxMux);

  if (!havePkt)
    return;

  uint8_t buf[sizeof(Header) + kMaxPayload] = {};
  auto *hdr = reinterpret_cast<Header *>(buf);
  hdr->magic0 = kMagic0;
  hdr->magic1 = kMagic1;
  hdr->version = kVersion;
  hdr->type = pkt.type;
  hdr->seq = sSeq++;
  hdr->payloadLen = pkt.len;
  if (pkt.len)
    memcpy(buf + sizeof(Header), pkt.payload, pkt.len);

  if (esp_now_send(pkt.dest, buf, sizeof(Header) + pkt.len) != ESP_OK) {
    portENTER_CRITICAL(&sTxMux);
    sTxInFlight = false;
    sHasInflightPkt = false;
    auto &q = isHighPriorityTx(pkt.type) ? sHighTxQueue : sNormTxQueue;
    if (pkt.retries < kMaxTxRetries) {
      pkt.retries++;
      q.push_front(pkt);
    }
    portEXIT_CRITICAL(&sTxMux);
    sTxKick = true;
  }
}

bool enqueuePacket(uint8_t type, const void *payload, uint16_t payloadLen, const uint8_t dest[6]) {
  if (!dest || payloadLen > kMaxPayload)
    return false;

  if (type == static_cast<uint8_t>(MsgType::BleSendKey))
    sLastBleKeyQueuedMs = millis();

  OutboundPkt pkt;
  pkt.type = type;
  memcpy(pkt.dest, dest, 6);
  pkt.len = payloadLen;
  if (payload && payloadLen)
    memcpy(pkt.payload, payload, payloadLen);

  portENTER_CRITICAL(&sTxMux);
  if (type == static_cast<uint8_t>(MsgType::BleSendKey) && !sHighTxQueue.empty()) {
    const auto &back = sHighTxQueue.back();
    if (back.type == type && back.len == payloadLen &&
        (payloadLen == 0 || memcmp(back.payload, payload, payloadLen) == 0)) {
      portEXIT_CRITICAL(&sTxMux);
      return true;
    }
  }
  auto &q = isHighPriorityTx(type) ? sHighTxQueue : sNormTxQueue;
  const size_t maxQ = isHighPriorityTx(type) ? kHighTxQueueMax : kNormTxQueueMax;
  if (isReliableTx(type)) {
    if (q.size() >= maxQ) {
      portEXIT_CRITICAL(&sTxMux);
      return false;
    }
  } else {
    while (q.size() >= maxQ)
      q.pop_front();
  }
  q.push_back(pkt);
  portEXIT_CRITICAL(&sTxMux);
  sTxKick = true;
  return true;
}

void pumpTxQueue() {
  if (!sTxKick && sTxInFlight)
    return;
  sTxKick = false;
  for (int i = 0; i < 6 && !sTxInFlight; ++i) {
    portENTER_CRITICAL(&sTxMux);
    const bool pending = !sHighTxQueue.empty() || !sNormTxQueue.empty();
    portEXIT_CRITICAL(&sTxMux);
    if (!pending)
      break;
    flushTxQueue();
  }
}

void dispatchMessage(uint8_t type, const uint8_t *payload, uint16_t len, const uint8_t srcMac[6]);

void ensureMsgQueue() {
  if (!sMsgQueue)
    sMsgQueue = xQueueCreate(kMsgQueueDepth, sizeof(QueuedMsg));
}

void drainMessageQueue() {
  if (!sMsgQueue)
    return;
  QueuedMsg msg;
  while (xQueueReceive(sMsgQueue, &msg, 0) == pdTRUE)
    dispatchMessage(msg.type, msg.payload, msg.len, msg.srcMac);
}

bool enqueueMessage(uint8_t type, const uint8_t *payload, uint16_t len, const uint8_t srcMac[6]) {
  ensureMsgQueue();
  if (!sMsgQueue)
    return false;
  QueuedMsg msg = {};
  msg.type = type;
  memcpy(msg.srcMac, srcMac, 6);
  msg.len = len;
  if (len && payload)
    memcpy(msg.payload, payload, len);
  if (xQueueSend(sMsgQueue, &msg, 0) != pdTRUE) {
    Serial.println("[OLP] message queue full");
    return false;
  }
  return true;
}

bool headerValid(const Header &h, size_t len) {

  return len >= sizeof(Header) && h.magic0 == kMagic0 && h.magic1 == kMagic1 && h.version == kVersion &&

         h.payloadLen <= len - sizeof(Header) && h.payloadLen <= kMaxPayload;

}



void savePeerMac(const uint8_t mac[6]) {

  nvs_handle_t h;

  if (nvs_open("omote_link", NVS_READWRITE, &h) != ESP_OK)

    return;

  nvs_set_blob(h, "peer", mac, 6);

  nvs_commit(h);

  nvs_close(h);

  memcpy(sPeerMac, mac, 6);

  sPeerKnown = true;

}



bool loadPeerMac() {

  nvs_handle_t h;

  if (nvs_open("omote_link", NVS_READONLY, &h) != ESP_OK)

    return false;

  size_t len = 6;

  const esp_err_t err = nvs_get_blob(h, "peer", sPeerMac, &len);

  nvs_close(h);

  if (err != ESP_OK || len != 6)

    return false;

  sPeerKnown = true;

  return true;

}



bool addPeer(const uint8_t mac[6], bool encrypt = false) {

  if (esp_now_is_peer_exist(mac))

    esp_now_del_peer(mac);



  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, mac, 6);

  peer.channel = WiFi.channel() ? WiFi.channel() : 1;

  peer.encrypt = encrypt;

  if (encrypt) {

    const uint8_t lmk[16] = {0};

    memcpy(peer.lmk, lmk, 16);

  }

  return esp_now_add_peer(&peer) == ESP_OK;

}



bool sendPacket(uint8_t type, const void *payload, uint16_t payloadLen, const uint8_t dest[6]) {
  if (!ensureEspNow())
    return false;
  return enqueuePacket(type, payload, payloadLen, dest);
}



void dispatchMessage(uint8_t type, const uint8_t *payload, uint16_t len, const uint8_t srcMac[6]) {

  if (!sHandler)

    return;

  sHandler(static_cast<MsgType>(type), payload, len, srcMac);

}



void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {

  if (!info || !data || len < static_cast<int>(sizeof(Header)))

    return;



  Header hdr;

  memcpy(&hdr, data, sizeof(hdr));

  if (!headerValid(hdr, static_cast<size_t>(len)))

    return;



  const uint8_t *payload = data + sizeof(Header);

  const uint8_t msgType = hdr.type;



  if (msgType == static_cast<uint8_t>(MsgType::Ping) && sRole == Role::Host) {

    memcpy(sLastClientMac, info->src_addr, 6);

    addPeer(info->src_addr, false);

    PongPayload pong = {};

    WiFi.macAddress(pong.hostMac);

    pong.wifiChannel = static_cast<uint8_t>(WiFi.channel());

    pong.hostHeap = ESP.getFreeHeap();

    sendPacket(static_cast<uint8_t>(MsgType::Pong), &pong, sizeof(pong), info->src_addr);

    Serial.printf("[OLP] ping from %02X:%02X:%02X:%02X:%02X:%02X heap=%u\n", info->src_addr[0], info->src_addr[1],

                  info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5], ESP.getFreeHeap());

    return;

  }



  if (msgType == static_cast<uint8_t>(MsgType::Pong) && sRole == Role::Client) {

    if (hdr.payloadLen < sizeof(PongPayload))

      return;

    PongPayload pong;

    memcpy(&pong, payload, sizeof(pong));

    memcpy(sStatus.bridgeMac, pong.hostMac, 6);

    sStatus.wifiChannel = pong.wifiChannel;

    sStatus.bridgeHeap = pong.hostHeap;

    sStatus.lastRxMs = millis();

    sPongsReceived++;

    const bool wasLinked = sState == LinkState::Linked;

    sState = LinkState::Linked;



    if (!sPeerKnown || memcmp(sPeerMac, info->src_addr, 6) != 0) {

      savePeerMac(info->src_addr);

      addPeer(info->src_addr, false);

      Serial.printf("[OLP] linked bridge %02X:%02X:%02X:%02X:%02X:%02X ch=%u heap=%u\n", info->src_addr[0],

                    info->src_addr[1], info->src_addr[2], info->src_addr[3], info->src_addr[4], info->src_addr[5],

                    pong.wifiChannel, pong.hostHeap);

    }

    if (!wasLinked && !sLinkNotified) {

      sLinkNotified = true;

      onLinkEstablished();

    }

    return;

  }



  if (sRole == Role::Host) {
    memcpy(sLastClientMac, info->src_addr, 6);
    addPeer(info->src_addr, false);
    /* BLE / control bypass the queue so config file transfers cannot starve HID keys. */
    if (msgType == static_cast<uint8_t>(MsgType::BleSendKey) ||
        msgType == static_cast<uint8_t>(MsgType::BleControl) ||
        msgType == static_cast<uint8_t>(MsgType::BleStatusReq)) {
      dispatchMessage(msgType, payload, hdr.payloadLen, info->src_addr);
      return;
    }
  }

  enqueueMessage(msgType, payload, hdr.payloadLen, info->src_addr);

}



bool ensureEspNow() {

  if (sEspNowReady)

    return true;



  esp_err_t nvs = nvs_flash_init();

  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {

    nvs_flash_erase();

    nvs_flash_init();

  }



  if (WiFi.getMode() == WIFI_OFF)

    WiFi.mode(WIFI_STA);



  if (esp_now_init() != ESP_OK) {

    Serial.println("[OLP] esp_now_init failed");

    return false;

  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  sEspNowReady = true;

  sState = LinkState::Ready;

  Serial.printf("[OLP] ready role=%s ch=%u heap=%u\n", sRole == Role::Host ? "host" : "client", WiFi.channel(),

                ESP.getFreeHeap());

  return true;

}



void clientTick() {

  if (!ensureEspNow())

    return;



  if (!sPeerKnown)

    loadPeerMac();



  if (sPeerKnown)

    addPeer(sPeerMac, false);

  else

    addPeer(kBroadcast, false);



  const uint32_t now = millis();

  if (!sHighTxQueue.empty() || sTxInFlight)
    return;

  if (sLastBleKeyQueuedMs && now - sLastBleKeyQueuedMs < kPingDeferAfterBleKeyMs)
    return;

  if (now - sLastPingMs < kClientPingMs)
    return;

  sLastPingMs = now;



  PingPayload ping = {ESP.getFreeHeap()};

  const uint8_t *dest = sPeerKnown ? sPeerMac : kBroadcast;

  sendPacket(static_cast<uint8_t>(MsgType::Ping), &ping, sizeof(ping), dest);

  sPingsSent++;



  if (sState == LinkState::Linked && sStatus.lastRxMs && now - sStatus.lastRxMs > kLinkTimeoutMs) {

    sState = LinkState::Ready;

    sLinkNotified = false;

  }

}



void hostTick() {

  if (!ensureEspNow())

    return;

  addPeer(kBroadcast, false);

}



} // namespace



void init(Role role) {

  sRole = role;

  sState = LinkState::Uninitialized;

  sEspNowReady = false;

  sLinkNotified = false;

  ensureMsgQueue();

  if (role == Role::Client)

    loadPeerMac();

}



void tick() {

  drainMessageQueue();
  pumpTxQueue();

  if (sRole == Role::Host)

    hostTick();

  else

    clientTick();

}



LinkState state() { return sState; }



const BridgeStatus &bridgeStatus() { return sStatus; }



uint32_t pingsSent() { return sPingsSent; }



uint32_t pongsReceived() { return sPongsReceived; }



void setMessageHandler(MessageHandler handler) { sHandler = handler; }



bool sendToPeer(MsgType type, const void *payload, uint16_t len) {

  if (!ensureEspNow())

    return false;

  if (sRole == Role::Client) {

    if (!sPeerKnown)

      return false;

    return sendPacket(static_cast<uint8_t>(type), payload, len, sPeerMac);

  }

  if (sLastClientMac[0] == 0 && sLastClientMac[1] == 0 && sLastClientMac[2] == 0 && sLastClientMac[3] == 0 &&

      sLastClientMac[4] == 0 && sLastClientMac[5] == 0)

    return false;

  return sendPacket(static_cast<uint8_t>(type), payload, len, sLastClientMac);

}



bool sendToMac(const uint8_t mac[6], MsgType type, const void *payload, uint16_t len) {

  if (!ensureEspNow() || !mac)

    return false;

  addPeer(mac, false);

  return sendPacket(static_cast<uint8_t>(type), payload, len, mac);

}



bool peerMac(uint8_t out[6]) {

  if (!out || !sPeerKnown)

    return false;

  memcpy(out, sPeerMac, 6);

  return true;

}

bool hostHasKnownClient() {
  if (sRole != Role::Host)
    return false;
  for (int i = 0; i < 6; ++i) {
    if (sLastClientMac[i] != 0)
      return true;
  }
  return false;
}



__attribute__((weak)) void onLinkEstablished() {}



} // namespace omote_link

