#include "bridge_client.hpp"



#if defined(OMOTE_BRIDGE_CLIENT) && !defined(IS_SIMULATOR)

#include "ble_scene.hpp"
#include "HaRuntime.hpp"
#include "omote_link.hpp"

void bridge_client_onConfigSynced(bool chainResync, const std::vector<std::string> &manifest);



#include <Arduino.h>

#include <LittleFS.h>
#include <Preferences.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <vector>



#ifndef FS_PATH

#define FS_PATH "/littlefs/"

#endif



namespace bridge_client {

namespace {



enum class SyncPhase : uint8_t {

  Idle,

  AwaitManifest,

  AwaitFile,

  PushFile,

  Done,

};



SyncPhase sPhase = SyncPhase::Idle;

bool sConfigSynced = false;

std::vector<std::string> sManifestFiles;

size_t sNextFileIdx = 0;

std::string sCurrentPath;

std::vector<uint8_t> sCurrentFileBuf;

uint16_t sExpectedSize = 0;

uint32_t sLastSyncAttemptMs = 0;

std::vector<std::string> sPendingSubscribe;

bool sPendingResync = false;
bool sPendingLinkSync = false;
BleRemoteStatus sBleStatus;
bool sSettingsPairingPending = false;
bool sSceneArmed = false;
uint32_t sManifestWaitMs = 0;
uint32_t sFileWaitMs = 0;
String sManifestAccum;
uint8_t sManifestChunkTotal = 0;
uint8_t sManifestChunkGot = 0;
String sHaAttrsAccum;
String sHaAttrsEntity;
uint8_t sHaAttrsChunkTotal = 0;
uint8_t sHaAttrsChunkGot = 0;
static constexpr uint32_t kManifestTimeoutMs = 8000;
static constexpr uint32_t kFileTimeoutMs = 20000;
static constexpr uint8_t kMaxFileRetries = 2;

uint32_t fnv1aHash(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

bool verifyWrittenFile(const char *fsPath, const uint8_t *expected, size_t len) {
  File verify = LittleFS.open(fsPath, "r");
  if (!verify)
    return false;
  if (verify.size() != len) {
    verify.close();
    return false;
  }
  uint8_t chunk[128];
  size_t off = 0;
  while (off < len) {
    const size_t toRead = std::min(sizeof(chunk), len - off);
    const size_t got = verify.read(chunk, toRead);
    if (got != toRead)
      break;
    if (memcmp(chunk, expected + off, got) != 0) {
      verify.close();
      return false;
    }
    off += got;
  }
  verify.close();
  return off == len;
}
static constexpr uint32_t kConfigChangedDebounceMs = 4000;
static constexpr uint32_t kConfigChangedBleDeferMs = 45000;
static constexpr uint8_t kManifestChunkMagic = 0xFD;
static constexpr size_t kManifestChunkHdr = 3;
uint8_t sCurrentFileRetries = 0;
bool sDeferredConfigChanged = false;
uint32_t sDeferredConfigChangedMs = 0;
std::vector<std::string> sPushQueue;
size_t sPushNextIdx = 0;
std::vector<uint8_t> sPushFileBuf;
uint16_t sPushChunkOffset = 0;
bool sPullAfterPush = false;
static constexpr char kPrefsNs[] = "omote_br";
Preferences sPrefs;
bool sPrefsReady = false;

void collectJsonRelPaths(const char *dirPath, std::vector<std::string> &out);
void beginResync(const char *reason);

void ensurePrefs() {
  if (!sPrefsReady) {
    sPrefs.begin(kPrefsNs, false);
    sPrefsReady = true;
  }
}

std::string manifestFingerprint(const std::vector<std::string> &files) {
  std::vector<std::string> sorted = files;
  std::sort(sorted.begin(), sorted.end());
  std::string fp;
  for (const auto &m : sorted)
    fp += m + "\n";
  return fp;
}

void saveSyncFingerprint() {
  ensurePrefs();
  sPrefs.putString("mfm", manifestFingerprint(sManifestFiles).c_str());
}

bool hadSavedSyncFingerprint() {
  ensurePrefs();
  return sPrefs.getString("mfm", "").length() > 0;
}

bool bridgeManifestDiffersFromSaved() {
  ensurePrefs();
  const String saved = sPrefs.getString("mfm", "");
  if (!saved.length())
    return false;
  return saved.c_str() != manifestFingerprint(sManifestFiles);
}

void collectAllLocalConfigPaths(std::vector<std::string> &out) {
  out.clear();
  static const char *rootFiles[] = {"Scenes.json", "HaSettings.json", "DeviceSettings.json",
                                    "DeviceSettings.schema.json"};
  for (const char *name : rootFiles) {
    if (LittleFS.exists(String(FS_PATH) + name))
      out.push_back(name);
  }
  static const char *dirs[] = {FS_PATH "Scenes", FS_PATH "Pages", FS_PATH "Commands"};
  for (const char *dir : dirs)
    collectJsonRelPaths(dir, out);
}

void startPushCurrentFile();
void advancePushQueue();

void beginPushToBridge(const std::vector<std::string> &paths, bool pullAfter) {
  if (paths.empty() || omote_link::state() != omote_link::LinkState::Linked)
    return;
  if (sPhase == SyncPhase::AwaitManifest || sPhase == SyncPhase::AwaitFile ||
      sPhase == SyncPhase::PushFile) {
    Serial.println("[bridge_client] push deferred — sync busy");
    return;
  }
  sPushQueue = paths;
  sPushNextIdx = 0;
  sPullAfterPush = pullAfter;
  sPhase = SyncPhase::PushFile;
  Serial.printf("[bridge_client] push to bridge started (%u files)\n", static_cast<unsigned>(paths.size()));
  advancePushQueue();
}

void advancePushQueue() {
  if (sPhase != SyncPhase::PushFile)
    return;
  if (sPushNextIdx >= sPushQueue.size()) {
    sPushQueue.clear();
    sPushFileBuf.clear();
    sPushChunkOffset = 0;
    sPhase = SyncPhase::Idle;
    Serial.println("[bridge_client] push to bridge complete");
    if (sPullAfterPush) {
      sPullAfterPush = false;
      beginResync("after push to bridge");
    }
    return;
  }
  startPushCurrentFile();
}

void startPushCurrentFile() {
  if (sPushNextIdx >= sPushQueue.size())
    return;
  const std::string &rel = sPushQueue[sPushNextIdx++];
  String full = String(FS_PATH) + rel.c_str();
  File f = LittleFS.open(full, "r");
  if (!f) {
    Serial.printf("[bridge_client] push open failed %s\n", full.c_str());
    advancePushQueue();
    return;
  }
  sPushFileBuf.clear();
  const size_t fileSz = f.size();
  sPushFileBuf.resize(fileSz);
  size_t got = 0;
  while (got < fileSz) {
    const int n = f.read(sPushFileBuf.data() + got, fileSz - got);
    if (n <= 0)
      break;
    got += static_cast<size_t>(n);
  }
  f.close();
  if (got != fileSz) {
    Serial.printf("[bridge_client] push read short %s (%u/%u)\n", rel.c_str(), static_cast<unsigned>(got),
                  static_cast<unsigned>(fileSz));
    advancePushQueue();
    return;
  }
  if (sPushFileBuf.size() > 65535) {
    Serial.printf("[bridge_client] push skip %s (too large)\n", rel.c_str());
    advancePushQueue();
    return;
  }
  omote_link::ConfigFilePushStartPayload start = {};
  strncpy(start.path, rel.c_str(), sizeof(start.path) - 1);
  start.totalSize = static_cast<uint16_t>(sPushFileBuf.size());
  omote_link::sendToPeer(omote_link::MsgType::ConfigFilePushStart, &start, sizeof(start));
  sPushChunkOffset = 0;
  Serial.printf("[bridge_client] pushing %s (%u bytes)\n", rel.c_str(),
                static_cast<unsigned>(sPushFileBuf.size()));
}

void tickPushFile() {
  if (sPhase != SyncPhase::PushFile || sPushFileBuf.empty())
    return;
  for (int i = 0; i < 4 && sPushChunkOffset < sPushFileBuf.size(); ++i) {
    omote_link::ConfigFileChunkHeader hdr = {};
    hdr.offset = sPushChunkOffset;
    hdr.totalSize = static_cast<uint16_t>(sPushFileBuf.size());
    const size_t maxData = omote_link::kMaxPayload - sizeof(hdr);
    const size_t toSend = std::min(maxData, sPushFileBuf.size() - sPushChunkOffset);
    hdr.dataLen = static_cast<uint8_t>(toSend);
    uint8_t buf[sizeof(hdr) + omote_link::kMaxPayload] = {};
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), sPushFileBuf.data() + sPushChunkOffset, toSend);
    const bool sent = omote_link::sendToPeer(omote_link::MsgType::ConfigFilePushChunk, buf,
                                             static_cast<uint16_t>(sizeof(hdr) + toSend));
    if (!sent)
      return;
    sPushChunkOffset += static_cast<uint16_t>(toSend);
  }
  if (sPushChunkOffset >= sPushFileBuf.size()) {
    sPushFileBuf.clear();
    sPushChunkOffset = 0;
    advancePushQueue();
  }
}

void maybePushToBridgeBeforePull() {
  std::vector<std::string> local;
  collectAllLocalConfigPaths(local);
  if (local.empty())
    return;

  std::vector<std::string> toPush;
  for (const auto &rel : local) {
    bool onBridge = false;
    for (const auto &m : sManifestFiles) {
      if (m == rel) {
        onBridge = true;
        break;
      }
    }
    if (!onBridge)
      toPush.push_back(rel);
  }
  if (!toPush.empty() && bridgeManifestDiffersFromSaved())
    Serial.println("[bridge_client] bridge manifest changed — pushing missing files only");

  if (!toPush.empty())
    beginPushToBridge(toPush, true);
}

void requestManifest() {

  sPhase = SyncPhase::AwaitManifest;
  sManifestWaitMs = millis();

  const bool ok = omote_link::sendToPeer(omote_link::MsgType::ConfigManifestReq, nullptr, 0);
  if (ok)
    Serial.println("[bridge_client] config manifest requested");
  else {
    Serial.println("[bridge_client] manifest req send FAILED");
    sPhase = SyncPhase::Idle;
    sManifestWaitMs = 0;
  }

}



void requestFile(const std::string &path) {

  omote_link::ConfigFileReqPayload req = {};

  strncpy(req.path, path.c_str(), sizeof(req.path) - 1);

  sCurrentPath = path;

  sCurrentFileBuf.clear();

  sExpectedSize = 0;

  sPhase = SyncPhase::AwaitFile;
  sFileWaitMs = millis();

  omote_link::sendToPeer(omote_link::MsgType::ConfigFileReq, &req, sizeof(req));

  Serial.printf("[bridge_client] file req %s\n", path.c_str());

}



bool writeCurrentFile() {

  if (sCurrentPath.empty() || sCurrentFileBuf.empty())

    return false;

  std::string fsPath = FS_PATH;

  if (sCurrentPath.front() == '/')

    fsPath += sCurrentPath.substr(1);

  else

    fsPath += sCurrentPath;



  const auto slash = fsPath.find_last_of('/');

  if (slash != std::string::npos) {

    std::string dir = fsPath.substr(0, slash);

    if (!LittleFS.exists(dir.c_str()))

      LittleFS.mkdir(dir.c_str());

  }



  File f = LittleFS.open(fsPath.c_str(), "w");

  if (!f) {

    Serial.printf("[bridge_client] write failed %s\n", fsPath.c_str());

    return false;

  }

  const size_t toWrite = sCurrentFileBuf.size();
  const size_t written = f.write(sCurrentFileBuf.data(), toWrite);
  f.close();
  if (written != toWrite) {
    Serial.printf("[bridge_client] write short %s (%u/%u)\n", fsPath.c_str(), static_cast<unsigned>(written),
                  static_cast<unsigned>(toWrite));
    LittleFS.remove(fsPath.c_str());
    return false;
  }

  if (!verifyWrittenFile(fsPath.c_str(), sCurrentFileBuf.data(), toWrite)) {
    Serial.printf("[bridge_client] verify readback mismatch %s\n", fsPath.c_str());
    LittleFS.remove(fsPath.c_str());
    return false;
  }

  const uint32_t hash = fnv1aHash(sCurrentFileBuf.data(), toWrite);
  sCurrentFileBuf.clear();
  sCurrentFileBuf.shrink_to_fit();

  Serial.printf("[bridge_client] wrote %s (%u bytes hash=%08x)\n", fsPath.c_str(),
                static_cast<unsigned>(toWrite), hash);

  return true;

}



bool relPathFromFs(const String &fullPath, String &relOut) {
  relOut = fullPath;
  if (relOut.startsWith(FS_PATH))
    relOut = relOut.substring(strlen(FS_PATH));
  else if (relOut.startsWith("/littlefs/"))
    relOut = relOut.substring(10);
  while (relOut.startsWith("/"))
    relOut.remove(0, 1);
  return relOut.length() > 0;
}

void collectJsonRelPaths(const char *dirPath, std::vector<std::string> &out) {
  File root = LittleFS.open(dirPath);
  if (!root || !root.isDirectory())
    return;
  File file = root.openNextFile();
  while (file) {
    const String path = file.path();
    if (file.isDirectory()) {
      collectJsonRelPaths(path.c_str(), out);
    } else if (path.endsWith(".json")) {
      String rel;
      if (relPathFromFs(path, rel))
        out.push_back(rel.c_str());
    }
    file = root.openNextFile();
  }
}

void purgeStaleConfigFiles() {
  if (sManifestFiles.empty())
    return;
  std::vector<std::string> local;
  static const char *dirs[] = {FS_PATH "Scenes", FS_PATH "Pages", FS_PATH "Commands"};
  for (const char *dir : dirs)
    collectJsonRelPaths(dir, local);

  for (const auto &rel : local) {
    bool keep = false;
    for (const auto &m : sManifestFiles) {
      if (m == rel) {
        keep = true;
        break;
      }
    }
    if (keep)
      continue;
    const String full = String(FS_PATH) + rel.c_str();
    if (LittleFS.remove(full.c_str()))
      Serial.printf("[bridge_client] purged stale %s\n", rel.c_str());
    else
      Serial.printf("[bridge_client] purge failed %s\n", rel.c_str());
  }
}

void finalizeSyncedFilesystem() {
  purgeStaleConfigFiles();
}

void beginResync(const char *reason) {
  if (sPhase == SyncPhase::AwaitFile || sPhase == SyncPhase::AwaitManifest) {
    sPendingResync = true;
    Serial.printf("[bridge_client] resync deferred (%s)\n", reason);
    return;
  }
  sConfigSynced = false;
  sManifestFiles.clear();
  sNextFileIdx = 0;
  sCurrentPath.clear();
  sCurrentFileBuf.clear();
  sExpectedSize = 0;
  sPhase = SyncPhase::Idle;
  sLastSyncAttemptMs = 0;
  requestManifest();
  Serial.printf("[bridge_client] resync started (%s)\n", reason);
}

void advanceFileQueue() {

  if (sNextFileIdx >= sManifestFiles.size()) {

    if (sManifestFiles.empty()) {
      sPhase = SyncPhase::Idle;
      return;
    }

    sPhase = SyncPhase::Done;

    finalizeSyncedFilesystem();

    sConfigSynced = true;

    Serial.println("[bridge_client] config sync complete");

    saveSyncFingerprint();

    const bool chainResync = sPendingResync;
    if (chainResync) {
      sPendingResync = false;
      sDeferredConfigChanged = false;
    }

    ::bridge_client_onConfigSynced(chainResync, sManifestFiles);

    if (!sPendingSubscribe.empty()) {

      subscribeEntities(sPendingSubscribe);

      sPendingSubscribe.clear();

    }

    return;

  }

  requestFile(sManifestFiles[sNextFileIdx++]);

}



void parseManifestBody(const char *data, size_t len) {

  sManifestFiles.clear();

  sNextFileIdx = 0;

  if (!data || !len) {
    sPhase = SyncPhase::Idle;
    Serial.println("[bridge_client] empty manifest — retry later");
    return;
  }

  std::string manifest(data, len);

  size_t start = 0;

  while (start < manifest.size()) {

    const size_t end = manifest.find('\n', start);

    const std::string line = manifest.substr(start, end == std::string::npos ? std::string::npos : end - start);

    if (!line.empty() && line.size() > 5 && line.find(".json") != std::string::npos)

      sManifestFiles.push_back(line);

    if (end == std::string::npos)

      break;

    start = end + 1;

  }

  Serial.printf("[bridge_client] manifest %u files\n", static_cast<unsigned>(sManifestFiles.size()));

  if (sManifestFiles.empty()) {
    sPhase = SyncPhase::Idle;
    Serial.println("[bridge_client] manifest has no files — retry later");
    return;
  }

  purgeStaleConfigFiles();

  saveSyncFingerprint();

  maybePushToBridgeBeforePull();
  if (sPhase == SyncPhase::PushFile)
    return;

  advanceFileQueue();

}

void parseManifest(const uint8_t *payload, uint16_t len) {

  if (!payload || !len) {
    sPhase = SyncPhase::Idle;
    Serial.println("[bridge_client] empty manifest — retry later");
    return;
  }

  if (len >= kManifestChunkHdr && payload[0] == kManifestChunkMagic) {
    const uint8_t part = payload[1];
    const uint8_t total = payload[2];
    if (total < 1 || part >= total) {
      Serial.println("[bridge_client] bad manifest chunk");
      sPhase = SyncPhase::Idle;
      return;
    }
    if (part == 0) {
      sManifestAccum = "";
      sManifestChunkTotal = total;
      sManifestChunkGot = 0;
    } else if (total != sManifestChunkTotal) {
      Serial.println("[bridge_client] manifest chunk out of order");
      sPhase = SyncPhase::Idle;
      return;
    }
    sManifestAccum.concat(reinterpret_cast<const char *>(payload + kManifestChunkHdr), len - kManifestChunkHdr);
    sManifestChunkGot++;
    if (sManifestChunkGot < sManifestChunkTotal)
      return;
    parseManifestBody(sManifestAccum.c_str(), sManifestAccum.length());
    sManifestAccum = "";
    sManifestChunkTotal = 0;
    sManifestChunkGot = 0;
    return;
  }

  parseManifestBody(reinterpret_cast<const char *>(payload), len);

}

void skipCurrentFile(const char *reason) {

  Serial.printf("[bridge_client] skip file %s (%s)\n", sCurrentPath.c_str(), reason);
  sCurrentPath.clear();
  sCurrentFileBuf.clear();
  sExpectedSize = 0;
  sFileWaitMs = 0;
  sCurrentFileRetries = 0;
  sPhase = SyncPhase::Idle;
  advanceFileQueue();

}

void retryCurrentFile(const char *reason) {
  if (sCurrentPath.empty())
    return;
  Serial.printf("[bridge_client] retry file %s (%s)\n", sCurrentPath.c_str(), reason);
  sCurrentFileBuf.clear();
  sExpectedSize = 0;
  sFileWaitMs = millis();
  omote_link::ConfigFileReqPayload req = {};
  strncpy(req.path, sCurrentPath.c_str(), sizeof(req.path) - 1);
  omote_link::sendToPeer(omote_link::MsgType::ConfigFileReq, &req, sizeof(req));
}

void onOlpMessage(omote_link::MsgType type, const uint8_t *payload, uint16_t len, const uint8_t *srcMac) {

  (void)srcMac;

  switch (type) {

  case omote_link::MsgType::ConfigManifest:

    if (sPhase == SyncPhase::AwaitManifest || sPhase == SyncPhase::Idle) {
      sManifestWaitMs = 0;
      parseManifest(payload, len);
    }

    break;

  case omote_link::MsgType::ConfigFileChunk: {

    if (sPhase != SyncPhase::AwaitFile || len < sizeof(omote_link::ConfigFileChunkHeader))

      break;

    omote_link::ConfigFileChunkHeader hdr;

    memcpy(&hdr, payload, sizeof(hdr));

    if (hdr.offset == 0 && hdr.totalSize == 0 && hdr.dataLen == 0) {
      skipCurrentFile("missing on bridge");
      break;
    }

    const uint8_t *data = payload + sizeof(hdr);

    const uint16_t dataLen = hdr.dataLen;

    if (sizeof(hdr) + dataLen > len)

      break;

    if (hdr.offset == 0) {

      sCurrentFileBuf.clear();

      sExpectedSize = hdr.totalSize;

      sCurrentFileBuf.reserve(hdr.totalSize);

    }

    if (sCurrentFileBuf.size() != hdr.offset)

      break;

    sCurrentFileBuf.insert(sCurrentFileBuf.end(), data, data + dataLen);
    sFileWaitMs = millis();

    if (sCurrentFileBuf.size() >= sExpectedSize && sExpectedSize > 0) {

      if (!writeCurrentFile()) {
        if (sCurrentFileRetries < kMaxFileRetries) {
          sCurrentFileRetries++;
          retryCurrentFile("write verify failed");
        } else {
          skipCurrentFile("write verify failed");
        }
        break;
      }

      sFileWaitMs = 0;
      sCurrentFileRetries = 0;

      sPhase = SyncPhase::Idle;

      advanceFileQueue();

    }

    break;

  }

  case omote_link::MsgType::HaState: {

    if (HaRuntime::overlayActive())
      break;

    if (len < sizeof(omote_link::HaStatePayload))

      break;

    omote_link::HaStatePayload st;

    memcpy(&st, payload, sizeof(st));

    Serial.printf("[bridge_client] HA state %s=%s\n", st.entityId, st.state);
    applyHaState(st.entityId, st.state);

    break;

  }

  case omote_link::MsgType::HaStateAttrs: {

    if (HaRuntime::overlayActive())
      break;

    if (len < offsetof(omote_link::HaStateAttrsPayload, data))
      break;
    omote_link::HaStateAttrsPayload pkt;
    memset(&pkt, 0, sizeof(pkt));
    const uint16_t hdrLen = static_cast<uint16_t>(offsetof(omote_link::HaStateAttrsPayload, data));
    memcpy(&pkt, payload, std::min(len, hdrLen));
    if (pkt.total < 1 || pkt.part >= pkt.total || pkt.dataLen > sizeof(pkt.data))
      break;
    if (len < hdrLen + pkt.dataLen)
      break;
    memcpy(pkt.data, payload + hdrLen, pkt.dataLen);
    if (pkt.part == 0) {
      sHaAttrsAccum = "";
      sHaAttrsEntity = pkt.entityId;
      sHaAttrsChunkTotal = pkt.total;
      sHaAttrsChunkGot = 0;
    } else if (pkt.total != sHaAttrsChunkTotal || sHaAttrsEntity != pkt.entityId) {
      break;
    }
    sHaAttrsAccum.concat(pkt.data, pkt.dataLen);
    sHaAttrsChunkGot++;
    if (sHaAttrsChunkGot >= sHaAttrsChunkTotal) {
      Serial.printf("[bridge_client] HA attrs %s (%u bytes)\n", sHaAttrsEntity.c_str(),
                    static_cast<unsigned>(sHaAttrsAccum.length()));
      HaRuntime::applyBridgeHaAttrs(sHaAttrsEntity.c_str(), sHaAttrsAccum.c_str());
      sHaAttrsAccum = "";
      sHaAttrsEntity = "";
      sHaAttrsChunkTotal = 0;
      sHaAttrsChunkGot = 0;
    }
    break;
  }

  case omote_link::MsgType::ConfigChanged:

    if (!sConfigSynced) {
      Serial.println("[bridge_client] config changed ignored (initial sync pending)");
      break;
    }
    sDeferredConfigChanged = true;
    sDeferredConfigChangedMs = millis() + kConfigChangedDebounceMs;
    Serial.println("[bridge_client] config changed — debounced pull");

    break;

  case omote_link::MsgType::BleStatus:

    if (len >= sizeof(omote_link::BleStatusPayload)) {
      omote_link::BleStatusPayload st;
      memcpy(&st, payload, sizeof(st));
      applyBleStatus(st);
    }
    break;

  default:

    break;

  }

}



} // namespace



