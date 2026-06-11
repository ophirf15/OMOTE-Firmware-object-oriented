#include "bridge_olp_host.hpp"



#include "bridge_ble_host.hpp"
#include "bridge_config_schema.hpp"
#include "bridge_ha.hpp"

#include "omote_link.hpp"

#include <Arduino.h>

#include <LittleFS.h>

#include <algorithm>

#include <cstddef>
#include <cstring>

#include <set>

#include <vector>



#ifndef FS_PATH

#define FS_PATH "/littlefs/"

#endif



namespace bridge_olp_host {

namespace {



std::vector<String> sManifest;

uint8_t sReplyMac[6] = {};

bool sHasClientMac = false;

bool sConfigNotifyPending = false;
uint32_t sConfigNotifyAtMs = 0;
static constexpr uint32_t kConfigNotifyDebounceMs = 2500;

struct FileSendJob {
  uint8_t mac[6] = {};
  String relPath;
};

std::vector<FileSendJob> sFileSendQueue;

struct ActiveFileSend {
  bool active = false;
  uint8_t mac[6] = {};
  String relPath;
  std::vector<uint8_t> data;
  size_t offset = 0;
};

ActiveFileSend sActiveFileSend;

struct ActiveFileReceive {
  bool active = false;
  String relPath;
  File file;
  size_t offset = 0;
  size_t total = 0;
};

ActiveFileReceive sActiveFileReceive;

std::vector<std::string> sHaSubscribeAccum;

uint8_t sHaSubscribeExpected = 0;

void parseHaSubscribeChunk(const omote_link::HaSubscribePayload &sub) {

  const uint8_t len = sub.dataLen > sizeof(sub.entities) ? sizeof(sub.entities) : sub.dataLen;

  std::string packed(sub.entities, sub.entities + len);

  std::vector<std::string> ids;

  size_t start = 0;

  while (start < packed.size()) {

    const size_t end = packed.find('\n', start);

    const std::string line = packed.substr(start, end == std::string::npos ? std::string::npos : end - start);

    if (!line.empty())

      ids.push_back(line);

    if (end == std::string::npos)

      break;

    start = end + 1;

  }

  if (sub.total <= 1) {

    bridge_ha::setSubscribedEntities(ids);

    return;

  }

  if (sub.part == 0) {

    sHaSubscribeAccum.clear();

    sHaSubscribeExpected = sub.total;

  } else if (sHaSubscribeExpected != sub.total) {

    Serial.println("[bridge_olp] HA subscribe chunk sequence reset");

    sHaSubscribeAccum.clear();

    sHaSubscribeExpected = sub.total;

  }

  sHaSubscribeAccum.insert(sHaSubscribeAccum.end(), ids.begin(), ids.end());

  if (sub.part + 1 >= sub.total) {

    bridge_ha::setSubscribedEntities(sHaSubscribeAccum);

    sHaSubscribeAccum.clear();

    sHaSubscribeExpected = 0;

  }

}



void collectFiles(const char *dir, std::vector<String> &out) {

  File root = LittleFS.open(dir);

  if (!root || !root.isDirectory())

    return;

  File file = root.openNextFile();

  while (file) {

    String path = file.path();

    if (file.isDirectory()) {

      collectFiles(path.c_str(), out);

    } else if (path.endsWith(".json")) {

      String rel = path;

      if (rel.startsWith(FS_PATH))

        rel = rel.substring(strlen(FS_PATH));

      else if (rel.startsWith("/littlefs/"))

        rel = rel.substring(10);

      if (rel.length())

        out.push_back(rel);

    }

    file = root.openNextFile();

  }

}

void collectSceneDiskFiles(std::vector<String> &out) {
  out.clear();
  collectFiles(FS_PATH "Scenes", out);
}

String readTextFile(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f)
    return {};
  String body = f.readString();
  f.close();
  return body;
}

String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (unsigned i = 0; i < in.length(); i++) {
    const char c = in.charAt(i);
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out;
}

String sceneNameFromFile(const String &relPath) {
  const String full = String(FS_PATH) + relPath;
  const String body = readTextFile(full.c_str());
  const int key = body.indexOf("\"ScreenName\"");
  if (key >= 0) {
    const int q1 = body.indexOf('"', key + 12);
    const int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1)
      return body.substring(q1 + 1, q2);
  }
  String name = relPath;
  if (name.startsWith("Scenes/Scene_"))
    name = name.substring(13);
  if (name.endsWith(".json"))
    name = name.substring(0, name.length() - 5);
  name.replace('_', ' ');
  return name;
}

