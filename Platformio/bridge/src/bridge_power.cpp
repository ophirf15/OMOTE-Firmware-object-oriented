#include "bridge_power.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

namespace bridge_power {
namespace {

bool sRemoteAwake = true;
bool sWifiSleepEnabled = false;
uint32_t sAsleepSinceMs = 0;
static constexpr uint32_t kWifiSleepDelayMs = 800;
static constexpr uint32_t kLightSleepMs = 50;

} // namespace

void init() {
  sRemoteAwake = true;
  sWifiSleepEnabled = false;
  WiFi.setSleep(WIFI_PS_NONE);
}

void setRemoteAwake(bool awake) {
  if (sRemoteAwake == awake)
    return;
  sRemoteAwake = awake;
  Serial.printf("[bridge_power] remote %s\n", awake ? "awake" : "sleeping");
  if (awake) {
    sWifiSleepEnabled = false;
    WiFi.setSleep(WIFI_PS_NONE);
  } else {
    sAsleepSinceMs = millis();
  }
}

void noteRemoteActivity() { setRemoteAwake(true); }

bool remoteAwake() { return sRemoteAwake; }

bool allowHaPolling() { return sRemoteAwake; }

void tick() {
  if (!sRemoteAwake) {
    if (!sWifiSleepEnabled && millis() - sAsleepSinceMs >= kWifiSleepDelayMs) {
      WiFi.setSleep(WIFI_PS_MAX_MODEM);
      sWifiSleepEnabled = true;
      Serial.println("[bridge_power] WiFi modem sleep — remote away");
    }
    if (sWifiSleepEnabled) {
      esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kLightSleepMs) * 1000ULL);
      esp_light_sleep_start();
    }
  } else if (sWifiSleepEnabled) {
    WiFi.setSleep(WIFI_PS_NONE);
    sWifiSleepEnabled = false;
  }
}

} // namespace bridge_power
