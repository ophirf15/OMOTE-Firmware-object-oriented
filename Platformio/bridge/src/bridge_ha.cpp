#include "bridge_ha.hpp"

#include "bridge_mdns.hpp"
#include "bridge_power.hpp"

#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <vector>

#ifndef FS_PATH
#define FS_PATH "/littlefs/"
#endif

namespace bridge_ha {
namespace {

struct Settings {
  String url;
  String token;
  bool ok() const { return url.length() && token.length(); }
};

struct ResolvedBase {
  String host;
  uint16_t port = 8123;
  bool useSsl = false;
  IPAddress ip;
  uint32_t resolvedMs = 0;
  bool valid = false;
};

Settings sSettings;
ResolvedBase sResolved;
std::vector<String> sEntities;
uint32_t sLastPollMs = 0;
uint32_t sLastSettingsCheckMs = 0;
size_t sLastSettingsSize = 0;
static constexpr uint32_t kPollMs = 5000;
static constexpr uint32_t kResolveTtlMs = 300000;
static constexpr uint32_t kSettingsCheckMs = 30000;

String trimSlashes(String s) {
  while (s.length() && s.endsWith("/"))
    s.remove(s.length() - 1);
  return s;
}

String normalizeHaUrl(String url) {
  url.trim();
  url = trimSlashes(url);
  if (!url.length())
    return url;
  if (!url.startsWith("http://") && !url.startsWith("https://"))
    url = "http://" + url;
  return url;
}

bool parseHaSettingsJson(const String &json, String &urlOut, String &tokenOut) {
  urlOut = "";
  tokenOut = "";
  const int urlKey = json.indexOf("\"Url\"");
  if (urlKey >= 0) {
    const int q1 = json.indexOf('"', urlKey + 5);
    const int q2 = json.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1)
      urlOut = normalizeHaUrl(json.substring(q1 + 1, q2));
  }
  const int tokKey = json.indexOf("\"Token\"");
  if (tokKey >= 0) {
    const int q1 = json.indexOf('"', tokKey + 7);
    const int q2 = json.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1)
      tokenOut = json.substring(q1 + 1, q2);
  }
  return urlOut.length() && tokenOut.length();
}

void reloadSettings(bool force = false) {
  const uint32_t now = millis();
  if (!force && sSettings.ok() && now - sLastSettingsCheckMs < kSettingsCheckMs)
    return;
  sLastSettingsCheckMs = now;

  if (!LittleFS.exists(FS_PATH "HaSettings.json")) {
    if (sSettings.url.length())
      Serial.println("[bridge_ha] HaSettings.json removed");
    sSettings = {};
    sResolved = {};
    sLastSettingsSize = 0;
    return;
  }

  File f = LittleFS.open(FS_PATH "HaSettings.json", "r");
  if (!f)
    return;
  const size_t sz = f.size();
  if (!force && sz == sLastSettingsSize && sSettings.ok()) {
    f.close();
    return;
  }

  const String json = f.readString();
  f.close();

  String url;
  String token;
  if (!parseHaSettingsJson(json, url, token)) {
    Serial.println("[bridge_ha] HaSettings.json missing Url or Token");
    sSettings = {};
    sResolved = {};
    sLastSettingsSize = sz;
    return;
  }

  const bool changed = url != sSettings.url || token != sSettings.token;
  sSettings.url = url;
  sSettings.token = token;
  sLastSettingsSize = sz;

  if (changed || force) {
    sResolved = {};
    Serial.printf("[bridge_ha] settings loaded url=%s\n", sSettings.url.c_str());
  }
}

bool wifiReady() { return WiFi.status() == WL_CONNECTED; }

bool parseHaBaseUrl(const String &urlIn, String &host, uint16_t &port, bool &useSsl) {
  String url = urlIn;
  useSsl = url.startsWith("https://");
  if (url.startsWith("http://"))
    url.remove(0, 7);
  else if (useSsl)
    url.remove(0, 8);
  url = trimSlashes(url);

  const int colon = url.indexOf(':');
  if (colon > 0) {
    host = url.substring(0, colon);
    port = static_cast<uint16_t>(url.substring(colon + 1).toInt());
    if (port == 0)
      port = useSsl ? 443 : 8123;
  } else {
    host = url;
    port = useSsl ? 443 : 8123;
  }
  return host.length() > 0;
}

bool resolveHaBase(bool force = false) {
  reloadSettings();
  if (!sSettings.ok() || !wifiReady())
    return false;

  const uint32_t now = millis();
  if (!force && sResolved.valid && sResolved.resolvedMs && now - sResolved.resolvedMs < kResolveTtlMs)
    return true;

  String host;
  uint16_t port = 8123;
  bool useSsl = false;
  if (!parseHaBaseUrl(sSettings.url, host, port, useSsl))
    return false;

  IPAddress ip;
  if (!bridge_mdns::resolve(host.c_str(), ip))
    return false;

  sResolved.host = host;
  sResolved.port = port;
  sResolved.useSsl = useSsl;
  sResolved.ip = ip;
  sResolved.resolvedMs = now;
  sResolved.valid = true;
  Serial.printf("[bridge_ha] using HA at %s:%u\n", ip.toString().c_str(), port);
  return true;
}