void collectRegisteredScenePaths(const String &registryBody, std::vector<String> &out) {
  out.clear();
  int pos = 0;
  while (true) {
    const int key = registryBody.indexOf("\"FileName\"", pos);
    if (key < 0)
      break;
    const int q1 = registryBody.indexOf('"', key + 10);
    const int q2 = registryBody.indexOf('"', q1 + 1);
    if (q1 < 0 || q2 < 0)
      break;
    const String path = registryBody.substring(q1 + 1, q2);
    if (path.startsWith("Scenes/") && path.endsWith(".json"))
      out.push_back(path);
    pos = q2 + 1;
  }
}

bool writeScenesRegistry(const std::vector<String> &diskFiles) {
  String out = "{\n  \"Scenes\": [\n";
  bool first = true;
  for (const String &rel : diskFiles) {
    if (!rel.startsWith("Scenes/") || !rel.endsWith(".json"))
      continue;
    if (!first)
      out += ",\n";
    first = false;
    const String sceneName = sceneNameFromFile(rel);
    out += "    {\n";
    out += "      \"SceneName\": \"" + jsonEscape(sceneName) + "\",\n";
    out += "      \"FileName\": \"" + jsonEscape(rel) + "\"\n";
    out += "    }";
  }
  out += "\n  ]\n}\n";

  File f = LittleFS.open(FS_PATH "Scenes.json", "w");
  if (!f)
    return false;
  const size_t written = f.print(out);
  f.close();
  return written == out.length();
}

void repairScenesRegistry() {
  std::vector<String> diskFiles;
  collectSceneDiskFiles(diskFiles);

  std::vector<String> diskScenes;
  for (const String &rel : diskFiles) {
    if (rel.startsWith("Scenes/") && rel.endsWith(".json"))
      diskScenes.push_back(rel);
  }

  std::vector<String> registered;
  if (LittleFS.exists(FS_PATH "Scenes.json"))
    collectRegisteredScenePaths(readTextFile(FS_PATH "Scenes.json"), registered);

  auto sortPaths = [](std::vector<String> &paths) {
    std::sort(paths.begin(), paths.end(),
              [](const String &a, const String &b) { return a < b; });
  };
  sortPaths(diskScenes);
  sortPaths(registered);

  if (diskScenes.size() == registered.size()) {
    bool same = true;
    for (size_t i = 0; i < diskScenes.size(); i++) {
      if (diskScenes[i] != registered[i]) {
        same = false;
        break;
      }
    }
    if (same)
      return;
  }

  Serial.printf("[bridge_olp] repairing Scenes.json (%u registry, %u on disk)\n",
                static_cast<unsigned>(registered.size()), static_cast<unsigned>(diskScenes.size()));
  if (writeScenesRegistry(diskFiles))
    Serial.println("[bridge_olp] Scenes.json repaired");
  else
    Serial.println("[bridge_olp] Scenes.json repair write failed");
}

void buildManifest() {

  repairScenesRegistry();

  sManifest.clear();

  if (LittleFS.exists(FS_PATH "Scenes.json"))

    sManifest.push_back("Scenes.json");

  if (LittleFS.exists(FS_PATH "HaSettings.json"))

    sManifest.push_back("HaSettings.json");

  if (LittleFS.exists(FS_PATH "DeviceSettings.json"))

    sManifest.push_back("DeviceSettings.json");

  if (LittleFS.exists(FS_PATH "DeviceSettings.schema.json"))

    sManifest.push_back("DeviceSettings.schema.json");

  collectFiles(FS_PATH "Scenes", sManifest);

  collectFiles(FS_PATH "Pages", sManifest);

  collectFiles(FS_PATH "Commands", sManifest);

}



