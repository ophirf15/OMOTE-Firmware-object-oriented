#include "BatteryRev1.hpp"
#include <Arduino.h>

namespace {

constexpr int kAdcSamples = 16;
constexpr int kChargePinSamples = 32;
// MCP73831 STAT on CRG_STAT (GPIO21). Schematic: LOW=charging, HIGH=complete/unplugged.
// Measured on Rev1 hardware the net behaves differently when the charger IC is off:
//   unplugged (MCP73831 unpowered) -> pin reads solid LOW (32/32 samples)
//   plugged + charging             -> pin reads mostly LOW (22-31/32 samples)
//   plugged + charge complete      -> pin reads mostly HIGH (<=12/32 samples)
constexpr int kSolidLowLows = 32;
constexpr int kChargingBandMinLows = 22;
constexpr int kChargingBandMaxLows = 31;
constexpr int kIdleHighLows = 12;
constexpr int kPluggedAbsVoltageMv = 4155;
constexpr int kPluggedAboveBaselineMv = 110;
constexpr int kBaselineTrackMaxMv = 4140;
constexpr uint8_t kChargeStableNeeded = 2;
constexpr int kSocEmaAlphaUp = 6;
constexpr int kSocEmaAlphaDown = 1;
constexpr int kSocDropConfirmReads = 3;
constexpr int kSocMaxDropPerUpdate = 1;
// Single-cell LiPo: ~3.3V empty (under load) to ~4.05V full on this hardware.
// 3700mV is nominal, not empty — the old 3700-4050 window (350mV) made every
// ADC twitch look like a huge SOC jump.
constexpr int kEmptyMv = 3300;
constexpr int kFullMv = 4050;
constexpr int kVoltageOutlierMv = 100;
constexpr uint8_t kLowVoltageZeroReads = 3;

// Returns -1 hold latch, 0 not charging, 1 charging
int classifyChargePin(int lows) {
  if (lows == kSolidLowLows)
    return 0;
  if (lows >= kChargingBandMinLows && lows <= kChargingBandMaxLows)
    return 1;
  if (lows <= kIdleHighLows)
    return 0;
  return -1;
}

} // namespace

BatteryRev1::BatteryRev1(int adc_pin, int charging_pin)
    : mAdcPin(adc_pin), mChargingPin(charging_pin) {
  pinMode(mChargingPin, INPUT_PULLUP);
  pinMode(mAdcPin, INPUT);
  mFilteredVoltageMv = readRawVoltageMv();
  mLastChargePinLows = sampleChargingPinLows();
  const int initClass = classifyChargePin(mLastChargePinLows);
  mChargingLatched = initClass == 1;
  mChargingLastRaw = mChargingLatched;
  mChargingStable = kChargeStableNeeded;
  mVoltageBaselineMv = mFilteredVoltageMv;
  Serial.printf("[BAT] init pin=%d mV=%d charging=%s plugged=%s (lows=%d/%d)\n",
                mChargingPin, mFilteredVoltageMv, mChargingLatched ? "yes" : "no",
                isPluggedIn() ? "yes" : "no", mLastChargePinLows, kChargePinSamples);
}

int BatteryRev1::readRawVoltageMv() {
  uint32_t sum = 0;
  uint16_t minVal = 4095;
  uint16_t maxVal = 0;
  for (int i = 0; i < kAdcSamples; ++i) {
    const uint16_t sample = static_cast<uint16_t>(analogRead(mAdcPin));
    sum += sample;
    if (sample < minVal)
      minVal = sample;
    if (sample > maxVal)
      maxVal = sample;
  }
  sum -= minVal + maxVal;
  const int avg = static_cast<int>(sum / (kAdcSamples - 2));
  return avg * 2 * 3300 / 4095 + 325;
}

int BatteryRev1::sampleChargingPinLows() {
  pinMode(mChargingPin, INPUT_PULLUP);
  int lows = 0;
  for (int i = 0; i < kChargePinSamples; ++i) {
    if (digitalRead(mChargingPin) == LOW)
      lows++;
    delayMicroseconds(500);
  }
  mLastChargePinLows = lows;
  return lows;
}

int BatteryRev1::getDirectSOC() {
  return constrain(map(getVoltage(), kEmptyMv, kFullMv, 0, 100), 0, 100);
}

uint16_t BatteryRev1::getRawSOC() {
  return static_cast<uint16_t>(
      constrain(map(getVoltage(), kEmptyMv - 300, kFullMv + 50, 0, 100 * 256), 0, 100 * 256));
}

void BatteryRev1::updateVoltageBaseline(int mv) {
  if (mVoltageBaselineMv <= 0) {
    mVoltageBaselineMv = mv;
    return;
  }
  if (mv < mVoltageBaselineMv)
    mVoltageBaselineMv = mv;
  else if (mv < kBaselineTrackMaxMv && mv > mVoltageBaselineMv)
    mVoltageBaselineMv += (mv - mVoltageBaselineMv) / 32;
}

