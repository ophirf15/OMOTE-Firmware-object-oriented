#pragma once



namespace bridge_olp_host {



void init();

void tick();



void pushHaState(const char *entityId, const char *state);
void pushHaStateAttrs(const char *entityId, const char *attributesJson);

/** True if a physical remote has contacted this bridge (ESP-NOW). */
bool isRemoteLinked();

/** Tell linked remote to re-pull config from bridge LittleFS. */
void notifyConfigChanged();

} // namespace bridge_olp_host

