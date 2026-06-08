#pragma once

#include <rapidjson/document.h>

namespace device_settings_schema {

/** Loaded from /littlefs/DeviceSettings.schema.json */
const rapidjson::Document &document();
/** @param forceReload set true after editor deploy overwrites the schema file */
bool loadFromLittleFS(bool forceReload = false);
bool isLoaded();

} // namespace device_settings_schema
