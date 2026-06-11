#include "JsonUI.hpp"

#include "EditorSyncPage.hpp"
#include "HaRuntime.hpp"
#include "UiOverlayGate.hpp"
#include "HardwareFactory.hpp"
#include "JsonHomeScreen.hpp"
#include "LvglResourceManager.hpp"
#include "PopUpScreen.hpp"
#include "ScreenManager.hpp"
#include "UIBase.hpp"
#include "config_reload.hpp"
#include "device_settings.hpp"
#include "device_settings_schema.hpp"
#include "editor_sync_mode.hpp"
#ifndef IS_SIMULATOR
#include "display.hpp"
#endif

using namespace UI;

namespace {
Screen::JsonHomeScreen *gOverlayHomeScreen = nullptr;

void onUiOverlayGate(bool active) {
#ifndef IS_SIMULATOR
  HaRuntime::setOverlayActive(active);
#endif
}
} // namespace

JsonUI::JsonUI() : BasicUI() {}

void JsonUI::loopHandler() {
  static bool syncUiShown = false;
#ifndef IS_SIMULATOR
  static Screen::Base *editorSyncPopUp = nullptr;
#endif
  static bool pagesReloadPending = false;

  if (editor_sync_mode::overlayRequested() && !syncUiShown) {
    syncUiShown = true;
#ifndef IS_SIMULATOR
    auto popUp = std::make_unique<Screen::PopUpScreen>(std::make_unique<Page::EditorSyncPage>());
    editorSyncPopUp = popUp.get();
    Screen::Manager::getInstance().pushScreen(std::move(popUp), LV_SCR_LOAD_ANIM_OVER_LEFT);
#endif
  }

  if (!editor_sync_mode::overlayRequested() && syncUiShown) {
#ifndef IS_SIMULATOR
    if (editorSyncPopUp) {
      Screen::Manager::getInstance().popScreen(editorSyncPopUp);
      editorSyncPopUp = nullptr;
    }
#endif
    syncUiShown = false;
  }

  if (config_reload::consumeHaSettingsDirty())
    HaRuntime::reloadSettingsFromDisk();
  if (config_reload::consumeDeviceSettingsSchemaDirty())
    device_settings_schema::loadFromLittleFS(true);
  if (config_reload::consumeDeviceSettingsDirty()) {
    if (device_settings::loadFromLittleFS(true))
      device_settings::applyToHardware();
#ifndef IS_SIMULATOR
    device_settings::notifyActivity();
    if (auto disp = std::static_pointer_cast<Display>(HardwareFactory::getAbstract().display()))
      disp->wake();
#endif
  }

#ifdef IS_SIMULATOR
  if (config_reload::consumePagesDirty())
    pagesReloadPending = true;
#endif

  UiOverlayGate::setActive(Screen::Manager::getInstance().screenStackDepth() > 1);
  HaRuntime::tick();
  UIBase::loopHandler();
  UiOverlayGate::setActive(Screen::Manager::getInstance().screenStackDepth() > 1);

#ifdef IS_SIMULATOR
  // HW deploy uses reboot to pick up scene/page JSON; in-process reload can break touch.
  if (!editor_sync_mode::isActive() && pagesReloadPending && mJsonHomeScreen) {
    pagesReloadPending = false;
    Screen::JsonHomeScreen *home = mJsonHomeScreen;
    LvglResourceManager::GetInstance().QueueForLater([home]() {
      home->reloadCurrentSceneFromDisk();
    });
  }
#endif
}

void JsonUI::InitHomeScreen() {
  auto homeScreen = std::make_unique<Screen::JsonHomeScreen>(mDeviceFactory);
  mJsonHomeScreen = homeScreen.get();
  gOverlayHomeScreen = mJsonHomeScreen;
  UiOverlayGate::setHandler(onUiOverlayGate);
  Screen::Manager::getInstance().pushScreen(std::move(homeScreen));
};

void JsonUI::AddPageToHomeScreen(Page::Base::Ptr aPageToAdd) {
  mJsonHomeScreen->AddPage(std::move(aPageToAdd));
};

bool JsonUI::GoToPage(ID anId) {
  return false;
};