namespace {
static constexpr uint8_t kManifestChunkMagic = 0xFD;
static constexpr size_t kManifestChunkHdr = 3; // magic + part + total
static constexpr size_t kManifestChunkDataMax = omote_link::kMaxPayload - kManifestChunkHdr;

bool sendManifestPacket(const uint8_t mac[6], const void *data, uint16_t len) {
  return omote_link::sendToMac(mac, omote_link::MsgType::ConfigManifest, data, len);
}
} // namespace

void sendManifest(const uint8_t mac[6]) {

  String body;

  for (size_t i = 0; i < sManifest.size(); i++) {

    if (i)

      body += '\n';

    body += sManifest[i];

  }

  const size_t len = body.length();
  if (len <= omote_link::kMaxPayload) {
    const bool ok = sendManifestPacket(mac, body.c_str(), static_cast<uint16_t>(len));
    Serial.printf("[bridge_olp] manifest %u files (%u bytes) sent=%d\n",
                  static_cast<unsigned>(sManifest.size()), static_cast<unsigned>(len), ok ? 1 : 0);
    return;
  }

  // Split on newlines so no path is ever cut mid-string.
  std::vector<String> chunks;
  String current;
  int lineStart = 0;
  while (lineStart < static_cast<int>(body.length())) {
    const int lineEnd = body.indexOf('\n', lineStart);
    const String line = (lineEnd < 0) ? body.substring(lineStart) : body.substring(lineStart, lineEnd);
    const size_t addLen = line.length() + (current.length() ? 1 : 0);
    if (current.length() && addLen > kManifestChunkDataMax) {
      chunks.push_back(current);
      current = line;
    } else {
      if (current.length())
        current += '\n';
      current += line;
    }
    if (lineEnd < 0)
      break;
    lineStart = lineEnd + 1;
  }
  if (current.length())
    chunks.push_back(current);

  // Enforce per-packet data cap (a single long path can exceed the line budget).
  std::vector<String> bounded;
  bounded.reserve(chunks.size() + 1);
  for (const String &c : chunks) {
    for (size_t off = 0; off < c.length(); off += kManifestChunkDataMax)
      bounded.push_back(c.substring(off, off + kManifestChunkDataMax));
  }
  chunks = std::move(bounded);

  const size_t total = chunks.size();
  uint8_t sent = 0;
  for (size_t part = 0; part < total; part++) {
    const uint16_t dataLen = static_cast<uint16_t>(chunks[part].length());
    if (dataLen > kManifestChunkDataMax) {
      Serial.printf("[bridge_olp] manifest chunk %u too large (%u)\n",
                    static_cast<unsigned>(part), static_cast<unsigned>(dataLen));
      continue;
    }
    uint8_t buf[kManifestChunkHdr + kManifestChunkDataMax] = {};
    buf[0] = kManifestChunkMagic;
    buf[1] = static_cast<uint8_t>(part);
    buf[2] = static_cast<uint8_t>(total);
    memcpy(buf + kManifestChunkHdr, chunks[part].c_str(), dataLen);
    const uint16_t payloadLen = static_cast<uint16_t>(kManifestChunkHdr + dataLen);
    if (sendManifestPacket(mac, buf, payloadLen))
      sent++;
    else
      Serial.printf("[bridge_olp] manifest chunk %u/%u send failed (payload %u)\n",
                    static_cast<unsigned>(part + 1), static_cast<unsigned>(total),
                    static_cast<unsigned>(payloadLen));
    delay(2);
  }

  Serial.printf("[bridge_olp] manifest %u files (%u bytes) in %u chunk(s) sent=%u\n",
                static_cast<unsigned>(sManifest.size()), static_cast<unsigned>(len), total, sent);

}



void sendMissingFile(const uint8_t mac[6]) {
  omote_link::ConfigFileChunkHeader miss = {};
  omote_link::sendToMac(mac, omote_link::MsgType::ConfigFileChunk, &miss, sizeof(miss));
}

