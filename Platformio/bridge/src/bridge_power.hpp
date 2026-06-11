#pragma once

namespace bridge_power {

void init();
void tick();

void setRemoteAwake(bool awake);
bool remoteAwake();
bool allowHaPolling();

/** Any ESP-NOW traffic from the remote (ping, keys, power, etc.). */
void noteRemoteActivity();

} // namespace bridge_power
