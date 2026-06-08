#pragma once

#include <cstdint>
#include <string>

/** BLE HID remote control (NimBLE). Scene-gated: init only when a BLE scene is active. */
class BleHandlerInterface {
public:
  BleHandlerInterface() = default;
  virtual ~BleHandlerInterface() = default;

  virtual void setProfile(const std::string &profileKey) = 0;
  virtual std::string currentProfile() const = 0;
  virtual std::string identityListJson() const = 0;

  virtual void init() = 0;
  virtual void shutdown() = 0;
  virtual bool isInitialized() const = 0;
  virtual bool isConnected() const = 0;
  virtual bool isAdvertising() const = 0;
  virtual bool isPairingMode() const = 0;

  virtual void startAdvertising() = 0;
  virtual void stopAdvertising() = 0;
  virtual void onWake(uint32_t windowMs = 60000) = 0;
  virtual void startPairingMode() = 0;
  virtual void stopPairingMode() = 0;
  virtual void disconnectClients() = 0;
  virtual void forgetBonds() = 0;

  virtual void sendKey(const std::string &keyName) = 0;
  virtual void sendText(const std::string &text) = 0;
  virtual bool sendRawConsumerUsage(uint16_t usage) = 0;
  virtual bool sendRawButton(uint8_t button1to16) = 0;

  virtual void taskLoop() = 0;
  virtual std::string statusJson() const = 0;
};