void queueFileSend(const uint8_t mac[6], const char *path) {
  FileSendJob job;
  memcpy(job.mac, mac, 6);
  job.relPath = path;
  if (job.relPath.startsWith("/"))
    job.relPath.remove(0, 1);
  if (!job.relPath.length())
    return;

  if (sActiveFileSend.active && sActiveFileSend.relPath == job.relPath) {
    sActiveFileSend.offset = 0;
    memcpy(sActiveFileSend.mac, job.mac, 6);
    Serial.printf("[bridge_olp] resend %s from start\n", job.relPath.c_str());
    return;
  }

  for (const auto &queued : sFileSendQueue) {
    if (queued.relPath == job.relPath)
      return;
  }

  sFileSendQueue.push_back(std::move(job));
}

void startNextFileSend() {
  if (sActiveFileSend.active || sFileSendQueue.empty())
    return;

  const FileSendJob job = sFileSendQueue.front();
  sFileSendQueue.erase(sFileSendQueue.begin());

  if (job.relPath == "DeviceSettings.schema.json")
    bridge_config_schema::ensureOnDisk();

  String fsPath = FS_PATH;
  fsPath += job.relPath;

  File f = LittleFS.open(fsPath, "r");
  if (!f) {
    Serial.printf("[bridge_olp] missing %s — skip\n", fsPath.c_str());
    sendMissingFile(job.mac);
    return;
  }

  const size_t fileSz = f.size();
  if (fileSz == 0 || fileSz > 65535) {
    Serial.printf("[bridge_olp] skip %s (size %u)\n", fsPath.c_str(), static_cast<unsigned>(fileSz));
    f.close();
    sendMissingFile(job.mac);
    return;
  }

  std::vector<uint8_t> buf(fileSz);
  size_t got = 0;
  while (got < fileSz) {
    const int n = f.read(buf.data() + got, fileSz - got);
    if (n <= 0)
      break;
    got += static_cast<size_t>(n);
  }
  f.close();
  if (got != fileSz) {
    Serial.printf("[bridge_olp] read failed %s (%u/%u)\n", fsPath.c_str(), static_cast<unsigned>(got),
                  static_cast<unsigned>(fileSz));
    sendMissingFile(job.mac);
    return;
  }

  sActiveFileSend.active = true;
  memcpy(sActiveFileSend.mac, job.mac, 6);
  sActiveFileSend.relPath = job.relPath;
  sActiveFileSend.data = std::move(buf);
  sActiveFileSend.offset = 0;
  Serial.printf("[bridge_olp] loaded %s (%u bytes)\n", job.relPath.c_str(), static_cast<unsigned>(fileSz));
}

bool tickFileSend() {
  if (!sActiveFileSend.active) {
    startNextFileSend();
    return false;
  }

  if (sActiveFileSend.data.empty()) {
    sActiveFileSend.active = false;
    return false;
  }

  const size_t total = sActiveFileSend.data.size();
  omote_link::ConfigFileChunkHeader hdr = {};
  hdr.offset = static_cast<uint16_t>(sActiveFileSend.offset);
  hdr.totalSize = static_cast<uint16_t>(total);
  const size_t maxData = omote_link::kMaxPayload - sizeof(hdr);
  const size_t toSend = std::min(maxData, total - sActiveFileSend.offset);
  hdr.dataLen = static_cast<uint8_t>(toSend);

  uint8_t buf[sizeof(hdr) + omote_link::kMaxPayload] = {};
  memcpy(buf, &hdr, sizeof(hdr));
  memcpy(buf + sizeof(hdr), sActiveFileSend.data.data() + sActiveFileSend.offset, toSend);
  const bool sent = omote_link::sendToMac(sActiveFileSend.mac, omote_link::MsgType::ConfigFileChunk, buf,
                                          static_cast<uint16_t>(sizeof(hdr) + toSend));
  if (!sent)
    return false;

  sActiveFileSend.offset += toSend;
  if (sActiveFileSend.offset >= total) {
    Serial.printf("[bridge_olp] sent %s (%u bytes)\n", sActiveFileSend.relPath.c_str(),
                  static_cast<unsigned>(total));
    sActiveFileSend.data.clear();
    sActiveFileSend.active = false;
    sActiveFileSend.relPath = "";
  }
  return true;
}

void pumpFileSend() {
  for (int i = 0; i < 12; ++i) {
    if (!tickFileSend())
      break;
  }
}

