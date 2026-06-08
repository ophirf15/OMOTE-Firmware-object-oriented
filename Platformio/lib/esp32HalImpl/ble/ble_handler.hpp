#pragma once

#include "Hardware/BleHandlerInterface.h"

#include <memory>

#ifndef IS_SIMULATOR

class BleHandler : public BleHandlerInterface {
public:
  void setProfile(const std::string &profileKey) override;
  std::string currentProfile() const override;
  std::string identityListJson() const override;

  void init() override;
  void shutdown() override;
  bool isInitialized() const override;
  bool isConnected() const override;
  bool isAdvertising() const override;
  bool isPairingMode() const override;

  void startAdvertising() override;
  void stopAdvertising() override;
  void onWake(uint32_t windowMs = 60000) override;
  void startPairingMode() override;
  void stopPairingMode() override;
  void disconnectClients() override;
  void forgetBonds() override;

  void sendKey(const std::string &keyName) override;
  void sendText(const std::string &text) override;
  bool sendRawConsumerUsage(uint16_t usage) override;
  bool sendRawButton(uint8_t button1to16) override;

  void taskLoop() override;
  std::string statusJson() const override;
};

std::shared_ptr<BleHandlerInterface> createBleHandler();

#endif // !IS_SIMULATOR
