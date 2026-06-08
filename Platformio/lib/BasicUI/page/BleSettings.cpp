#include "BleSettings.hpp"

#ifndef IS_SIMULATOR
#include <Arduino.h>
#endif

#include "Button.hpp"
#include "HardwareFactory.hpp"
#include "UiOverlayGate.hpp"
#include "ble_scene.hpp"
#include "Label.hpp"
#ifndef IS_SIMULATOR
#include "HaWebSocket.hpp"
#endif
#include "device_settings.hpp"

using namespace UI::Page;

namespace {

struct BleProfileOption {
  const char *key;
  const char *label;
};

constexpr BleProfileOption kBleProfiles[] = {
    {"generic", "Generic (recommended)"},
    {"onn-full-keyboard", "Onn keyboard + remote"},
    {"google-reference-rcu", "Google reference RCU"},
    {"apple-keyboard", "Apple keyboard"},
};

static void stopTimer(lv_timer_t *&timer) {
  if (timer) {
    lv_timer_del(timer);
    timer = nullptr;
  }
}

} // namespace

BleSettings::BleSettings() : Base(ID::Pages::BleSettings), mBle(HardwareFactory::getAbstract().ble()) {
  lv_obj_add_flag(LvglSelf(), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(LvglSelf(), LV_DIR_VER);
  lv_obj_set_scrollbar_mode(LvglSelf(), LV_SCROLLBAR_MODE_AUTO);

  const lv_coord_t width = GetContentWidth();
  static constexpr lv_coord_t kGap = 8;
  static constexpr lv_coord_t kBtnH = 32;

  mStatusLabel = AddNewElement<Widget::Label>("BLE: …");
  mStatusLabel->SetWidth(width);
  mStatusLabel->AlignTo(this, LV_ALIGN_TOP_LEFT, 0, 4);

  mHintLabel = AddNewElement<Widget::Label>(
      "Pair from here or use a scene with BleEnabled. Keys send only in BLE scenes.");
  mHintLabel->SetWidth(width);
  mHintLabel->AlignTo(mStatusLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  auto *profileLabel = AddNewElement<Widget::Label>("HID identity (VID/PID)");
  profileLabel->SetWidth(width);
  profileLabel->AlignTo(mHintLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, kGap);

  mProfileDropDown = AddNewElement<Widget::DropDown<std::string>>(
      [this](const std::string &value) { applyProfile(value); });
  mProfileDropDown->SetWidth(width);
  mProfileDropDown->SetHeight(30);
  mProfileDropDown->AlignTo(profileLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  std::string current = device_settings::currentConst().bleProfile;
  if (current.empty())
    current = "generic";
  for (const auto &opt : kBleProfiles) {
    mProfileDropDown->AddItem(opt.label, opt.key);
    if (opt.key == current)
      mProfileDropDown->SetSelected(opt.key);
  }

  UIElement *anchor = mProfileDropDown;

  auto addActionButton = [&](const char *text, auto &&handler) {
    auto *btn = AddNewElement<Widget::Button>();
    btn->SetText(text);
    btn->SetWidth(width);
    btn->SetHeight(kBtnH);
    btn->AlignTo(anchor, LV_ALIGN_OUT_BOTTOM_LEFT, 0, kGap);
    btn->OnShortClick(std::forward<decltype(handler)>(handler));
    anchor = btn;
  };

  if (!mBle) {
    auto *unavail = AddNewElement<Widget::Label>("BLE not available on this build.");
    unavail->SetWidth(width);
    unavail->AlignTo(anchor, LV_ALIGN_OUT_BOTTOM_LEFT, 0, kGap);
    return;
  }

  addActionButton("Start pairing", [this] {
    UiOverlayGate::prepareRam();
#ifndef IS_SIMULATOR
    HaWebSocket::suspendForBlePairing();
#endif
    ble_scene::requestSettingsPairing();
    if (mStatusLabel)
      mStatusLabel->SetText("BLE: starting… look for Omote Remote on TV");
  });

  addActionButton("Disconnect", [this] {
    if (!ensureBleReady())
      return;
    mBle->disconnectClients();
    refreshStatus();
  });

  addActionButton("Forget bonds", [this] {
    if (!ensureBleReady())
      return;
    mBle->forgetBonds();
    ble_scene::markBleRunning();
    refreshStatus();
  });

  addActionButton("Refresh status", [this] { refreshStatus(); });

  refreshStatus();
}

BleSettings::~BleSettings() { stopTimer(mRefreshTimer); }

bool BleSettings::ensureBleReady() {
  if (!mBle)
    return false;
#ifndef IS_SIMULATOR
  static constexpr uint32_t kMinHeapForBleInit = 36000;
  if (!mBle->isInitialized() && ESP.getFreeHeap() < kMinHeapForBleInit) {
    if (mStatusLabel)
      mStatusLabel->SetText("BLE: not enough free RAM — close scene first");
    return false;
  }
#endif
  if (!mBle->isInitialized()) {
    mBle->setProfile(device_settings::currentConst().bleProfile);
    mBle->init();
  }
  return mBle->isInitialized();
}

void BleSettings::applyProfile(const std::string &profileKey) {
  if (!mBle || profileKey.empty())
    return;
  mBle->setProfile(profileKey);
  rapidjson::Document patch;
  patch.SetObject();
  auto &a = patch.GetAllocator();
  patch.AddMember("ble_profile", rapidjson::Value(profileKey.c_str(), a), a);
  device_settings::mergeFromJson(patch);
  device_settings::saveToLittleFS();
  if (!ensureBleReady())
    return;
  mBle->forgetBonds();
  ble_scene::markBleRunning();
  refreshStatus();
}

void BleSettings::refreshStatus() {
  if (!mStatusLabel)
    return;
  if (!mBle) {
    mStatusLabel->SetText("BLE: unavailable");
    return;
  }
  if (ble_scene::settingsPairingPending()) {
    mStatusLabel->SetText("BLE: starting… look for Omote Remote on TV");
    return;
  }
  std::string text = "BLE: ";
  text += mBle->isConnected() ? "connected" : "off";
  if (mBle->isPairingMode())
    text += " (pairing)";
  if (mBle->isAdvertising())
    text += " adv";
  if (mBle->isInitialized()) {
    text += " · ready";
  } else {
    text += " · idle";
#ifndef IS_SIMULATOR
    text += " (";
    text += std::to_string(ESP.getFreeHeap() / 1024);
    text += "k free)";
#endif
  }
  mStatusLabel->SetText(text);
}

void BleSettings::OnShow() {
  Base::OnShow();
  refreshStatus();
  stopTimer(mRefreshTimer);
  mRefreshTimer = lv_timer_create(
      [](lv_timer_t *t) {
        auto *self = static_cast<BleSettings *>(lv_timer_get_user_data(t));
        if (self)
          self->refreshStatus();
      },
      2000, this);
}

void BleSettings::OnHide() {
  stopTimer(mRefreshTimer);
#ifndef IS_SIMULATOR
  HaWebSocket::resumeAfterBlePairing();
#endif
  Base::OnHide();
}