void tickConfigNotify() {
  if (!sConfigNotifyPending || millis() < sConfigNotifyAtMs)
    return;
  sConfigNotifyPending = false;
  sConfigNotifyAtMs = 0;
  bool sent = false;
  if (sHasClientMac)
    sent = omote_link::sendToMac(sReplyMac, omote_link::MsgType::ConfigChanged, nullptr, 0);
  if (!sent)
    sent = omote_link::sendToPeer(omote_link::MsgType::ConfigChanged, nullptr, 0);
  if (sent)
    Serial.println("[bridge_olp] config changed → remote");
  else
    Serial.println("[bridge_olp] config changed — no linked remote (use Pull on remote)");
}



void onMessage(omote_link::MsgType type, const uint8_t *payload, uint16_t len, const uint8_t srcMac[6]) {

  memcpy(sReplyMac, srcMac, 6);

  sHasClientMac = true;

  switch (type) {

  case omote_link::MsgType::ConfigManifestReq:

    Serial.println("[bridge_olp] manifest req from remote");

    buildManifest();

    sendManifest(srcMac);

    break;

  case omote_link::MsgType::ConfigFileReq:

    if (len >= sizeof(omote_link::ConfigFileReqPayload)) {

      omote_link::ConfigFileReqPayload req;

      memcpy(&req, payload, sizeof(req));

      queueFileSend(srcMac, req.path);

    }

    break;

  case omote_link::MsgType::HaCall:

    if (len >= sizeof(omote_link::HaCallPayload)) {

      omote_link::HaCallPayload call;

      memcpy(&call, payload, sizeof(call));

      std::string data;

      if (call.dataLen)

        data.assign(call.data, call.data + call.dataLen);

      bridge_ha::callService(call.domain, call.service, call.entityId, data);

    }

    break;

  case omote_link::MsgType::HaSubscribe:

    if (len >= sizeof(omote_link::HaSubscribePayload)) {

      omote_link::HaSubscribePayload sub;

      memcpy(&sub, payload, sizeof(sub));

      parseHaSubscribeChunk(sub);

    }

    break;

  case omote_link::MsgType::BleSendKey:

    if (len >= sizeof(omote_link::BleSendKeyPayload)) {

      omote_link::BleSendKeyPayload req;

      memcpy(&req, payload, sizeof(req));

      bridge_ble_host::sendKey(req.key);

    }

    break;

  case omote_link::MsgType::BleControl:

    if (len >= sizeof(omote_link::BleControlPayload)) {

      omote_link::BleControlPayload req;

      memcpy(&req, payload, sizeof(req));

      bridge_ble_host::control(req.action, req.profile);

    }

    break;

  case omote_link::MsgType::BleStatusReq:

    bridge_ble_host::sendStatusToRemote(srcMac);

    break;

  case omote_link::MsgType::HaPollReq:

    if (len >= sizeof(omote_link::HaPollReqPayload)) {
      omote_link::HaPollReqPayload req;
      memcpy(&req, payload, sizeof(req));
      if (req.entityId[0])
        bridge_ha::pollEntityNow(req.entityId);
    }

    break;

  case omote_link::MsgType::ConfigFilePushStart:

    if (len >= sizeof(omote_link::ConfigFilePushStartPayload)) {
      omote_link::ConfigFilePushStartPayload req;
      memcpy(&req, payload, sizeof(req));
      if (sActiveFileReceive.file)
        sActiveFileReceive.file.close();
      sActiveFileReceive = {};
      String rel = req.path;
      while (rel.startsWith("/"))
        rel.remove(0, 1);
      if (!rel.length())
        break;
      const String full = String(FS_PATH) + rel;
      const auto slash = full.lastIndexOf('/');
      if (slash > 0) {
        const String dir = full.substring(0, slash);
        if (!LittleFS.exists(dir))
          LittleFS.mkdir(dir);
      }
      sActiveFileReceive.file = LittleFS.open(full, "w");
      if (!sActiveFileReceive.file) {
        Serial.printf("[bridge_olp] push open failed %s\n", full.c_str());
        break;
      }
      sActiveFileReceive.active = true;
      sActiveFileReceive.relPath = rel;
      sActiveFileReceive.total = req.totalSize;
      Serial.printf("[bridge_olp] push start %s (%u bytes)\n", rel.c_str(),
                    static_cast<unsigned>(req.totalSize));
    }

    break;

  case omote_link::MsgType::ConfigFilePushChunk: {

    if (!sActiveFileReceive.active || !sActiveFileReceive.file)
      break;
    if (len < sizeof(omote_link::ConfigFileChunkHeader))
      break;
    omote_link::ConfigFileChunkHeader hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    const uint8_t *data = payload + sizeof(hdr);
    const uint16_t dataLen = hdr.dataLen;
    if (sizeof(hdr) + dataLen > len)
      break;
    if (hdr.offset == 0) {
      sActiveFileReceive.offset = 0;
      sActiveFileReceive.total = hdr.totalSize;
    }
    if (sActiveFileReceive.offset != hdr.offset)
      break;
    sActiveFileReceive.file.write(data, dataLen);
    sActiveFileReceive.offset += dataLen;
    if (sActiveFileReceive.offset >= sActiveFileReceive.total && sActiveFileReceive.total > 0) {
      const String pushedPath = sActiveFileReceive.relPath;
      sActiveFileReceive.file.close();
      Serial.printf("[bridge_olp] push done %s (%u bytes)\n", pushedPath.c_str(),
                    static_cast<unsigned>(sActiveFileReceive.total));
      sActiveFileReceive = {};
      if (pushedPath == "DeviceSettings.schema.json")
        bridge_config_schema::ensureOnDisk();
      notifyConfigChanged();
    }

    break;
  }

  default:

    break;

  }

}



} // namespace



