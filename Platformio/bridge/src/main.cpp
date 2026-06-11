/**

 * OMOTE bridge host — ESP32-C3 / ESP32-S3 at the TV.

 */

#include <Arduino.h>

#include <LittleFS.h>

#include <Preferences.h>

#include <WiFi.h>

#include <esp_system.h>



#include "bridge_config_http.hpp"

#include "bridge_ha.hpp"
#include "bridge_mdns.hpp"

#include "bridge_olp_host.hpp"
#include "bridge_ble_host.hpp"

#include "captive_portal.hpp"

#include "omote_link.hpp"



namespace {



constexpr uint32_t kWifiTimeoutMs = 30000;



void logBoot() {

  Serial.println();

  Serial.println("[bridge] OMOTE link host starting");

  Serial.printf("[bridge] heap=%u reset_reason=%d\n", ESP.getFreeHeap(), esp_reset_reason());

}



bool hasStoredCredentials() {

  Preferences preferences;

  if (!preferences.begin("wifiSettings", true))

    return false;

  const bool ok = !preferences.getString("SSID").isEmpty();

  preferences.end();

  return ok;

}



bool connectWifi() {

  Preferences preferences;

  if (!preferences.begin("wifiSettings", true))

    return false;

  const String ssid = preferences.getString("SSID");

  const String password = preferences.getString("password");

  preferences.end();



  if (ssid.isEmpty())

    return false;



  WiFi.persistent(false);

  WiFi.mode(WIFI_STA);

  WiFi.setHostname("omote-bridge");

  WiFi.begin(ssid.c_str(), password.c_str());



  const uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {

    delay(250);

    yield();

    Serial.print('.');

  }

  Serial.println();



  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("[bridge] WiFi connect failed — starting captive portal");

    WiFi.disconnect(true, true);

    delay(100);

    return false;

  }



  Serial.printf("[bridge] WiFi OK ssid=%s %s ch=%u heap=%u\n", ssid.c_str(), WiFi.localIP().toString().c_str(),
                WiFi.channel(), ESP.getFreeHeap());

  WiFi.setSleep(WIFI_PS_NONE);
  delay(500);
  bridge_mdns::begin();

  Serial.printf("[bridge] net: ip=%s gw=%s mask=%s\n", WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str());

  return true;

}



void startServices() {

  if (!LittleFS.begin(true)) {

    Serial.println("[bridge] LittleFS mount failed");

  } else {

    Serial.println("[bridge] LittleFS mounted");

  }



  bridge_config_http::begin();

  bridge_ha::init();

  bridge_olp_host::init();
  bridge_ble_host::init();

  omote_link::init(omote_link::Role::Host);

  omote_link::tick();

  Serial.println("[bridge] ESP-NOW + HTTP ready (editor: http://omote.local)");

}



} // namespace



void setup() {

#if ARDUINO_USB_CDC_ON_BOOT

  Serial.begin();

  const uint32_t usbWait = millis();

  while (!Serial && millis() - usbWait < 4000)

    delay(10);

#else

  Serial.begin(115200);

  delay(500);

#endif



  logBoot();



  if (!hasStoredCredentials()) {

    Serial.println("[bridge] No WiFi credentials — captive portal");

    bridge_portal::start();

    return;

  }



  if (!connectWifi()) {

    bridge_portal::start();

    return;

  }



  startServices();

}



void loop() {

  if (bridge_portal::isActive()) {

    bridge_portal::loop();

    delay(2);

    return;

  }

  omote_link::tick();

  bridge_config_http::sync();

  bridge_ha::tick();

  bridge_olp_host::tick();

  bridge_ble_host::tick();

  delay(2);

}

