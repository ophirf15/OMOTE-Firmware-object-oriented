#include "Command.hpp"
#include "HaRuntime.hpp"
#include "SceneKeyDispatch.hpp"
#include "HardwareFactory.hpp"
#if OMOTE_BLE
#include "ble_scene.hpp"
#elif defined(OMOTE_BRIDGE_CLIENT) && OMOTE_BRIDGE_CLIENT
#include "ble_scene.hpp"
#include "bridge_client.hpp"
#include <Arduino.h>
#endif
#include <fstream>
#include <unordered_map>
#if defined(ARDUINO) && !defined(IS_SIMULATOR)
#include <LittleFS.h>
#endif

using namespace Command;

namespace {

std::unordered_map<std::string, rapidjson::Document> &commandDocCache() {
  static std::unordered_map<std::string, rapidjson::Document> cache;
  return cache;
}

const rapidjson::Document &loadCommandDocument(const std::filesystem::path &commandFilePath) {
  const std::string key = commandFilePath.string();
  auto &cache = commandDocCache();
  auto it = cache.find(key);
  if (it != cache.end())
    return it->second;

  rapidjson::Document doc = OMOTE::JSON::GetDocument(commandFilePath);
  auto inserted = cache.emplace(key, std::move(doc));
  return inserted.first->second;
}

#if defined(ARDUINO) && !defined(IS_SIMULATOR)

std::unordered_map<std::string, CommandStruct> &commandEntryCache() {
  static std::unordered_map<std::string, CommandStruct> cache;
  return cache;
}

std::unordered_map<std::string, std::string> &commandFileTextCache() {
  static std::unordered_map<std::string, std::string> cache;
  return cache;
}

std::string commandEntryKey(const std::string &relPath, const std::string &fullCommand) {
  return relPath + '\0' + fullCommand;
}

std::string readTextFileLimited(const char *path, size_t maxBytes = 65536) {
  File f = LittleFS.open(path, "r");
  if (!f)
    return {};
  const size_t sz = f.size();
  if (sz == 0 || sz > maxBytes) {
    f.close();
    return {};
  }
  std::string out;
  out.resize(sz);
  size_t got = 0;
  while (got < sz) {
    const int n = f.read(reinterpret_cast<uint8_t *>(out.data() + got), sz - got);
    if (n <= 0)
      break;
    got += static_cast<size_t>(n);
  }
  f.close();
  out.resize(got);
  return out;
}

bool jsonStringFieldInBlock(const std::string &block, const char *field, std::string &value) {
  const std::string needle = std::string("\"") + field + "\"";
  for (size_t pos = 0; (pos = block.find(needle, pos)) != std::string::npos;) {
    pos += needle.size();
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\n' || block[pos] == '\r'))
      pos++;
    if (pos >= block.size() || block[pos] != ':')
      continue;
    pos++;
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\n' || block[pos] == '\r'))
      pos++;
    if (pos >= block.size() || block[pos] != '"')
      continue;
    pos++;
    value.clear();
    while (pos < block.size()) {
      const char c = block[pos++];
      if (c == '"')
        return true;
      if (c == '\\' && pos < block.size()) {
        value += block[pos++];
        continue;
      }
      value += c;
    }
    return false;
  }
  return false;
}

bool jsonStringsInArrayField(const std::string &block, const char *field, std::vector<std::string> &values) {
  const std::string needle = std::string("\"") + field + "\"";
  const size_t pos = block.find(needle);
  if (pos == std::string::npos)
    return false;
  size_t i = block.find('[', pos);
  if (i == std::string::npos)
    return false;
  i++;
  values.clear();
  while (i < block.size()) {
    while (i < block.size() && (block[i] == ' ' || block[i] == '\t' || block[i] == '\n' || block[i] == '\r' || block[i] == ','))
      i++;
    if (i >= block.size() || block[i] == ']')
      break;
    if (block[i] != '"') {
      i++;
      continue;
    }
    i++;
    std::string value;
    while (i < block.size()) {
      const char c = block[i++];
      if (c == '"') {
        values.push_back(std::move(value));
        break;
      }
      if (c == '\\' && i < block.size()) {
        value += block[i++];
        continue;
      }
      value += c;
    }
  }
  return true;
}

bool parseCommandBlock(const std::string &block, CommandStruct &row) {
  std::string name;
  std::string mode;
  if (!jsonStringFieldInBlock(block, "Command", name) || !jsonStringFieldInBlock(block, "Mode", mode))
    return false;

  row = {};
  row.mode = magic_enum::enum_cast<CommandMode>(mode).value_or(CommandMode::NONE);
  if (row.mode == CommandMode::NONE)
    return false;

  std::string protocol;
  if (jsonStringFieldInBlock(block, "Protocol", protocol))
    row.protocol = protocol;
  jsonStringsInArrayField(block, "Data", row.data);
  return true;
}

bool extractObjectBlock(const std::string &json, size_t pos, std::string &block, size_t &objEnd) {
  const size_t objStart = json.rfind('{', pos);
  if (objStart == std::string::npos)
    return false;

  size_t depth = 0;
  objEnd = std::string::npos;
  for (size_t i = objStart; i < json.size(); i++) {
    if (json[i] == '{')
      depth++;
    else if (json[i] == '}' && --depth == 0) {
      objEnd = i;
      break;
    }
  }
  if (objEnd == std::string::npos)
    return false;

  block = json.substr(objStart, objEnd - objStart + 1);
  return true;
}

bool findCommandInText(const std::string &json, const std::string &fullCommand, CommandStruct &out) {
  size_t pos = 0;
  const std::string cmdNeedle = "\"Command\"";
  while ((pos = json.find(cmdNeedle, pos)) != std::string::npos) {
    std::string block;
    size_t objEnd = 0;
    if (!extractObjectBlock(json, pos, block, objEnd)) {
      pos++;
      continue;
    }

    std::string name;
    if (!jsonStringFieldInBlock(block, "Command", name) || name != fullCommand) {
      pos = objEnd + 1;
      continue;
    }

    return parseCommandBlock(block, out);
  }
  return false;
}

const std::string &loadCommandFileText(const std::string &relPath) {
  auto &fileCache = commandFileTextCache();
  const auto it = fileCache.find(relPath);
  if (it != fileCache.end())
    return it->second;

  const std::string text = readTextFileLimited((std::string(FS_PATH) + relPath).c_str());
  return fileCache.emplace(relPath, text).first->second;
}

bool lookupCommandInFile(const std::string &relPath, const std::string &fullCommand, CommandStruct &out) {
  if (relPath.empty() || fullCommand.empty())
    return false;

  const std::string cacheKey = commandEntryKey(relPath, fullCommand);
  const auto cached = commandEntryCache().find(cacheKey);
  if (cached != commandEntryCache().end()) {
    out = cached->second;
    return out.mode != CommandMode::NONE;
  }

  const std::string &json = loadCommandFileText(relPath);
  if (json.empty())
    return false;

  CommandStruct row;
  if (!findCommandInText(json, fullCommand, row))
    return false;

  out = row;
  commandEntryCache()[cacheKey] = row;
  return true;
}

#endif

} // namespace

