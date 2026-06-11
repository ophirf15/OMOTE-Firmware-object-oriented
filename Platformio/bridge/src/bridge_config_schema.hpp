#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bridge_config_schema {

/** Ensure DeviceSettings.schema.json on LittleFS parses as JSON with sections. */
bool ensureOnDisk();

/** Load schema bytes for OLP transfer (repairs on disk first if needed). */
bool loadBytes(std::vector<uint8_t> &out);

} // namespace bridge_config_schema
