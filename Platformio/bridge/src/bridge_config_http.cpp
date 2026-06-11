#include "bridge_config_http.hpp"
#include "bridge_config_schema.hpp"

#include "bridge_ble_host.hpp"
#include "bridge_ha.hpp"
#include "bridge_mdns.hpp"
#include "bridge_olp_host.hpp"
#include "default_device_settings_schema.hpp"
#include "omote_link.hpp"

#include <Arduino.h>
#include <string>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <algorithm>
#include <vector>

#ifndef FS_PATH
#define FS_PATH "/littlefs/"
#endif

namespace bridge_config_http {
namespace {

WebServer server(80);
bool sRunning = false;
bool sEditorSync = false;

static constexpr size_t kFsChunkMax = 8192;

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(int code, const String &body) {
  sendCors();
  server.send(code, "application/json", body);
}

String normalizeRelPath(String path) {
  path.trim();
  path.replace('\\', '/');
  while (path.startsWith("/"))
    path.remove(0, 1);
  if (path.startsWith("littlefs/"))
    path.remove(0, 9);
  return path;
}

bool isSafePath(const String &pathIn) {
  const String path = normalizeRelPath(pathIn);
  return path.length() && path.indexOf("..") < 0;
}

String lfsPath(const String &relIn) {
  const String rel = normalizeRelPath(relIn);
  return String(FS_PATH) + rel;
}

bool ensureParentDirs(const String &fullPath) {
  int start = fullPath.startsWith("/") ? 1 : 0;
  int slash = fullPath.indexOf('/', start);
  while (slash > 0) {
    const String dir = fullPath.substring(0, slash);
    if (dir.length() && !LittleFS.exists(dir))
      LittleFS.mkdir(dir);
    slash = fullPath.indexOf('/', slash + 1);
  }
  return true;
}

String relPathFromFs(const String &fullPath) {
  String rel = fullPath;
  if (rel.startsWith(FS_PATH))
    rel = rel.substring(strlen(FS_PATH));
  else if (rel.startsWith("/littlefs/"))
    rel = rel.substring(10);
  if (rel.startsWith("/"))
    rel.remove(0, 1);
  return rel;
}

void collectFiles(const char *dirPath, std::vector<String> &out) {
  File root = LittleFS.open(dirPath);
  if (!root || !root.isDirectory())
    return;
  File file = root.openNextFile();
  while (file) {
    const String fullPath = file.path();
    if (file.isDirectory()) {
      collectFiles(file.path(), out);
    } else {
      const String rel = relPathFromFs(fullPath);
      if (rel.length())
        out.push_back(rel);
    }
    file = root.openNextFile();
  }
}

void handleOptions() {
  sendCors();
  server.send(204);
}

void handleRoot() { sendJson(200, "{\"name\":\"OMOTE Bridge\",\"api\":\"/api/status\"}"); }

void handleStatus() {
  String body = "{\"connected\":";
  body += WiFi.isConnected() ? "true" : "false";
  body += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  body += ",\"hostname\":\"omote\",\"api\":\"omote-config-v1\",\"role\":\"bridge\"";
  body += ",\"editor_sync\":";
  body += sEditorSync ? "true" : "false";
  body += ",\"remote_linked\":";
  body += bridge_olp_host::isRemoteLinked() ? "true" : "false";
  body += "}";
  sendJson(200, body);
}

void handleEditorSyncGet() { sendJson(200, sEditorSync ? "{\"editor_sync\":true}" : "{\"editor_sync\":false}"); }

void handleEditorSyncPost() {
  bool on = false;
  if (server.hasArg("plain")) {
    const String body = server.arg("plain");
    on = body.indexOf("\"on\":true") >= 0 || body.indexOf("\"on\": true") >= 0;
  }
  sEditorSync = on;
  if (on)
    sendJson(200, "{\"ok\":true,\"editor_sync\":true}");
  else
    sendJson(200, "{\"ok\":true,\"editor_sync\":false}");
}

void handleFsTree() {
  std::vector<String> files;
  if (LittleFS.exists(FS_PATH))
    collectFiles(FS_PATH, files);
  std::sort(files.begin(), files.end());
  String body = "{\"files\":[";
  bool first = true;
  for (const auto &f : files) {
    if (!f.endsWith(".json"))
      continue;
    if (!first)
      body += ",";
    first = false;
    body += "\"" + f + "\"";
  }
  body += "]}";
  sendJson(200, body);
}

void handleFsStat() {
  if (!server.hasArg("path")) {
    sendJson(400, "{\"error\":\"missing path\"}");
    return;
  }
  const String path = normalizeRelPath(server.arg("path"));
  if (!isSafePath(path)) {
    Serial.printf("[bridge_http] stat bad path: %s\n", server.arg("path").c_str());
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  const String full = lfsPath(path);
  if (!LittleFS.exists(full)) {
    sendJson(404, "{\"error\":\"not found\"}");
    return;
  }
  File f = LittleFS.open(full, "r");
  const size_t sz = f.size();
  f.close();
  sendJson(200, "{\"path\":\"" + path + "\",\"size\":" + String(sz) + "}");
}

void handleFsReadRaw() {
  if (!server.hasArg("path")) {
    sendJson(400, "{\"error\":\"missing path\"}");
    return;
  }
  const String path = normalizeRelPath(server.arg("path"));
  if (!isSafePath(path)) {
    Serial.printf("[bridge_http] read bad path: %s\n", server.arg("path").c_str());
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  const String full = lfsPath(path);
  if (!LittleFS.exists(full)) {
    sendJson(404, "{\"error\":\"not found\"}");
    return;
  }
  File f = LittleFS.open(full, "r");
  sendCors();
  server.sendHeader("X-OMOTE-Path", path);
  server.streamFile(f, "application/json; charset=utf-8");
  f.close();
}

void handleFsReadChunk() {
  if (!server.hasArg("path")) {
    sendJson(400, "{\"error\":\"missing path\"}");
    return;
  }
  const String path = normalizeRelPath(server.arg("path"));
  if (!isSafePath(path)) {
    Serial.printf("[bridge_http] chunk bad path: %s\n", server.arg("path").c_str());
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  const String full = lfsPath(path);
  if (!LittleFS.exists(full)) {
    sendJson(404, "{\"error\":\"not found\"}");
    return;
  }
  File f = LittleFS.open(full, "r");
  const size_t total = f.size();
  size_t offset = 0;
  if (server.hasArg("offset"))
    offset = static_cast<size_t>(strtoul(server.arg("offset").c_str(), nullptr, 10));
  size_t maxLen = kFsChunkMax;
  if (server.hasArg("max")) {
    maxLen = static_cast<size_t>(strtoul(server.arg("max").c_str(), nullptr, 10));
    if (maxLen == 0 || maxLen > kFsChunkMax)
      maxLen = kFsChunkMax;
  }
  if (offset > total) {
    f.close();
    sendJson(416, "{\"error\":\"offset past end\"}");
    return;
  }
  const size_t toRead = std::min(maxLen, total - offset);
  if (!f.seek(offset)) {
    f.close();
    sendJson(500, "{\"error\":\"seek failed\"}");
    return;
  }
  uint8_t buf[kFsChunkMax];
  const size_t n = f.read(buf, toRead);
  f.close();

  sendCors();
  server.sendHeader("Connection", "close");
  server.sendHeader("X-OMOTE-Path", path);
  server.sendHeader("X-OMOTE-Offset", String(static_cast<unsigned>(offset)));
  server.sendHeader("X-OMOTE-Total", String(static_cast<unsigned>(total)));
  server.sendHeader("X-OMOTE-More", (offset + n < total) ? "1" : "0");
  server.sendHeader("X-OMOTE-Length", String(static_cast<unsigned>(n)));
  server.setContentLength(n);
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char *>(buf), n);
}

void handleFsWrite() {
  if (!server.hasArg("path")) {
    sendJson(400, "{\"error\":\"missing path\"}");
    return;
  }
  const String path = normalizeRelPath(server.arg("path"));
  if (!isSafePath(path)) {
    Serial.printf("[bridge_http] write bad path: %s\n", server.arg("path").c_str());
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  const String full = lfsPath(path);
  ensureParentDirs(full);
  String body;
  if (server.hasArg("plain"))
    body = server.arg("plain");
  else if (server.hasArg("content"))
    body = server.arg("content");
  else if (server.hasArg("body"))
    body = server.arg("body");
  File f = LittleFS.open(full, "w");
  if (!f) {
    sendJson(500, "{\"error\":\"open failed\"}");
    return;
  }
  if (body.length())
    f.print(body);
  f.close();
  Serial.printf("[bridge_http] wrote %s (%u bytes)\n", path.c_str(), static_cast<unsigned>(body.length()));
  if (path == "DeviceSettings.schema.json")
    bridge_config_schema::ensureOnDisk();
  if (path == "HaSettings.json")
    bridge_ha::init();
  bridge_olp_host::notifyConfigChanged();
  sendJson(200, "{\"ok\":true}");
}

void handleFsDelete() {
  if (!server.hasArg("path")) {
    sendJson(400, "{\"error\":\"missing path\"}");
    return;
  }
  const String path = normalizeRelPath(server.arg("path"));
  if (!isSafePath(path)) {
    Serial.printf("[bridge_http] delete bad path: %s\n", server.arg("path").c_str());
    sendJson(400, "{\"error\":\"bad path\"}");
    return;
  }
  const String full = lfsPath(path);
  if (!LittleFS.exists(full)) {
    sendJson(404, "{\"error\":\"not found\"}");
    return;
  }
  if (!LittleFS.remove(full)) {
    sendJson(500, "{\"error\":\"delete failed\"}");
    return;
  }
  Serial.printf("[bridge_http] deleted %s\n", path.c_str());
  bridge_olp_host::notifyConfigChanged();
  sendJson(200, "{\"ok\":true}");
}

void handleReboot() {
  sendJson(200, "{\"ok\":true,\"restart\":true}");
  delay(300);
  ESP.restart();
}

void handleDeviceSettingsGet() {
  const String full = lfsPath("DeviceSettings.json");
  File f = LittleFS.open(full, "r");
  if (!f) {
    sendJson(404, "{\"error\":\"not found\"}");
    return;
  }
  sendCors();
  server.streamFile(f, "application/json");
}

bool deviceSettingsSchemaLooksComplete(const String &body) {
  return body.indexOf("\"mqtt\"") >= 0 && body.indexOf("\"sections\"") >= 0;
}

void handleDeviceSettingsSchemaGet() {
  bridge_config_schema::ensureOnDisk();
  const String full = lfsPath("DeviceSettings.schema.json");
  File f = LittleFS.open(full, "r");
  if (f) {
    const String body = f.readString();
    f.close();
    if (body.length()) {
      sendCors();
      server.send(200, "application/json", body);
      return;
    }
    Serial.println("[bridge_http] DeviceSettings.schema.json empty — serving firmware default");
  }
  sendCors();
  server.send(200, "application/json", kDefaultDeviceSettingsSchema);
}

void handleDeviceSettingsPost() {
  if (!server.hasArg("plain") && !server.hasArg("body")) {
    sendJson(400, "{\"error\":\"missing body\"}");
    return;
  }
  const String body = server.hasArg("plain") ? server.arg("plain") : server.arg("body");
  const String full = lfsPath("DeviceSettings.json");
  ensureParentDirs(full);
  File f = LittleFS.open(full, "w");
  if (!f) {
    sendJson(500, "{\"error\":\"open failed\"}");
    return;
  }
  f.print(body);
  f.close();
  bridge_olp_host::notifyConfigChanged();
  sendJson(200, "{\"ok\":true}");
}

void handleHaTest() {
  const auto r = bridge_ha::diagnose();
  String body = "{\"ok\":";
  body += (r.tcpOk && r.httpCode >= 200 && r.httpCode < 300) ? "true" : "false";
  body += ",\"tcp_ok\":";
  body += r.tcpOk ? "true" : "false";
  body += ",\"http_code\":";
  body += String(r.httpCode);
  body += ",\"bridge_ip\":\"";
  body += r.bridgeIp;
  body += "\",\"gateway_ip\":\"";
  body += r.gatewayIp;
  body += "\",\"ha_ip\":\"";
  body += r.haIp;
  body += "\",\"detail\":\"";
  body += r.detail.c_str();
  body += "\"}";
  sendJson(r.tcpOk ? 200 : 502, body);
}

void handleBleStatusGet() {
  if (!bridge_ble_host::available()) {
    sendJson(503, "{\"error\":\"ble unavailable\"}");
    return;
  }
  sendCors();
  server.send(200, "application/json", bridge_ble_host::statusJson().c_str());
}

void handleBleIdentitiesGet() {
  sendCors();
  server.send(200, "application/json", bridge_ble_host::identityListJson().c_str());
}

void handleBlePairingPost() {
  const bool on = server.hasArg("plain") ? server.arg("plain").indexOf("\"on\":true") >= 0 : true;
  bridge_ble_host::control(on ? static_cast<uint8_t>(omote_link::BleControlAction::StartPairing)
                              : static_cast<uint8_t>(omote_link::BleControlAction::StopPairing));
  sendJson(200, "{\"ok\":true}");
}

void handleBleForgetPost() {
  bridge_ble_host::control(static_cast<uint8_t>(omote_link::BleControlAction::ForgetBonds));
  sendJson(200, "{\"ok\":true}");
}

void handleBleDisconnectPost() {
  bridge_ble_host::control(static_cast<uint8_t>(omote_link::BleControlAction::Disconnect));
  sendJson(200, "{\"ok\":true}");
}

void handleBleTestPost() {
  if (!server.hasArg("plain") && !server.hasArg("body")) {
    sendJson(400, "{\"error\":\"missing body\"}");
    return;
  }
  const String body = server.hasArg("plain") ? server.arg("plain") : server.arg("body");
  const int keyPos = body.indexOf("\"key\"");
  if (keyPos < 0) {
    sendJson(400, "{\"error\":\"key required\"}");
    return;
  }
  const int q1 = body.indexOf('"', keyPos + 5);
  const int q2 = body.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 <= q1) {
    sendJson(400, "{\"error\":\"key required\"}");
    return;
  }
  const String key = body.substring(q1 + 1, q2);
  bridge_ble_host::setSceneArmed(true);
  bridge_ble_host::sendKey(key.c_str());
  sendJson(200, "{\"ok\":true}");
}

void registerRoutes() {
  static bool once = false;
  if (once)
    return;
  once = true;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/device/sync-mode", HTTP_GET, handleEditorSyncGet);
  server.on("/api/device/sync-mode", HTTP_POST, handleEditorSyncPost);
  server.on("/api/device/reboot", HTTP_POST, handleReboot);
  server.on("/api/device/settings", HTTP_GET, handleDeviceSettingsGet);
  server.on("/api/device/settings", HTTP_POST, handleDeviceSettingsPost);
  server.on("/api/device/settings/schema", HTTP_GET, handleDeviceSettingsSchemaGet);
  server.on("/api/ha/test", HTTP_GET, handleHaTest);
  server.on("/api/ble/status", HTTP_GET, handleBleStatusGet);
  server.on("/api/ble/identities", HTTP_GET, handleBleIdentitiesGet);
  server.on("/api/ble/pairing", HTTP_POST, handleBlePairingPost);
  server.on("/api/ble/forget", HTTP_POST, handleBleForgetPost);
  server.on("/api/ble/disconnect", HTTP_POST, handleBleDisconnectPost);
  server.on("/api/ble/test", HTTP_POST, handleBleTestPost);
  server.on("/api/fs/tree", HTTP_GET, handleFsTree);
  server.on("/api/fs/stat", HTTP_GET, handleFsStat);
  server.on("/api/fs/read/raw", HTTP_GET, handleFsReadRaw);
  server.on("/api/fs/read/chunk", HTTP_GET, handleFsReadChunk);
  server.on("/api/fs/write", HTTP_POST, handleFsWrite);
  server.on("/api/fs/write", HTTP_PUT, handleFsWrite);
  server.on("/api/fs/delete", HTTP_POST, handleFsDelete);

  const char *optsRoutes[] = {
      "/api/status",        "/api/device/sync-mode", "/api/device/reboot", "/api/device/settings",
      "/api/device/settings/schema", "/api/ha/test",
      "/api/ble/status", "/api/ble/identities", "/api/ble/pairing", "/api/ble/forget",
      "/api/ble/disconnect", "/api/ble/test",
      "/api/fs/tree",       "/api/fs/stat",
      "/api/fs/read/raw",
      "/api/fs/read/chunk", "/api/fs/write",         "/api/fs/delete",
  };
  for (const char *route : optsRoutes)
    server.on(route, HTTP_OPTIONS, handleOptions);

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
      return;
    }
    sendJson(404, "{\"error\":\"not found\"}");
  });
}

} // namespace

void begin() { registerRoutes(); }

void sync() {
  if (!WiFi.isConnected()) {
    if (sRunning) {
      server.stop();
      sRunning = false;
    }
    return;
  }

  bridge_mdns::begin();

  if (!sRunning) {
    server.begin();
    sRunning = true;
  }
  for (int i = 0; i < 4; i++)
    server.handleClient();
}

} // namespace bridge_config_http
