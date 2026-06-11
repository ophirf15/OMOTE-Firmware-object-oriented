#pragma once

namespace bridge_status_led {

void init();
void tick();

/** Brief flash when a BLE HID key is sent to the TV. */
void onBleKey();

} // namespace bridge_status_led
