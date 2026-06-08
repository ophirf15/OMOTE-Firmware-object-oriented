#include "Command.hpp"
#include "HardwareFactory.hpp"
#include "ble_scene.hpp"
#include <fstream>
#include <unordered_map>

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

} // namespace

CommandMode Commands::getCommand(const std::string &aCommandFile, const std::string &aCommandPrefix, const std::string &aCommand, CommandStruct &aCommandStruct) {

  std::filesystem::path commandFilePath(FS_PATH + aCommandFile);

  const rapidjson::Document &d = loadCommandDocument(commandFilePath);
  if (d.HasParseError() || d.IsNull())
    return NONE;

  std::string fullCommand(aCommand);
  if (!aCommandPrefix.empty())
    fullCommand.insert(0, aCommandPrefix);

  std::string protocol;
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
}

void Commands::releaseCachedDocuments() { commandDocCache().clear(); }

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
    if (!key.empty()) {
      ble_scene::requestBleStart();
      if (auto ble = HardwareFactory::getAbstract().ble())
        ble->sendKey(key);
    }
  }
}