CommandMode Commands::getCommand(const std::string &aCommandFile, const std::string &aCommandPrefix, const std::string &aCommand, CommandStruct &aCommandStruct) {

  std::filesystem::path commandFilePath(FS_PATH + aCommandFile);

  std::string fullCommand(aCommand);
  if (!aCommandPrefix.empty())
    fullCommand.insert(0, aCommandPrefix);

#if defined(ARDUINO) && !defined(IS_SIMULATOR)
  aCommandStruct = {};
  if (lookupCommandInFile(aCommandFile, fullCommand, aCommandStruct))
    return aCommandStruct.mode;
  return NONE;
#else
  const rapidjson::Document &d = loadCommandDocument(commandFilePath);
  if (d.HasParseError() || d.IsNull())
    return NONE;

  if (d.HasMember("Commands") && d["Commands"].IsArray()) {
    for (rapidjson::SizeType i = 0; i < d["Commands"].Size(); i++) {
      if (d["Commands"][i].HasMember("Command") && d["Commands"][i]["Command"].IsString()) {
        std::string command = d["Commands"][i]["Command"].GetString();
        if (command == fullCommand) {
          if (d["Commands"][i].HasMember("Mode") && d["Commands"][i]["Mode"].IsString()) {
            aCommandStruct.mode = magic_enum::enum_cast<CommandMode>(d["Commands"][i]["Mode"].GetString()).value_or(CommandMode::NONE);
            if (aCommandStruct.mode != CommandMode::NONE) {
              if (d["Commands"][i].HasMember("Protocol") && d["Commands"][i]["Protocol"].IsString()) {
                aCommandStruct.protocol = d["Commands"][i]["Protocol"].GetString();
                if (d["Commands"][i].HasMember("Data") && d["Commands"][i]["Data"].IsArray()) {
                  for (rapidjson::SizeType j = 0; j < d["Commands"][i]["Data"].Size(); j++) {
                    if (d["Commands"][i]["Data"][j].IsString())
                      aCommandStruct.data.push_back(d["Commands"][i]["Data"][j].GetString());
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  return aCommandStruct.mode;
#endif
}

void Commands::releaseCachedDocuments() {
  commandDocCache().clear();
#if defined(ARDUINO) && !defined(IS_SIMULATOR)
  commandEntryCache().clear();
  commandFileTextCache().clear();
#endif
}

bool Commands::executeKey(const KeyStruct &aKey) {
  if (aKey.kind == KeyActionKind::Ha) {
    if (aKey.ha.entityId.empty())
      return false;
    return HaRuntime::callService(aKey.ha.domain, aKey.ha.service, aKey.ha.entityId);
  }
  if (aKey.kind == KeyActionKind::Scene) {
    if (aKey.scene.sceneFile.empty())
      return false;
    return SceneKeyDispatch::openScene(aKey.scene.sceneFile, aKey.scene.tabIndex);
  }
  if (aKey.kind == KeyActionKind::Tab) {
    return SceneKeyDispatch::switchTab(aKey.scene.tabIndex);
  }
  if (aKey.command.mode != NONE || !aKey.commandLookupName.empty()) {
    CommandStruct cmd = aKey.command;
    if (!aKey.commandLookupName.empty() && !aKey.commandLookupFile.empty()) {
      CommandStruct fresh;
      if (getCommand(aKey.commandLookupFile, aKey.commandLookupPrefix, aKey.commandLookupName, fresh) != NONE)
        cmd = fresh;
    }
    if (cmd.mode == NONE)
      return false;
    sendCommand(cmd);
    return true;
  }
  return false;
}

void Commands::sendCommand(const CommandStruct &aCommandStruct) {
  if ((aCommandStruct.mode == MQTT) && (aCommandStruct.protocol == "PUB")) {
    HardwareFactory::getAbstract().wifi()->mqttSend(aCommandStruct.data[0].c_str(), aCommandStruct.data[1].c_str());
  } else if ((aCommandStruct.mode == IR) && (aCommandStruct.data.size() > 0)) {
    HardwareFactory::getAbstract().ir()->sendBackground(aCommandStruct.protocol, aCommandStruct.data);
  } else if (aCommandStruct.mode == BLE) {
    const std::string key =
        !aCommandStruct.protocol.empty()
            ? aCommandStruct.protocol
            : (aCommandStruct.data.empty() ? std::string() : aCommandStruct.data[0]);
    if (key.empty())
      return;
#if OMOTE_BLE
    ble_scene::requestBleStart();
    if (auto ble = HardwareFactory::getAbstract().ble())
      ble->sendKey(key);
#elif defined(OMOTE_BRIDGE_CLIENT) && OMOTE_BRIDGE_CLIENT
    ble_scene::requestBleStart();
    Serial.printf("[bridge_client] BLE key %s\n", key.c_str());
    bridge_client::sendBleKey(key);
#endif
  }
}
