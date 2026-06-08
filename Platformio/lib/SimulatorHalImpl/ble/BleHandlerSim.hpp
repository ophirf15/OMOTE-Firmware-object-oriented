#pragma once

#include "Hardware/BleHandlerInterface.h"

class BleHandlerSim : public BleHandlerInterface {
public:
  void setProfile(const std::string &profileKey) override { mProfile = profileKey; }
  std::string currentProfile() const override { return mProfile; }
  std::string identityListJson() const override {
    return "{\"current\":\"generic\",\"identities\":[{\"key\":\"generic\",\"name\":\"Generic (sim)\","
           "\"vid\":20293,\"pid\":20308,\"recommended\":true,\"description\":\"Simulator stub\"}]}";
  }

  void init() override { mInitialized = true; }
  void shutdown() override { mInitialized = false; mConnected = false; }
  bool isInitialized() const override { return mInitialized; }
  bool isConnected() const override { return mConnected; }
  bool isAdvertising() const override { return mInitialized && !mConnected; }
  bool isPairingMode() const override { return false; }

  void startAdvertising() override {}
  void stopAdvertising() override {}
  void onWake(uint32_t) override {}
  void startPairingMode() override {}
  void stopPairingMode() override {}
  void disconnectClients() override { mConnected = false; }
  void forgetBonds() override {}

  void sendKey(const std::string &keyName) override { mLastKey = keyName; }
  void sendText(const std::string &text) override { mLastText = text; }
  bool sendRawConsumerUsage(uint16_t) override { return mConnected; }
  bool sendRawButton(uint8_t) override { return mConnected; }

  void taskLoop() override {}
  std::string statusJson() const override {
    return "{\"connected\":false,\"initialized\":false,\"advertising\":false,\"pairing_mode\":false,"
           "\"bond_count\":0,\"bonds\":[]}";
  }

  std::string lastKey() const { return mLastKey; }
  std::string lastText() const { return mLastText; }

private:
  std::string mProfile = "generic";
  std::string mLastKey;
  std::string mLastText;
  bool mInitialized = false;
  bool mConnected = false;
};