void init() { omote_link::setMessageHandler(onMessage); }



void tick() {
  tickConfigNotify();
  pumpFileSend();
}



void pushHaState(const char *entityId, const char *state) {

  if (!entityId || !state || !sHasClientMac)

    return;

  omote_link::HaStatePayload st = {};

  strncpy(st.entityId, entityId, sizeof(st.entityId) - 1);

  strncpy(st.state, state, sizeof(st.state) - 1);

  omote_link::sendToMac(sReplyMac, omote_link::MsgType::HaState, &st, sizeof(st));

}

void pushHaStateAttrs(const char *entityId, const char *attributesJson) {
  if (!entityId || !attributesJson || !attributesJson[0] || !sHasClientMac)
    return;

  const size_t len = strlen(attributesJson);
  if (!len)
    return;

  constexpr size_t kChunkMax = sizeof(omote_link::HaStateAttrsPayload::data);
  const uint8_t total = static_cast<uint8_t>((len + kChunkMax - 1) / kChunkMax);
  if (total < 1)
    return;

  for (uint8_t part = 0; part < total; ++part) {
    const size_t offset = static_cast<size_t>(part) * kChunkMax;
    const size_t chunkLen = std::min(kChunkMax, len - offset);
    omote_link::HaStateAttrsPayload pkt = {};
    strncpy(pkt.entityId, entityId, sizeof(pkt.entityId) - 1);
    pkt.part = part;
    pkt.total = total;
    pkt.dataLen = static_cast<uint8_t>(chunkLen);
    memcpy(pkt.data, attributesJson + offset, chunkLen);
    omote_link::sendToMac(sReplyMac, omote_link::MsgType::HaStateAttrs, &pkt,
                          static_cast<uint16_t>(offsetof(omote_link::HaStateAttrsPayload, data) + chunkLen));
  }
}



bool isRemoteLinked() {
  if (sHasClientMac)
    return true;
  return omote_link::hostHasKnownClient();
}

bool isConfigTransferActive() {
  if (sActiveFileSend.active)
    return true;
  if (!sFileSendQueue.empty())
    return true;
  if (sActiveFileReceive.active)
    return true;
  return false;
}

void notifyConfigChanged() {
  sConfigNotifyPending = true;
  sConfigNotifyAtMs = millis() + kConfigNotifyDebounceMs;
}



} // namespace bridge_olp_host



void bridge_olp_pushHaState(const char *entityId, const char *state) {

  bridge_olp_host::pushHaState(entityId, state);

}

void bridge_olp_pushHaStateAttrs(const char *entityId, const char *attributesJson) {
  bridge_olp_host::pushHaStateAttrs(entityId, attributesJson);
}