bool dropHaRxWhenOverlay(omote_link::MsgType type) {
  return HaRuntime::overlayActive() &&
         (type == omote_link::MsgType::HaState || type == omote_link::MsgType::HaStateAttrs);
}

void init() {

  omote_link::setMessageHandler(onOlpMessage);
  omote_link::setRxDropFilter(dropHaRxWhenOverlay);

}



void tick() {

  if (omote_link::state() != omote_link::LinkState::Linked)

    return;

  const uint32_t now = millis();

  if (sPendingLinkSync && sPhase == SyncPhase::Idle) {
    sPendingLinkSync = false;
    beginResync("link up");
  }

  if (sPhase == SyncPhase::AwaitManifest && sManifestWaitMs &&
      now - sManifestWaitMs > kManifestTimeoutMs) {
    Serial.println("[bridge_client] manifest timeout — retry");
    sManifestWaitMs = 0;
    sManifestAccum = "";
    sManifestChunkTotal = 0;
    sManifestChunkGot = 0;
    sPhase = SyncPhase::Idle;
    sLastSyncAttemptMs = 0;
  }

  if (sPhase == SyncPhase::AwaitFile && sFileWaitMs && now - sFileWaitMs > kFileTimeoutMs) {
    if (sCurrentFileRetries < kMaxFileRetries) {
      sCurrentFileRetries++;
      retryCurrentFile("timeout");
    } else {
      skipCurrentFile("timeout");
    }
  }

  if (sDeferredConfigChanged && now >= sDeferredConfigChangedMs) {
    if (ble_scene::sceneBleArmed()) {
      sDeferredConfigChangedMs = now + kConfigChangedBleDeferMs;
    } else if (sPhase == SyncPhase::AwaitManifest || sPhase == SyncPhase::AwaitFile) {
      sDeferredConfigChangedMs = now + 3000;
    } else {
      sDeferredConfigChanged = false;
      beginResync("bridge config changed");
    }
  }

  if (sPhase == SyncPhase::PushFile)
    tickPushFile();

  if (!sConfigSynced && sPhase == SyncPhase::Idle && now - sLastSyncAttemptMs > 5000) {

    sLastSyncAttemptMs = now;

    requestManifest();

  }

}