bool tcpProbe(const IPAddress &ip, uint16_t port) {
  WiFiClient probe;
  probe.setTimeout(15000);
  Serial.printf("[bridge_ha] TCP probe %s:%u from bridge %s gw %s ssid=%s\n", ip.toString().c_str(), port,
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.SSID().c_str());
  yield();
  const bool ok = probe.connect(ip, port, 15000);
  if (ok) {
    Serial.println("[bridge_ha] TCP probe OK");
    probe.stop();
  } else {
    Serial.println("[bridge_ha] TCP probe FAILED");
  }
  return ok;
}

int httpRequest(const String &path, const char *method, const String &body, String *responseOut = nullptr) {
  reloadSettings();
  if (!sSettings.ok() || !wifiReady())
    return -2;

  // Match remote HaWebSocket REST: full URL with hostname (not pre-resolved IP).
  const String fullUrl = trimSlashes(sSettings.url) + path;

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  if (!http.begin(fullUrl)) {
    Serial.printf("[bridge_ha] begin failed %s\n", fullUrl.c_str());
    return -1;
  }

  http.addHeader("Authorization", "Bearer " + sSettings.token);
  http.setReuse(false);

  int code;
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
  } else {
    code = http.GET();
  }

  if (responseOut)
    *responseOut = http.getString();
  else
    http.getString();

  http.end();

  if (code < 0)
    Serial.printf("[bridge_ha] HTTP %s %s -> error %d\n", method, fullUrl.c_str(), code);
  else if (code == 401)
    Serial.printf("[bridge_ha] HTTP %s %s -> 401 (bad token?)\n", method, fullUrl.c_str());
  else if (code < 200 || code >= 300)
    Serial.printf("[bridge_ha] HTTP %s %s -> %d (len=%u)\n", method, fullUrl.c_str(), code,
                  static_cast<unsigned>(fullUrl.length()));

  return code;
}

bool pushEntityToRemote(const String &entityId) {
  std::string state;
  std::string attrs;
  if (!fetchEntityState(entityId.c_str(), state, &attrs)) {
    Serial.printf("[bridge_ha] fetch failed %s\n", entityId.c_str());
    return false;
  }
  Serial.printf("[bridge_ha] %s -> state=%s attrs=%u\n", entityId.c_str(), state.c_str(),
                static_cast<unsigned>(attrs.size()));
  ::bridge_olp_pushHaState(entityId.c_str(), state.c_str());
  if (!attrs.empty())
    ::bridge_olp_pushHaStateAttrs(entityId.c_str(), attrs.c_str());
  return true;
}

} // namespace

void init() {
  reloadSettings(true);
  if (!sSettings.ok())
    return;
  const auto r = diagnose();
  if (r.tcpOk && r.httpCode >= 200)
    Serial.println("[bridge_ha] boot HA check OK");
  else if (r.tcpOk)
    Serial.printf("[bridge_ha] boot: TCP OK, HTTP %d\n", r.httpCode);
  else
    Serial.printf("[bridge_ha] boot: cannot reach HA at %s\n", r.haIp.c_str());
}

void tick() {
  if (!bridge_power::allowHaPolling() || !configured() || sEntities.empty() || !wifiReady())
    return;
  const uint32_t now = millis();
  if (now - sLastPollMs < kPollMs)
    return;
  sLastPollMs = now;
  pollSubscribedNow();
}

void pollEntityNow(const std::string &entityId) {
  if (entityId.empty() || !configured() || !wifiReady())
    return;
  pushEntityToRemote(String(entityId.c_str()));
}

void pollSubscribedNow() {
  if (!configured() || sEntities.empty() || !wifiReady())
    return;
  for (const auto &entityId : sEntities)
    pushEntityToRemote(entityId);
}

bool configured() {
  reloadSettings();
  return sSettings.ok();
}

bool callService(const std::string &domain, const std::string &service, const std::string &entityId,
                 const std::string &serviceDataJson) {
  if (!configured() || entityId.empty() || !wifiReady())
    return false;

  String body = "{\"entity_id\":\"" + String(entityId.c_str()) + "\"";
  if (!serviceDataJson.empty()) {
    String data = String(serviceDataJson.c_str());
    if (data.startsWith("{") && data.endsWith("}")) {
      data.remove(0, 1);
      data.remove(data.length() - 1);
      if (data.length())
        body += "," + data;
    }
  }
  body += "}";

  const String path = "/api/services/" + String(domain.c_str()) + "/" + String(service.c_str());
  int code = httpRequest(path, "POST", body);
  if (code < 0) {
    sResolved.valid = false;
    code = httpRequest(path, "POST", body);
  }
  Serial.printf("[bridge_ha] %s.%s %s -> %d\n", domain.c_str(), service.c_str(), entityId.c_str(), code);

  if (code >= 200 && code < 300)
    pollEntityNow(entityId);
  return code >= 200 && code < 300;
}

