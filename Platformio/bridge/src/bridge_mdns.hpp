#pragma once

#include <IPAddress.h>

namespace bridge_mdns {

/** Call once after WiFi is connected. Advertises omote.local for the editor. */
void begin();

bool ready();

/** Resolve hostname or IP (incl. homeassistant.local) via mDNS then DNS. */
bool resolve(const char *hostname, IPAddress &out);

} // namespace bridge_mdns