bool linked() { return omote_link::state() == omote_link::LinkState::Linked; }



bool configSynced() { return sConfigSynced; }

bool isSyncedConfigFile(const std::string &relPath) {
  for (const auto &m : sManifestFiles) {
    if (m == relPath)
      return true;
  }
  return false;
}

std::vector<std::string> syncedSceneFiles() {
  std::vector<std::string> out;
  for (const auto &m : sManifestFiles) {
    if (m.size() > 7 && m.compare(0, 7, "Scenes/") == 0 && m.size() > 5 &&
        m.compare(m.size() - 5, 5, ".json") == 0)
      out.push_back(m);
  }
  return out;
}

bool callHa(const std::string &domain, const std::string &service, const std::string &entityId,

            const std::string &serviceDataJson) {

  if (!linked())

    return false;



  omote_link::HaCallPayload call = {};

  strncpy(call.domain, domain.c_str(), sizeof(call.domain) - 1);

  strncpy(call.service, service.c_str(), sizeof(call.service) - 1);

  if (entityId.size() >= sizeof(call.entityId)) {

    Serial.printf("[bridge_client] HA entity id too long for OLP (%u)\n",

                  static_cast<unsigned>(entityId.size()));

    return false;

  }

  strncpy(call.entityId, entityId.c_str(), sizeof(call.entityId) - 1);

  if (!serviceDataJson.empty()) {

    const size_t n = std::min(serviceDataJson.size(), sizeof(call.data));

    memcpy(call.data, serviceDataJson.c_str(), n);

    call.dataLen = static_cast<uint8_t>(n);

  }

  Serial.printf("[bridge_client] HA call %s.%s %s\n", call.domain, call.service, call.entityId);

  return omote_link::sendToPeer(omote_link::MsgType::HaCall, &call, sizeof(call));

}



