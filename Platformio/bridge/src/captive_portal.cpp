#include "captive_portal.hpp"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <memory>

namespace bridge_portal {
namespace {

constexpr char kApName[] = "OMOTE-Bridge-Setup";

std::unique_ptr<DNSServer> dns;
std::unique_ptr<WebServer> server;
bool active = false;

const char *setupPage() {
  return R"(<!DOCTYPE html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>OMOTE Bridge WiFi</title><style>body{font-family:sans-serif;max-width:400px;margin:2em auto;padding:1em}
input,button{width:100%;padding:12px;margin:8px 0;box-sizing:border-box}button{background:#3366cc;color:#fff;border:0}</style></head>
<body><h1>OMOTE Bridge</h1><p>Connect your phone to <b>OMOTE-Bridge-Setup</b>, then enter your home WiFi details.</p>
<form method=POST action=/save>
<label>Network name (SSID)</label><input name=ssid required autocomplete=off>
<label>Password</label><input name=password type=password autocomplete=off>
<button type=submit>Save &amp; reboot</button></form></body></html>)";
}

void handleRoot() { server->send(200, "text/html", setupPage()); }

void handleSave() {
  if (!server->hasArg("ssid")) {
    server->send(400, "text/plain", "Missing ssid");
    return;
  }
  Preferences preferences;
  preferences.begin("wifiSettings", false);
  preferences.putString("SSID", server->arg("ssid"));
  preferences.putString("password", server->arg("password"));
  preferences.end();

  server->send(200, "text/html", "<html><body><h2>Saved. Rebooting bridge...</h2></body></html>");
  server->client().flush();
  delay(800);
  ESP.restart();
}

void handleCaptiveRedirect() {
  server->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  server->send(302, "text/plain", "");
}

} // namespace

void start() {
  if (active)
    return;

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(kApName, nullptr, 6, 0, 8)) {
    Serial.println("[bridge] captive portal: softAP failed");
    return;
  }

  Serial.printf("[bridge] captive portal: join WiFi \"%s\" then open http://%s\n", kApName,
                WiFi.softAPIP().toString().c_str());

  dns = std::make_unique<DNSServer>();
  server = std::make_unique<WebServer>(80);

  dns->start(53, "*", WiFi.softAPIP());
  server->on("/", HTTP_GET, handleRoot);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/generate_204", HTTP_GET, handleCaptiveRedirect);
  server->on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
  server->on("/fwlink", HTTP_GET, handleCaptiveRedirect);
  server->onNotFound(handleCaptiveRedirect);
  server->begin();
  active = true;
}

void loop() {
  if (!active || !dns || !server)
    return;
  dns->processNextRequest();
  server->handleClient();
}

bool isActive() { return active; }

const char *apName() { return kApName; }

} // namespace bridge_portal
