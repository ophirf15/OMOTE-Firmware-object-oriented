#include "bridge_config_schema.hpp"

#include "default_device_settings_schema.hpp"

#include <Arduino.h>
#include <LittleFS.h>

#include "rapidjson/document.h"

#ifndef FS_PATH
#define FS_PATH "/littlefs/"
#endif

namespace bridge_config_schema {
namespace {

constexpr const char *kSchemaRel = "DeviceSettings.schema.json";

bool schemaBytesValid(const uint8_t *data, size_t len) {
  if (!data || len < 8)
    return false;
  rapidjson::Document doc;
  doc.Parse(reinterpret_cast<const char *>(data), len);
  return !doc.HasParseError() && doc.IsObject() && doc.HasMember("sections") &&
         doc["sections"].IsArray();
}

bool readFileBytes(const char *fsPath, std::vector<uint8_t> &out) {
  File f = LittleFS.open(fsPath, "r");
  if (!f)
    return false;
  const size_t fileSz = f.size();
  if (fileSz == 0 || fileSz > 65535) {
    f.close();
    return false;
  }
  out.resize(fileSz);
  size_t got = 0;
  while (got < fileSz) {
    const int n = f.read(out.data() + got, fileSz - got);
    if (n <= 0)
      break;
    got += static_cast<size_t>(n);
  }
  f.close();
  return got == fileSz;
}

bool writeDefaultSchema() {
  const char *def = kDefaultDeviceSettingsSchema;
  const size_t len = strlen(def);
  const String full = String(FS_PATH) + kSchemaRel;
  File f = LittleFS.open(full, "w");
  if (!f)
    return false;
  const size_t written = f.write(reinterpret_cast<const uint8_t *>(def), len);
  f.close();
  if (written != len) {
    LittleFS.remove(full);
    return false;
  }
  Serial.printf("[bridge_schema] restored firmware default (%u bytes)\n", static_cast<unsigned>(len));
  return true;
}

} // namespace

bool ensureOnDisk() {
  const String full = String(FS_PATH) + kSchemaRel;
  std::vector<uint8_t> buf;
  if (!readFileBytes(full.c_str(), buf) || !schemaBytesValid(buf.data(), buf.size())) {
    if (buf.empty())
      Serial.println("[bridge_schema] missing DeviceSettings.schema.json — writing default");
    else
      Serial.printf("[bridge_schema] corrupt schema (%u bytes) — writing default\n",
                    static_cast<unsigned>(buf.size()));
    return writeDefaultSchema();
  }
  return true;
}

bool loadBytes(std::vector<uint8_t> &out) {
  if (!ensureOnDisk())
    return false;
  const String full = String(FS_PATH) + kSchemaRel;
  return readFileBytes(full.c_str(), out);
}

} // namespace bridge_config_schema