void subscribeEntities(const std::vector<std::string> &entityIds) {

  if (!linked()) {

    sPendingSubscribe = entityIds;

    return;

  }

  static std::vector<std::string> sLastSubscribed;
  if (entityIds == sLastSubscribed)
    return;
  sLastSubscribed = entityIds;

  constexpr size_t kChunkMax = sizeof(omote_link::HaSubscribePayload::entities);

  std::vector<std::string> chunks;

  std::string current;

  for (const auto &id : entityIds) {

    if (id.empty())

      continue;

    if (id.size() > omote_link::kMaxHaEntityIdLen) {

      Serial.printf("[bridge_client] HA entity id too long (%u), skipped\n",

                    static_cast<unsigned>(id.size()));

      continue;

    }

    const size_t extra = current.empty() ? id.size() : id.size() + 1;

    if (!current.empty() && current.size() + extra > kChunkMax) {

      chunks.push_back(current);

      current.clear();

    }

    if (!current.empty())

      current.push_back('\n');

    current += id;

  }

  if (!current.empty())

    chunks.push_back(current);

  if (chunks.empty()) {

    omote_link::HaSubscribePayload clear = {};

    clear.total = 1;

    omote_link::sendToPeer(omote_link::MsgType::HaSubscribe, &clear, sizeof(clear));

    return;

  }

  const uint8_t total = static_cast<uint8_t>(std::min(chunks.size(), size_t{255}));

  for (uint8_t part = 0; part < total; ++part) {

    const std::string &packed = chunks[part];

    omote_link::HaSubscribePayload sub = {};

    sub.part = part;

    sub.total = total;

    sub.dataLen = static_cast<uint8_t>(std::min(packed.size(), kChunkMax));

    memcpy(sub.entities, packed.c_str(), sub.dataLen);

    omote_link::sendToPeer(omote_link::MsgType::HaSubscribe, &sub, sizeof(sub));

    if (part + 1 < total)

      delay(2);

  }

  Serial.printf("[bridge_client] HA subscribe %u entities in %u chunk(s)\n",

                static_cast<unsigned>(entityIds.size()), static_cast<unsigned>(total));

}