void setSubscribedEntities(const std::vector<std::string> &entityIds) {
  if (entityIds.size() == sEntities.size()) {
    bool same = true;
    for (size_t i = 0; i < entityIds.size(); i++) {
      if (String(entityIds[i].c_str()) != sEntities[i]) {
        same = false;
        break;
      }
    }
    if (same)
      return;
  }
  sEntities.clear();
  for (const auto &id : entityIds) {
    if (!id.empty())
      sEntities.push_back(String(id.c_str()));
  }
  for (const auto &id : sEntities)
    Serial.printf("[bridge_ha]   poll %s (len=%u)\n", id.c_str(), static_cast<unsigned>(id.length()));
  Serial.printf("[bridge_ha] subscribe %u entities\n", static_cast<unsigned>(sEntities.size()));
  sLastPollMs = 0;
  if (!sEntities.empty())
    pollSubscribedNow();
}

bool extractJsonObject(const String &json, const char *key, std::string &out) {
  out.clear();
  const String needle = String("\"") + key + "\":";
  const int keyPos = json.indexOf(needle);
  if (keyPos < 0)
    return false;
  const int start = json.indexOf('{', keyPos + needle.length());
  if (start < 0)
    return false;
  int depth = 0;
  for (int i = start; i < static_cast<int>(json.length()); ++i) {
    const char c = json.charAt(i);
    if (c == '{')
      depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        out = json.substring(start, i + 1).c_str();
        return true;
      }
    }
  }
  return false;
}

bool fetchEntityState(const std::string &entityId, std::string &stateOut, std::string *attrsOut) {
  stateOut.clear();
  if (attrsOut)
    attrsOut->clear();
  if (!configured() || entityId.empty() || !wifiReady())
    return false;

  String payload;
  const String path = "/api/states/" + String(entityId.c_str());
  const int code = httpRequest(path, "GET", {}, &payload);
  if (code < 200 || code >= 300)
    return false;

  const int stateKey = payload.indexOf("\"state\"");
  if (stateKey < 0)
    return false;
  const int q1 = payload.indexOf('"', stateKey + 7);
  const int q2 = payload.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 <= q1)
    return false;
  stateOut = payload.substring(q1 + 1, q2).c_str();
  if (attrsOut)
    extractJsonObject(payload, "attributes", *attrsOut);
  return true;
}

HaDiagResult diagnose() {
  HaDiagResult r;
  r.bridgeIp = WiFi.localIP().toString();
  r.gatewayIp = WiFi.gatewayIP().toString();
  reloadSettings(true);

  if (!sSettings.ok()) {
    r.detail = "HaSettings.json missing Url or Token";
    return r;
  }
  if (!wifiReady()) {
    r.detail = "bridge WiFi not connected";
    return r;
  }

  sResolved.valid = false;
  if (!resolveHaBase(true)) {
    r.detail = "mDNS/DNS could not resolve HA host";
    return r;
  }

  r.haIp = sResolved.ip.toString();

  r.httpCode = httpRequest("/api/", "GET", {});
  r.tcpOk = r.httpCode >= 0;
  if (!r.tcpOk) {
    r.tcpOk = tcpProbe(sResolved.ip, sResolved.port);
    if (!r.tcpOk) {
      r.detail = std::string("Cannot reach HA at ") + r.haIp.c_str() + " from bridge " + r.bridgeIp.c_str() +
                 " (ssid=" + WiFi.SSID().c_str() +
                 "). Your remote ESP32 reached HA on this LAN — put the bridge on the same WiFi.";
      return r;
    }
    r.httpCode = httpRequest("/api/", "GET", {});
    r.tcpOk = r.httpCode >= 0;
  }

  if (r.httpCode >= 200 && r.httpCode < 300)
    r.detail = "HA API OK";
  else if (r.httpCode == 401)
    r.detail = "Reachable — token rejected (check HaSettings.json)";
  else if (r.httpCode < 0)
    r.detail = "TCP works but HTTP failed";
  else
    r.detail = "HTTP " + std::to_string(r.httpCode);
  return r;
}

int testApiConnection(std::string &detailOut) {
  const HaDiagResult r = diagnose();
  String msg = "bridge=" + r.bridgeIp + " ha=" + r.haIp + " tcp=" + (r.tcpOk ? "ok" : "FAIL") +
               " http=" + String(r.httpCode) + " — " + String(r.detail.c_str());
  detailOut = msg.c_str();
  Serial.printf("[bridge_ha] %s\n", detailOut.c_str());
  return r.httpCode;
}

} // namespace bridge_ha