bool BatteryRev1::isPluggedIn() const {
  const int lows = mLastChargePinLows;
  if (lows >= kChargingBandMinLows && lows <= kChargingBandMaxLows)
    return true;
  if (lows <= kIdleHighLows)
    return true;
  if (lows == kSolidLowLows) {
    if (mFilteredVoltageMv >= kPluggedAbsVoltageMv)
      return true;
    if (mVoltageBaselineMv > 0 &&
        mFilteredVoltageMv >= mVoltageBaselineMv + kPluggedAboveBaselineMv)
      return true;
  }
  return false;
}

bool BatteryRev1::isCharging() {
  const int lows = sampleChargingPinLows();
  updateVoltageBaseline(getVoltage());
  const int classified = classifyChargePin(lows);
  const bool prevLatched = mChargingLatched;

  if (classified >= 0) {
    const bool raw = classified == 1;
    if (raw == mChargingLatched)
      mChargingStable = kChargeStableNeeded;
    else if (raw == mChargingLastRaw) {
      if (mChargingStable < kChargeStableNeeded)
        mChargingStable++;
    } else {
      mChargingLastRaw = raw;
      mChargingStable = 1;
    }
    if (mChargingStable >= kChargeStableNeeded)
      mChargingLatched = raw;
  }

  if (mChargingLatched != prevLatched) {
    Serial.printf("[BAT] %s pin=%d lows=%d/%d stable=%u mV=%d soc=%d%%\n",
                  mChargingLatched ? "CHARGING (active)" : (isPluggedIn() ? "PLUGGED (idle)" : "ON-BATTERY"),
                  mChargingPin, mLastChargePinLows, kChargePinSamples, mChargingStable,
                  mFilteredVoltageMv, mDisplayedSoc < 0 ? getDirectSOC() : mDisplayedSoc);
  }

  const uint32_t now = millis();
  if (now - mLastDebugMs >= 5000) {
    mLastDebugMs = now;
    const int soc = mDisplayedSoc < 0 ? getDirectSOC() : mDisplayedSoc;
    const char *zone = classified < 0 ? "uncertain" : (classified ? "charging" : "idle");
    Serial.printf("[BAT] status latched=%s plugged=%s zone=%s lows=%d/%d mV=%d baseline=%d soc=%d%%\n",
                  mChargingLatched ? "charging" : "not-charging", isPluggedIn() ? "yes" : "no", zone,
                  mLastChargePinLows, kChargePinSamples, mFilteredVoltageMv, mVoltageBaselineMv, soc);
  }

  return mChargingLatched;
}

bool BatteryRev1::isConnected() {
  return !mChargingLatched && getVoltage() < 4350;
}

int BatteryRev1::getVoltage() {
  const int raw = readRawVoltageMv();
  if (mFilteredVoltageMv <= 0) {
    mFilteredVoltageMv = raw;
    mVoltageOutlierStreak = 0;
    return mFilteredVoltageMv;
  }

  if (raw < mFilteredVoltageMv - kVoltageOutlierMv) {
    if (mVoltageOutlierStreak < 255)
      mVoltageOutlierStreak++;
    if (mVoltageOutlierStreak < 3)
      return mFilteredVoltageMv;
  } else {
    mVoltageOutlierStreak = 0;
  }

  if (raw < mFilteredVoltageMv - 250)
    mFilteredVoltageMv += (raw - mFilteredVoltageMv) / 16;
  else
    mFilteredVoltageMv += (raw - mFilteredVoltageMv) / 4;
  return mFilteredVoltageMv;
}

int BatteryRev1::getPercentage() {
  const int rawSoc = BatteryRevX::getPercentage();
  if (mDisplayedSoc < 0) {
    mDisplayedSoc = rawSoc;
    mSocDropStreak = 0;
    mLowVoltageStreak = 0;
    return rawSoc;
  }

  const int prev = mDisplayedSoc;
  if (rawSoc >= mDisplayedSoc) {
    mSocDropStreak = 0;
    mDisplayedSoc += (rawSoc - mDisplayedSoc) * kSocEmaAlphaUp / 16;
  } else if (mChargingLatched || isPluggedIn()) {
    mSocDropStreak = 0;
    mDisplayedSoc = prev;
  } else {
    mSocDropStreak++;
    if (mSocDropStreak >= kSocDropConfirmReads) {
      int next = prev + (rawSoc - prev) * kSocEmaAlphaDown / 16;
      if (next < prev - kSocMaxDropPerUpdate)
        next = prev - kSocMaxDropPerUpdate;
      mDisplayedSoc = next;
    }
  }

  if (mFilteredVoltageMv < kEmptyMv - 100) {
    if (mLowVoltageStreak < 255)
      mLowVoltageStreak++;
    if (mLowVoltageStreak >= kLowVoltageZeroReads)
      mDisplayedSoc = 0;
  } else {
    mLowVoltageStreak = 0;
    if (mFilteredVoltageMv >= kFullMv - 20)
      mDisplayedSoc = 100;
  }

  return mDisplayedSoc;
}