void applyHaState(const std::string &entityId, const std::string &state) {

  HaRuntime::applyBridgeHaState(entityId.c_str(), state.c_str());

}



void onLinked() {

  sConfigSynced = false;
  sPendingLinkSync = true;

  Serial.println("[bridge_client] link up — config pull scheduled");

}

bool syncInProgress() {
  return sPhase == SyncPhase::AwaitManifest || sPhase == SyncPhase::AwaitFile ||
         sPhase == SyncPhase::PushFile;
}

void requestConfigPull() {
  if (omote_link::state() != omote_link::LinkState::Linked) {
    Serial.println("[bridge_client] config pull skipped — bridge not linked");
    return;
  }
  beginResync("manual pull");
}

void requestQueuedResync() { beginResync("queued update"); }

void requestPushToBridge() {
  if (omote_link::state() != omote_link::LinkState::Linked) {
    Serial.println("[bridge_client] config push skipped — bridge not linked");
    return;
  }
  std::vector<std::string> local;
  collectAllLocalConfigPaths(local);
  if (local.empty()) {
    Serial.println("[bridge_client] config push skipped — no local config");
    return;
  }
  beginPushToBridge(local, false);
}

void requestHaEntityPoll(const std::string &entityId) {
  if (!linked() || entityId.empty())
    return;
  if (entityId.size() >= sizeof(omote_link::HaPollReqPayload::entityId))
    return;
  omote_link::HaPollReqPayload req = {};
  strncpy(req.entityId, entityId.c_str(), sizeof(req.entityId) - 1);
  omote_link::sendToPeer(omote_link::MsgType::HaPollReq, &req, sizeof(req));
}

