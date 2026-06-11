#include "bridge_status_led.hpp"

#if defined(BRIDGE_STATUS_LED_GPIO)

#include "bridge_ble_host.hpp"
#include "bridge_olp_host.hpp"
#include "bridge_power.hpp"
#include "captive_portal.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp32-hal-rgb-led.h>

namespace bridge_status_led {
namespace {

constexpr uint8_t kPin = BRIDGE_STATUS_LED_GPIO;
constexpr uint8_t kMaxBright = 52;

uint32_t sKeyFlashUntil = 0;

uint8_t dim(uint8_t v) { return static_cast<uint8_t>((uint16_t)v * kMaxBright / 255); }

void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWriteOrdered(kPin, LED_COLOR_ORDER_GRB, r, g, b);
}

uint8_t breathe(uint32_t periodMs, uint32_t now, uint8_t peak) {
  const uint32_t phase = now % periodMs;
  const uint32_t half = periodMs / 2;
  const uint8_t level =
      phase < half ? static_cast<uint8_t>((uint32_t)peak * phase / half)
                   : static_cast<uint8_t>((uint32_t)peak * (periodMs - phase) / half);
  return dim(level);
}

} // namespace

void init() {
  setRgb(0, 0, 0);
  Serial.printf("[bridge_led] status RGB on GPIO %u\n", static_cast<unsigned>(kPin));
}

void onBleKey() { sKeyFlashUntil = millis() + 90; }

void tick() {
  const uint32_t now = millis();

  if (now < sKeyFlashUntil) {
    setRgb(dim(255), dim(255), dim(180));
    return;
  }

  if (bridge_portal::isActive()) {
    const uint8_t p = breathe(1800, now, 255);
    setRgb(p, p / 4, 0);
    return;
  }

  if (!bridge_power::remoteAwake()) {
    setRgb(0, 0, dim(12));
    return;
  }

  if (bridge_olp_host::isConfigTransferActive()) {
    const uint8_t p = breathe(350, now, 255);
    setRgb(static_cast<uint8_t>(p * 55 / 100), 0, p);
    return;
  }

  if (bridge_ble_host::isConnected()) {
    setRgb(0, dim(230), dim(30));
    return;
  }

  if (bridge_ble_host::isPairingMode()) {
    const uint8_t p = breathe(700, now, 255);
    setRgb(p, 0, p);
    return;
  }

  if (bridge_ble_host::sceneArmed() && bridge_ble_host::isAdvertising()) {
    const uint8_t p = breathe(1400, now, 200);
    setRgb(0, p, 0);
    return;
  }

  if (bridge_olp_host::isRemoteLinked()) {
    setRgb(0, dim(170), dim(210));
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    setRgb(0, 0, dim(70));
    return;
  }

  setRgb(dim(60), 0, 0);
}

} // namespace bridge_status_led

#else

namespace bridge_status_led {

void init() {}
void tick() {}
void onBleKey() {}

} // namespace bridge_status_led

#endif
