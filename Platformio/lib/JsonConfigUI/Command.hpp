#pragma once
#include "Hardware/IRInterface.h"
#include "Hardware/KeyPressAbstract.hpp"
#include "Hardware/wifi/wifiHandlerInterface.h"
#include "RapidJsonUtilty.hpp"

namespace Command {

typedef enum {
  NONE = 0,
  MQTT,
  IR,
  BLE
} CommandMode;

using KeyIds = KeyPressAbstract::KeyId;
using KeyPressTypes = KeyPressAbstract::KeyEvent::Type;

struct CommandStruct {
  CommandMode mode = NONE;
  std::string protocol;
  std::vector<std::string> data;
};

enum class KeyActionKind {
  Command = 0,
  Ha,
};

struct HaKeySpec {
  std::string domain;
  std::string service;
  std::string entityId;
};

struct KeyStruct {
  KeyPressTypes pressType = KeyPressTypes::INVALID;
  KeyActionKind kind = KeyActionKind::Command;
  CommandStruct command;
  HaKeySpec ha;
};

class Commands {

public:
  Commands() {};
  ~Commands() {};
  static void sendCommand(const CommandStruct &aCommandStruct);
  /** Dispatch IR/BLE/MQTT command or Home Assistant service bound to a physical key. */
  static bool executeKey(const KeyStruct &aKey);
  static CommandMode getCommand(const std::string &aCommandFIle, const std::string &aCommandPrefix, const std::string &aCommand, CommandStruct &aCommandStrings);
  /** Drop parsed command JSON held in RAM (call before loading a new scene). */
  static void releaseCachedDocuments();
};
} // namespace Command