void applyBleStatus(const omote_link::BleStatusPayload &st) {
  sBleStatus.valid = true;
  sBleStatus.initialized = st.initialized != 0;
  sBleStatus.connected = st.connected != 0;
  sBleStatus.advertising = st.advertising != 0;
  sBleStatus.pairing = st.pairing != 0;
  sBleStatus.sceneArmed = st.sceneArmed != 0;
  sBleStatus.profile.assign(st.profile, st.profileLen);
  ble_scene::notifyRemoteBleStatus(sBleStatus.pairing, sBleStatus.connected, sBleStatus.initialized);
}

bool sendBleKey(const std::string &keyName) {
  if (!linked() || keyName.empty())
    return false;
  if (keyName.size() >= sizeof(omote_link::BleSendKeyPayload::key))
    return false;
  omote_link::BleSendKeyPayload req = {};
  strncpy(req.key, keyName.c_str(), sizeof(req.key) - 1);
  return omote_link::sendToPeer(omote_link::MsgType::BleSendKey, &req, sizeof(req));
}

bool sendBleControl(uint8_t action, const std::string &profile) {
  if (omote_link::state() == omote_link::LinkState::Uninitialized)
    return false;
  omote_link::BleControlPayload req = {};
  req.action = action;
  if (!profile.empty())
    strncpy(req.profile, profile.c_str(), sizeof(req.profile) - 1);
  return omote_link::sendToPeer(omote_link::MsgType::BleControl, &req, sizeof(req));
}

