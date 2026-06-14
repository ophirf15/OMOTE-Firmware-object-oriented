#pragma once
#include "BatteryRevX.hpp"
#include "Hardware/LoggingInterface.hpp"

class BatteryRev1 : public BatteryRevX {
public:
  /**
   * @brief Get the SOC of the battery
   *
   * @return int SOC of the battery
   */
  int getDirectSOC() override;

  /**
   * @brief Get the raw soc value of the battery
   *
   * @return raw MAX17048 SOC register value
   */
  uint16_t getRawSOC() override;

  /**
   * @brief Function to get the current voltage of the battery
   *
   * @return int Voltage of the battery in mV
   */
  virtual int getVoltage() override;

  /**
   * @brief Function to determine if the battery is charging or not
   *
   * @return true   Battery is currently charging
   * @return false  Battery is currently not charging
   */
  virtual bool isCharging() override;

  bool isConnected();

  bool isPluggedIn() const override;
  bool isChargingLatched() const override { return mChargingLatched; }

  BatteryRev1(int adc_pin, int charging_pin);

  int getPercentage() override;

  int getChargePinLows() const override { return mLastChargePinLows; }
  int getChargePinSampleCount() const override { return kChargePinSamples; }

  static constexpr int kChargePinSamples = 32;

  // Not sure why this is needed but shared_ptr seems to really
  // need it possibly a compiler template handling limitation
  // none the less we really should not use it.
  BatteryRev1() = default;

private:

  /**
   * @brief Variable to store which pin should be used for ADC
   *
   */
  int mAdcPin;

  /**
   * @brief Variable to store which pin is used to indicate if the battery is
   * currently charging or not
   *
   */
  int mChargingPin;

  int mFilteredVoltageMv = 0;
  int mDisplayedSoc = -1;
  bool mChargingLatched = false;
  bool mChargingLastRaw = false;
  uint8_t mChargingStable = 0;
  int mLastChargePinLows = 0;
  int mVoltageBaselineMv = 0;
  uint32_t mLastDebugMs = 0;
  uint8_t mSocDropStreak = 0;
  uint8_t mLowVoltageStreak = 0;
  uint8_t mVoltageOutlierStreak = 0;

  int readRawVoltageMv();
  int sampleChargingPinLows();
  void updateVoltageBaseline(int mv);

  using BatteryRevX::mLogger;
  using BatteryRevX::mLogStream;
};