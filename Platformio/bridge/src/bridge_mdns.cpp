#include "bridge_mdns.hpp"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>
#include <mdns.h>

namespace bridge_mdns {
namespace {

bool sReady = false;
IPAddress sCachedIp;
char sCachedHost[64] = {};
uint32_t sCachedMs = 0;
static constexpr uint32_t kCacheMs = 120000;

bool queryMdnsName(const char *name, uint32_t timeoutMs, IPAddress &out) {
  if (!name || !name[0])
    return false;

  IPAddress ip = MDNS.queryHost(name, timeoutMs);
  if (ip) {
    out = ip;
    return true;
  }

  esp_ip4_addr_t addr = {};
  if (mdns_query_a(name, timeoutMs, &addr) == ESP_OK && addr.addr) {
    out = IPAddress(addr.addr);
    return true;
  }
  return false;
}

} // namespace

void begin() {
  if (sReady || WiFi.status() != WL_CONNECTED)
    return;

  WiFi.setSleep(WIFI_PS_NONE);

  sReady = MDNS.begin("omote");
  if (!sReady) {
    Serial.println("[bridge_mdns] MDNS.begin failed");
    return;
  }

  MDNS.addService("http", "tcp", 80);
  delay(300);
  Serial.println("[bridge_mdns] ready (omote.local + mDNS client)");
}

bool ready() { return sReady; }

bool resolve(const char *hostname, IPAddress &out) {
  out = IPAddress();
  if (!hostname || !hostname[0])
    return false;

  if (out.fromString(hostname))
    return true;

  const uint32_t now = millis();
  if (sCachedHost[0] && strcmp(sCachedHost, hostname) == 0 && sCachedIp && sCachedMs &&
      now - sCachedMs < kCacheMs) {
    out = sCachedIp;
    return true;
  }

  begin();
  if (!sReady)
    return false;

  String host = hostname;
  host.trim();
  if (host.endsWith(".local"))
    host.remove(host.length() - 6);

  if (queryMdnsName(host.c_str(), 5000, out))
    goto cached;

  static const char *kAliases[] = {"homeassistant", "hassio", "home-assistant"};
  for (const char *alias : kAliases) {
    if (host == alias)
      continue;
    if (queryMdnsName(alias, 3000, out))
      goto cached;
  }

  if (WiFi.hostByName(hostname, out) == 1 && out)
    goto cached;

  Serial.printf("[bridge_mdns] resolve failed: %s\n", hostname);
  return false;

cached:
  strncpy(sCachedHost, hostname, sizeof(sCachedHost) - 1);
  sCachedIp = out;
  sCachedMs = now;
  Serial.printf("[bridge_mdns] %s -> %s\n", hostname, out.toString().c_str());
  return true;
}

} // namespace bridge_mdns
