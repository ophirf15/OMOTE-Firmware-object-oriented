#pragma once

#include <cstdint>
#include <string>

namespace bridge_ble_host {

void init();
void tick();
void reloadProfileFromDisk();

bool available();
bool sendKey(const std::string &keyName);
bool control(uint8_t action, const std::string &profile = {});
void setSceneArmed(bool armed);
bool sceneArmed();

std::string statusJson();
std::string identityListJson();
void sendStatusToRemote(const uint8_t mac[6]);

} // namespace bridge_ble_host