void requestBleStatus() {
  static uint32_t sLastReqMs = 0;
  const uint32_t now = millis();
  if (now - sLastReqMs < 750)
    return;
  sLastReqMs = now;
  if (omote_link::state() == omote_link::LinkState::Uninitialized)
    return;
  omote_link::sendToPeer(omote_link::MsgType::BleStatusReq, nullptr, 0);
}

void onBleSceneDisarmed() {
  if (sDeferredConfigChanged)
    sDeferredConfigChangedMs = millis() + 1500;
}

const BleRemoteStatus &bleStatus() { return sBleStatus; }

} // namespace bridge_client



namespace omote_link {

void onLinkEstablished() { bridge_client::onLinked(); }

} // namespace omote_link



#else



namespace bridge_client {



void init() {}

void tick() {}

bool linked() { return false; }

bool configSynced() { return false; }

bool syncInProgress() { return false; }

void requestConfigPull() {}

void requestQueuedResync() {}

void requestPushToBridge() {}

void requestHaEntityPoll(const std::string &) {}

void applyBleStatus(const omote_link::BleStatusPayload &) {}

bool sendBleKey(const std::string &) { return false; }

bool sendBleControl(uint8_t, const std::string &) { return false; }

void requestBleStatus() {}

void onBleSceneDisarmed() {}

const BleRemoteStatus &bleStatus() {
  static BleRemoteStatus empty;
  return empty;
}

bool isSyncedConfigFile(const std::string &) { return false; }

std::vector<std::string> syncedSceneFiles() { return {}; }

bool callHa(const std::string &, const std::string &, const std::string &, const std::string &) { return false; }

void subscribeEntities(const std::vector<std::string> &) {}

void applyHaState(const std::string &, const std::string &) {}



} // namespace bridge_client



#endif

