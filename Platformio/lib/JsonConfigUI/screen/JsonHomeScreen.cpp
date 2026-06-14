#include "JsonHomeScreen.hpp"

#include "ActionTester.hpp"
#include "AddDevice.hpp"
#include "HaRuntime.hpp"
#include "HardwareFactory.hpp"
#include "UiOverlayGate.hpp"
#include "JsonPage.hpp"
#include "RapidJsonUtilty.hpp"
#include "ScreenManager.hpp"
#include "LvglResourceManager.hpp"
#include "SettingsPage.hpp"
#include "ble_scene.hpp"
#include "editor_sync_mode.hpp"
#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
#include "BridgeSyncPage.hpp"
#endif
#ifndef IS_SIMULATOR
#include <Arduino.h>
#include <LittleFS.h>
#include "display.hpp"
#include "device_settings.hpp"
#include "device_settings_schema.hpp"
#endif
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

using namespace UI::Screen;

#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
#include "bridge_client.hpp"
#endif

namespace {
JsonHomeScreen *sOverlayHome = nullptr;

void prepareHomeOverlayRam() {
  if (sOverlayHome)
    sOverlayHome->prepareForOverlay();
}

#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
JsonHomeScreen *sHomeForBridge = nullptr;
#endif
} // namespace

#define RTC_SIG 0x128934ab56cd78ef
#define RTC_STR_SIZE 100

#ifdef OMOTE_SIM
static struct {
  uint64_t signature;
  char currentScene[RTC_STR_SIZE];
  uint16_t tabIdx;
} RtcLastState; // = {RTC_SIG, "Scenes/Scene_Audio.json", 1};
#else
RTC_DATA_ATTR static struct {
  uint64_t signature;
  char currentScene[RTC_STR_SIZE];
  uint16_t tabIdx;
} RtcLastState;
#endif

// NOTE : entry and exit sequence code is a little messy, this is because we have scenes
// that have them and scenes which don't (which therefore aren't really scenes).  Should really
// restructure to better handle this, and also give better status bar info, but this will do for now

JsonHomeScreen::JsonHomeScreen(DeviceFactory &aFactory)
    : Base(UI::ID::Screens::Home),
      mFactory(aFactory),
      mStatusBar(AddNewElement<Widget::StatusBar>(mFactory)),
      mList(AddNewElement<Widget::List>()),
      mTabView(AddNewElement<Page::TabView>(ID(ID::Pages::INVALID_PAGE_ID))) {

  UI::Screen::Manager::getInstance().setAllScreenProcessKeys(true);

  SetBgColor(UI::Color::BLACK);
  // Bring up immediately as main screen, backlight fade in provides equivalent to fade animation
  // otherwise combined fade in and backlight fade can cause odd effect at some brightensses
  SetPushAnimation(LV_SCR_LOAD_ANIM_NONE);
  // Init Factory to allow building of Json devices
  aFactory.InitJsonFactory();
  HaRuntime::init();
  sOverlayHome = this;
#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  sHomeForBridge = this;
#endif
  UiOverlayGate::setPrepareHandler(prepareHomeOverlayRam);
  std::fprintf(stderr, "[JsonHomeScreen] HaRuntime init done\n");
  std::fflush(stderr);

  mSceneChangeHandler.SetNotification(mStatusBar->GetSceneChangeNotification());
  mSceneChangeHandler = [this](std::string aNewScene) { GoToSceneSelection(aNewScene); };

  mStatusBar->SetTopButtonLabel("Select Scene");

#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  mStatusBar->AddExtraSettingItem({"Bridge sync", LV_SYMBOL_REFRESH, [] {
    return UI::Page::Base::Ptr(std::make_unique<Page::BridgeSyncPage>());
  }});
#else
  mStatusBar->AddExtraSettingItem({"Editor sync", LV_SYMBOL_REFRESH, [] {
    editor_sync_mode::enter(true);
    return UI::Page::Base::Ptr{};
  }});
#endif

  static constexpr auto ContentHeight =
      SCREEN_HEIGHT - Widget::StatusBar::Height;
  mList->SetHeight(ContentHeight);
  mList->AlignTo(mStatusBar, LV_ALIGN_OUT_BOTTOM_MID);
  mTabView->SetHeight(ContentHeight);
  mTabView->AlignTo(mStatusBar, LV_ALIGN_OUT_BOTTOM_MID);
  mTabView->SetVisiblity(false);

#ifndef IS_SIMULATOR
#if OMOTE_BRIDGE_CLIENT
  if (bridge_client::configSynced()) {
    populateSceneListFromDisk();
    Serial.println("[JsonHomeScreen] bridge client — using local config");
  } else {
    RtcLastState.signature = 0;
    mStatusBar->SetTopButtonLabel("Syncing config…");
    Serial.println("[JsonHomeScreen] bridge client — waiting for config sync");
  }
#else
  populateSceneListFromDisk();
  if (RtcLastState.signature == RTC_SIG) {
    const std::string restoredScene = RtcLastState.currentScene;
    LvglResourceManager::GetInstance().QueueForLater([this, restoredScene]() {
      displayScenePage(restoredScene, true);
    });
  }
#endif
#else
  RtcLastState.signature = 0;
  populateSceneListFromDisk();
#endif

  mStatusBar->AddDebugSettingItem({"Test Actions", LV_SYMBOL_LIST, [this] {
                                     return std::make_unique<UI::Page::ActionTester>();
                                   }});

  mStatusBar->AddDebugSettingItem({"Add Json Device", LV_SYMBOL_EDIT, [this] {
                                     auto jsonDevices = mFactory.getJsonDevices();
                                     return std::make_unique<UI::Page::AddDevice>(mFactory.getActiveDevices(), jsonDevices);
                                   }});
  mStatusBar->AddDebugSettingItem({"Add Compile Time Device", LV_SYMBOL_EDIT, [this] {
                                     auto compiledDevices = mFactory.getCompileTimeDevices();
                                     return std::make_unique<UI::Page::AddDevice>(mFactory.getActiveDevices(), compiledDevices);
                                   }});

  std::fprintf(stderr, "[JsonHomeScreen] home screen ready\n");
  std::fflush(stderr);
}

bool JsonHomeScreen::checkSceneForEntryExit(const std::string &aFileName) {
  std::filesystem::path filePath(FS_PATH + aFileName);
  rapidjson::Document d = OMOTE::JSON::GetDocument(filePath);

  if (d.HasParseError() || d.IsNull())
    return false;

  return ((d.HasMember("StartCommandSequence") && d["StartCommandSequence"].IsArray()) ||
          (d.HasMember("ExitCommandSequence") && d["ExitCommandSequence"].IsArray()));
}

void JsonHomeScreen::bindTabChangeHandler() {
  mTabView->OnTabChangeEvent([this](uint16_t idx) {
    RtcLastState.tabIdx = idx;
    LvglResourceManager::GetInstance().QueueForLater([this, idx]() {
      unloadInactiveTabs(idx);
      ensureTabLoaded(idx);
    });
  });
}

void JsonHomeScreen::clearScene() {
#ifndef IS_SIMULATOR
  ble_scene::disarmSceneBle();
#endif
  mOverlaySuspendedTabIdx = -1;
  Command::Commands::releaseCachedDocuments();
  mSceneTabSpecs.clear();
  mTabLoaded.clear();
  RemoveElement(mTabView);
  mTabView = AddNewElement<Page::TabView>(ID(ID::Pages::INVALID_PAGE_ID));
  mTabView->SetHeight(SCREEN_HEIGHT - Widget::StatusBar::Height);
  mTabView->AlignTo(mStatusBar, LV_ALIGN_OUT_BOTTOM_MID);
  mTabView->SetVisiblity(false);
  bindTabChangeHandler();
  mOverrideKeyHandlers.clear();
}

UI::Page::Base::Ptr JsonHomeScreen::buildTabPage(const SceneTabSpec &spec) {
  auto page = std::make_unique<Page::JsonPage>(spec.fileName, spec.pageName, spec.commandPrefix);
  std::string title = spec.shortName;
  if (title.empty())
    title = spec.pageName;
  if (title.empty())
    title = spec.fileName;
  page->SetTitle(title);
  return page;
}

void JsonHomeScreen::unregisterTabOverrides(uint16_t tabIdx) {
  if (tabIdx >= mSceneTabSpecs.size())
    return;
  auto &spec = mSceneTabSpecs[tabIdx];
  for (const auto &cached : spec.cachedOverrideHandlers) {
    auto range = mOverrideKeyHandlers.equal_range(cached.first);
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second.pressType == cached.second.pressType && it->second.command.mode == cached.second.command.mode &&
          it->second.command.protocol == cached.second.command.protocol &&
          it->second.command.data == cached.second.command.data) {
        mOverrideKeyHandlers.erase(it);
        break;
      }
    }
  }
  spec.cachedOverrideHandlers.clear();
}

void JsonHomeScreen::applyTabOverrides(uint16_t tabIdx, Page::JsonPage &page) {
  if (tabIdx >= mSceneTabSpecs.size())
    return;
  auto &spec = mSceneTabSpecs[tabIdx];
  if (spec.overrideKeyNames.empty())
    return;
  unregisterTabOverrides(tabIdx);
  std::multimap<Command::KeyIds, Command::KeyStruct> keyHandlers;
  page.getKeyOverrides(spec.overrideKeyNames, keyHandlers);
  for (const auto &entry : keyHandlers)
    spec.cachedOverrideHandlers.push_back(entry);
  mOverrideKeyHandlers.insert(keyHandlers.begin(), keyHandlers.end());
}

void JsonHomeScreen::ensureTabLoaded(uint16_t tabIdx) {
  if (mOverlaySuspendedTabIdx >= 0)
    return;
  if (tabIdx >= mSceneTabSpecs.size() || tabIdx >= mTabLoaded.size() || mTabLoaded[tabIdx])
    return;
#ifndef IS_SIMULATOR
  Serial.printf("[Scene] load tab %u heap=%u\n", static_cast<unsigned>(tabIdx), ESP.getFreeHeap());
#endif
  auto page = buildTabPage(mSceneTabSpecs[tabIdx]);
  auto *jsonPage = static_cast<Page::JsonPage *>(page.get());
  if (!jsonPage->isLoaded()) {
#ifndef IS_SIMULATOR
    Serial.printf("[Scene] tab %u page not loaded heap=%u\n", static_cast<unsigned>(tabIdx),
                  ESP.getFreeHeap());
#endif
    return;
  }
  applyTabOverrides(tabIdx, *jsonPage);
  mTabView->LoadTabContent(tabIdx, std::move(page));
  mTabLoaded[tabIdx] = true;
}

void JsonHomeScreen::unloadInactiveTabs(uint16_t activeIdx) {
  for (uint16_t i = 0; i < mTabLoaded.size(); i++) {
    if (i == activeIdx || !mTabLoaded[i])
      continue;
    unregisterTabOverrides(i);
    mTabView->UnloadTabContent(i);
    mTabLoaded[i] = false;
#ifndef IS_SIMULATOR
    Serial.printf("[Scene] unload tab %u heap=%u\n", static_cast<unsigned>(i), ESP.getFreeHeap());
#endif
  }
}

void JsonHomeScreen::prepareForOverlay() {
  suspendSceneTabForOverlay();
}

void JsonHomeScreen::suspendSceneTabForOverlay() {
  if (mOverlaySuspendedTabIdx >= 0)
    return;
  if (!mTabView || !mTabView->IsSetVisible() || mSceneTabSpecs.empty())
    return;
  mTabView->OnHide();
  const uint16_t idx = mTabView->GetCurrentTabIdx();
  if (idx >= mTabLoaded.size() || !mTabLoaded[idx])
    return;
  unregisterTabOverrides(idx);
  mTabView->UnloadTabContent(idx);
  mTabLoaded[idx] = false;
  mOverlaySuspendedTabIdx = static_cast<int16_t>(idx);
  Command::Commands::releaseCachedDocuments();
#ifndef IS_SIMULATOR
  Serial.printf("[Scene] suspend tab %u for overlay heap=%u\n", static_cast<unsigned>(idx), ESP.getFreeHeap());
#endif
}

void JsonHomeScreen::resumeSceneTabAfterOverlay() {
  if (mOverlaySuspendedTabIdx < 0 || !mTabView || !mTabView->IsSetVisible())
    return;
  const uint16_t idx = static_cast<uint16_t>(mOverlaySuspendedTabIdx);
  mOverlaySuspendedTabIdx = -1;
  LvglResourceManager::GetInstance().QueueForLater([this, idx]() {
    ensureTabLoaded(idx);
    mTabView->OnShow();
#ifndef IS_SIMULATOR
    Serial.printf("[Scene] resume tab %u after overlay heap=%u\n", static_cast<unsigned>(idx), ESP.getFreeHeap());
#endif
  });
}

void JsonHomeScreen::setupLazySceneTabs(std::vector<SceneTabSpec> specs, SceneFinishParams finish) {
  mSceneTabSpecs = std::move(specs);
  mTabLoaded.assign(mSceneTabSpecs.size(), false);
  for (const auto &spec : mSceneTabSpecs) {
    std::string title = spec.shortName;
    if (title.empty())
      title = spec.pageName;
    if (title.empty())
      title = spec.fileName;
    mTabView->AddPlaceholderTab(title);
  }
  finalizeSceneDisplay(finish);
}

void JsonHomeScreen::finalizeSceneDisplay(const SceneFinishParams &params) {
  mLastScene = params.fileName;

  if (params.restoreScene) {
    mTabView->SetCurrentTabIdx(params.tabIdx, LV_ANIM_OFF);
    HardwareFactory::getAbstract().setInScene(true);
  } else {
    RtcLastState.signature = RTC_SIG;
    strncpy(RtcLastState.currentScene, params.fileName.c_str(), RTC_STR_SIZE);
    RtcLastState.tabIdx = params.tabIdx;
    HardwareFactory::getAbstract().setInScene(true);
  }

  unloadInactiveTabs(params.tabIdx);
  ensureTabLoaded(params.tabIdx);

  if (params.showScene) {
    mTabView->SetVisiblity(true);
    mList->SetVisiblity(false);
    lv_obj_fade_in(mTabView->LvglSelf(), 400, 0);
    mTabView->OnShow();
  } else {
    mTabView->SetVisiblity(false);
    mList->SetVisiblity(true);
  }
#ifndef IS_SIMULATOR
  ble_scene::armSceneBle(params.bleEnabled);
  Serial.printf("[Scene] ready %s tabs=%u loaded=%u heap=%u ble=%d\n", params.fileName.c_str(),
                static_cast<unsigned>(mTabView->TabCount()),
                static_cast<unsigned>(mTabView->HasTabContent(params.tabIdx) ? 1 : 0),
                ESP.getFreeHeap(), params.bleEnabled ? 1 : 0);
#endif
}

namespace {

bool jsonHasObjectMember(const rapidjson::Value &value, const char *member) {
  return value.IsObject() && value.HasMember(member);
}

std::string sceneDisplayNameFromPath(const std::string &fileName) {
  std::string name = fileName;
  const std::string prefix = "Scenes/Scene_";
  if (name.rfind(prefix, 0) == 0)
    name = name.substr(prefix.size());
  if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
    name = name.substr(0, name.size() - 5);
  for (char &c : name) {
    if (c == '_')
      c = ' ';
  }
  return name;
}

bool isSceneListedInRegistry(const std::string &fileName) {
  rapidjson::Document d = OMOTE::JSON::GetDocument(std::filesystem::path(FS_PATH "Scenes.json"));
  if (d.HasParseError() || !jsonHasObjectMember(d, "Scenes") || !d["Scenes"].IsArray())
    return false;
  for (rapidjson::SizeType i = 0; i < d["Scenes"].Size(); i++) {
    const auto &scene = d["Scenes"][i];
    if (!scene.IsObject() || !jsonHasObjectMember(scene, "FileName") || !scene["FileName"].IsString())
      continue;
    if (fileName == scene["FileName"].GetString())
      return true;
  }
  return false;
}

bool sceneFileOnDisk(const std::string &fileName) {
#if !defined(IS_SIMULATOR)
  const String path = String(FS_PATH) + fileName.c_str();
  return LittleFS.exists(path.c_str());
#else
  return std::filesystem::exists(std::filesystem::path(FS_PATH + fileName));
#endif
}

std::vector<std::string> sBridgeManifestCache;

bool isSceneListedInManifest(const std::string &fileName) {
#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  if (!bridge_client::configSynced())
    return false;
#endif
  if (!sceneFileOnDisk(fileName))
    return false;
#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  if (isSceneListedInRegistry(fileName))
    return true;
  for (const auto &path : sBridgeManifestCache) {
    if (path == fileName)
      return true;
  }
  return false;
#else
  return isSceneListedInRegistry(fileName);
#endif
}

} // namespace

void JsonHomeScreen::displayScenePage(const std::string &aFileName, bool restoreScene, bool showScene) {
#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  if (!bridge_client::configSynced()) {
    Serial.printf("[Scene] blocked %s — bridge config not synced yet\n", aFileName.c_str());
    return;
  }
  if (!isSceneListedInManifest(aFileName)) {
    Serial.printf("[Scene] blocked %s — not in Scenes.json\n", aFileName.c_str());
    return;
  }
#endif
  std::filesystem::path aFilePath(FS_PATH + aFileName);
  rapidjson::Document d = OMOTE::JSON::GetDocument(aFilePath);

  if (d.HasParseError() || d.IsNull()) {
#ifndef IS_SIMULATOR
    Serial.printf("[Scene] failed to parse %s\n", aFileName.c_str());
#endif
    return;
  }

#ifndef IS_SIMULATOR
  Serial.printf("[Scene] opening %s heap=%u\n", aFileName.c_str(), ESP.getFreeHeap());
#endif

  bool bleEnabled = d.HasMember("BleEnabled") && d["BleEnabled"].IsBool() && d["BleEnabled"].GetBool();

  if (mLastScene == aFileName) {
    mTabView->SetVisiblity(true);
    mList->SetVisiblity(false);
    lv_obj_fade_in(mTabView->LvglSelf(), 400, 0);
#ifndef IS_SIMULATOR
    ble_scene::armSceneBle(bleEnabled);
#endif
    return; // no scene change, just bring existing tabview back up
  }

  const bool newSceneHasEntryExit =
      (d.HasMember("StartCommandSequence") && d["StartCommandSequence"].IsArray()) ||
      (d.HasMember("ExitCommandSequence") && d["ExitCommandSequence"].IsArray());
  if (!mExitCommands.empty() && (mSavedExitSeq != aFileName) && newSceneHasEntryExit) {
    // if we have an exit sequence to use, which isn't for the new scene, and the new scene uses entry or exit sequences then send it
    sendExitSequence();
    // Serial.println("Sending Exit commands");
  }

  clearScene();

  if (d.HasMember("ScreenName") && d["ScreenName"].IsString()) {
    std::string string = d["ScreenName"].GetString();
    string.insert(0, "Scene:");
    mStatusBar->SetTopButtonLabel(string);
  } else
    mStatusBar->SetTopButtonLabel(aFileName);

  // Run command sequences before building LVGL pages — parsing command JSON while
  // several JsonPages are alive can exhaust heap and crash in RapidJSON.
  if ((mLastStartSeq != aFileName) && !restoreScene && d.HasMember("StartCommandSequence") &&
      d["StartCommandSequence"].IsArray()) {
    for (rapidjson::SizeType i = 0; i < d["StartCommandSequence"].Size(); i++) {
      if (d["StartCommandSequence"][i].HasMember("CommandFile") &&
          d["StartCommandSequence"][i]["CommandFile"].IsString()) {
        if (d["StartCommandSequence"][i].HasMember("Command") &&
            d["StartCommandSequence"][i]["Command"].IsString()) {
          Command::CommandStruct aCommand;
          if (Command::NONE != Command::Commands::getCommand(d["StartCommandSequence"][i]["CommandFile"].GetString(),
                                                             "",
                                                             d["StartCommandSequence"][i]["Command"].GetString(),
                                                             aCommand)) {
            Command::Commands::sendCommand(aCommand);
            mLastStartSeq = aFileName;
          }
        }
      }
    }
  }

  if (mExitCommands.empty() && d.HasMember("ExitCommandSequence") && d["ExitCommandSequence"].IsArray()) {
    for (rapidjson::SizeType i = 0; i < d["ExitCommandSequence"].Size(); i++) {
      if (d["ExitCommandSequence"][i].HasMember("CommandFile") &&
          d["ExitCommandSequence"][i]["CommandFile"].IsString()) {
        if (d["ExitCommandSequence"][i].HasMember("Command") &&
            d["ExitCommandSequence"][i]["Command"].IsString()) {
          Command::CommandStruct aCommand;
          if (Command::NONE != Command::Commands::getCommand(d["ExitCommandSequence"][i]["CommandFile"].GetString(),
                                                             "",
                                                             d["ExitCommandSequence"][i]["Command"].GetString(),
                                                             aCommand)) {
            mExitCommands.push_back(aCommand);
            mSavedExitSeq = aFileName;
          }
        }
      }
    }
  }

  std::vector<SceneTabSpec> tabSpecs;
  if (d.HasMember("Pages") && d["Pages"].IsArray()) {
    for (rapidjson::SizeType i = 0; i < d["Pages"].Size(); i++) {
      if (!d["Pages"][i].HasMember("FileName") || !d["Pages"][i]["FileName"].IsString())
        continue;
      SceneTabSpec spec;
      spec.fileName = d["Pages"][i]["FileName"].GetString();
      if (d["Pages"][i].HasMember("PageName") && d["Pages"][i]["PageName"].IsString())
        spec.pageName = d["Pages"][i]["PageName"].GetString();
      else
        spec.pageName = spec.fileName;
      if (d["Pages"][i].HasMember("ShortName") && d["Pages"][i]["ShortName"].IsString())
        spec.shortName = d["Pages"][i]["ShortName"].GetString();
      if (d["Pages"][i].HasMember("CommandPrefix") && d["Pages"][i]["CommandPrefix"].IsString())
        spec.commandPrefix = d["Pages"][i]["CommandPrefix"].GetString();
      if (d["Pages"][i].HasMember("OverrideKeys") && d["Pages"][i]["OverrideKeys"].IsArray()) {
        const auto array = d["Pages"][i]["OverrideKeys"].GetArray();
        for (rapidjson::SizeType j = 0; j < array.Size(); j++) {
          if (array[j].IsString())
            spec.overrideKeyNames.emplace_back(array[j].GetString());
        }
      }
      tabSpecs.push_back(std::move(spec));
    }
  }

  SceneFinishParams finish;
  finish.fileName = aFileName;
  finish.restoreScene = restoreScene;
  finish.tabIdx = restoreScene ? RtcLastState.tabIdx : 0;
  finish.showScene = showScene;
  finish.bleEnabled = bleEnabled;
  LvglResourceManager::GetInstance().QueueForLater([this, tabSpecs = std::move(tabSpecs),
                                                    finish = std::move(finish)]() mutable {
    setupLazySceneTabs(std::move(tabSpecs), std::move(finish));
  });
}

void JsonHomeScreen::AddPage(Page::Base::Ptr aPage) {
  // mTabView->AddTab(std::move(aPage));
}

void JsonHomeScreen::SetBgColor(lv_color_t value, lv_part_t selector) {
  // mTabView->SetBgColor(value, selector);
  UI::UIElement::SetBgColor(value, selector);
}

bool JsonHomeScreen::OnKeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) {
  // handle exit sequence first
  if ((aKeyEvent.mId == KeyPressAbstract::KeyId::Power)) {
    if (aKeyEvent.mType == Command::KeyPressTypes::Press) {
      return true; // prevent page from responding to press event
    } else if (aKeyEvent.mType == Command::KeyPressTypes::Long) {
      // long press - send exit sequence and return to home screen
      sendExitSequence();
      clearScene();
      mLastScene.clear();
      mLastStartSeq.clear();
      RtcLastState.signature = 0; // invalidate non volatile RAM sig to prevent restoring  closed scene on power up
      HardwareFactory::getAbstract().setInScene(false);
      mStatusBar->SetTopButtonLabel("Select Scene");
      GoToSceneSelection("");
      return true;
    } else if (aKeyEvent.mType == Command::KeyPressTypes::Short) {
      // return to home screen without sending sequence or cancelling last scene
      GoToSceneSelection("");
      return true;
    }
  }
  // scene slection keys first
#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  if (!bridge_client::configSynced())
    return false;
#endif
  {
    auto range = mSceneKeyHandlers.equal_range(aKeyEvent.mId);
    if (range.first != mSceneKeyHandlers.end()) {
      for (auto i = range.first; i != range.second; ++i) {
        if (i->second.pressType == aKeyEvent.mType) {
          LvglResourceManager::GetInstance().QueueForLater([this, file = i->second.ScreenFileName]() {
            displayScenePage(file, false);
          });
          return true;
        }
      }
    }
  }

  // then override key handlers
  {
    auto range = mOverrideKeyHandlers.equal_range(aKeyEvent.mId);
    if (range.first != mOverrideKeyHandlers.end()) {
      for (auto i = range.first; i != range.second; ++i) {
        if (i->second.pressType == aKeyEvent.mType) {
          Command::Commands::sendCommand(i->second.command);
          return true;
        }
      }
    }
  }

  return false;
};

void JsonHomeScreen::sendExitSequence() {
  for (auto &i : mExitCommands)
    Command::Commands::sendCommand(i);
  mExitCommands.clear();
  mSavedExitSeq.clear();
}

void JsonHomeScreen::populateSceneListFromDisk() {
  mList->ClearItems();
  mSceneKeyHandlers.clear();

  std::set<std::string> added;

  auto addSceneItem = [&](const std::string &fileName, const std::string &sceneName,
                          const rapidjson::Value *registryEntry) {
    if (!added.insert(fileName).second)
      return;
    const auto symbol = checkSceneForEntryExit(fileName) ? LV_SYMBOL_WIFI : LV_SYMBOL_MINUS;
    mList->AddItem(sceneName, symbol, [this, fileName] {
      LvglResourceManager::GetInstance().QueueForLater([this, fileName]() {
        displayScenePage(fileName, false);
      });
    });
    if (registryEntry && registryEntry->IsObject() && jsonHasObjectMember(*registryEntry, "BindToKey") &&
        jsonHasObjectMember(*registryEntry, "PressType") && (*registryEntry)["BindToKey"].IsString() &&
        (*registryEntry)["PressType"].IsString()) {
      const auto id = magic_enum::enum_cast<KeyIds>((*registryEntry)["BindToKey"].GetString());
      const auto type = magic_enum::enum_cast<KeyPressTypes>((*registryEntry)["PressType"].GetString());
      if (id.has_value() && type.has_value())
        mSceneKeyHandlers.insert({id.value(), {type.value(), fileName}});
    }
  };

  rapidjson::Document registry = OMOTE::JSON::GetDocument(std::filesystem::path(FS_PATH "Scenes.json"));

  auto findRegistryEntry = [&registry](const std::string &fileName) -> const rapidjson::Value * {
    if (registry.HasParseError() || !jsonHasObjectMember(registry, "Scenes") || !registry["Scenes"].IsArray())
      return nullptr;
    for (rapidjson::SizeType i = 0; i < registry["Scenes"].Size(); i++) {
      const auto &scene = registry["Scenes"][i];
      if (!scene.IsObject() || !jsonHasObjectMember(scene, "FileName") || !scene["FileName"].IsString())
        continue;
      if (fileName == scene["FileName"].GetString())
        return &scene;
    }
    return nullptr;
  };

#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  for (const auto &fileName : sBridgeManifestCache) {
    if (fileName.size() <= 7 || fileName.compare(0, 7, "Scenes/") != 0 ||
        fileName.size() <= 5 || fileName.compare(fileName.size() - 5, 5, ".json") != 0)
      continue;
    if (!sceneFileOnDisk(fileName)) {
      Serial.printf("[JsonHomeScreen] skip missing scene file %s\n", fileName.c_str());
      continue;
    }
    std::string sceneName = sceneDisplayNameFromPath(fileName);
    const rapidjson::Value *reg = findRegistryEntry(fileName);
    if (reg && reg->IsObject() && jsonHasObjectMember(*reg, "SceneName") && (*reg)["SceneName"].IsString())
      sceneName = (*reg)["SceneName"].GetString();
    else {
      rapidjson::Document sceneDoc =
          OMOTE::JSON::GetDocument(std::filesystem::path(FS_PATH + fileName));
      if (!sceneDoc.HasParseError() && jsonHasObjectMember(sceneDoc, "ScreenName") &&
          sceneDoc["ScreenName"].IsString())
        sceneName = sceneDoc["ScreenName"].GetString();
    }
    addSceneItem(fileName, sceneName, reg);
  }
#else
  if (!registry.HasParseError() && jsonHasObjectMember(registry, "Scenes") && registry["Scenes"].IsArray()) {
    for (rapidjson::SizeType i = 0; i < registry["Scenes"].Size(); i++) {
      auto &scene = registry["Scenes"][i];
      if (!scene.IsObject() || !jsonHasObjectMember(scene, "FileName") || !scene["FileName"].IsString())
        continue;
      const std::string fileName = scene["FileName"].GetString();
      if (!sceneFileOnDisk(fileName))
        continue;
      std::string sceneName;
      if (jsonHasObjectMember(scene, "SceneName") && scene["SceneName"].IsString())
        sceneName = scene["SceneName"].GetString();
      else
        sceneName = sceneDisplayNameFromPath(fileName);
      addSceneItem(fileName, sceneName, &scene);
    }
  }
#endif
}

void JsonHomeScreen::reloadCurrentSceneFromDisk() {
  auto lock = LvglResourceManager::GetInstance().scopeLock();

  populateSceneListFromDisk();

  if (mLastScene.empty()) {
    std::fprintf(stderr, "[JsonHomeScreen] scene list reloaded\n");
    std::fflush(stderr);
    return;
  }

  const std::string scene = mLastScene;
  if (!std::filesystem::exists(std::filesystem::path(FS_PATH + scene)) ||
      !isSceneListedInManifest(scene)) {
    std::fprintf(stderr, "[JsonHomeScreen] scene removed: %s\n", scene.c_str());
    std::fflush(stderr);
    sendExitSequence();
    clearScene();
    mLastScene.clear();
    mLastStartSeq.clear();
    mTabView->SetVisiblity(false);
    mList->SetVisiblity(true);
    return;
  }
  const uint16_t tabIdx = mTabView->GetCurrentTabIdx();
  const bool wasVisible = mTabView->IsVisible();

  std::fprintf(stderr, "[JsonHomeScreen] reload scene: %s (tab visible=%d)\n", scene.c_str(),
               wasVisible ? 1 : 0);
  std::fflush(stderr);

  mLastScene.clear();
#ifdef IS_SIMULATOR
  try {
    displayScenePage(scene, true);
    if (wasVisible) {
      mTabView->SetCurrentTabIdx(tabIdx, LV_ANIM_OFF);
      mTabView->OnShow();
    } else {
      mTabView->SetVisiblity(false);
      mList->SetVisiblity(true);
    }
  } catch (const std::exception &ex) {
    std::fprintf(stderr, "[JsonHomeScreen] reload failed: %s\n", ex.what());
    std::fflush(stderr);
  } catch (...) {
    std::fprintf(stderr, "[JsonHomeScreen] reload failed: unknown error\n");
    std::fflush(stderr);
  }
#else
  RtcLastState.tabIdx = tabIdx;
  displayScenePage(scene, true, wasVisible);
#endif
#ifndef IS_SIMULATOR
  device_settings::notifyActivity();
  if (auto disp = std::static_pointer_cast<Display>(HardwareFactory::getAbstract().display()))
    disp->wake();
#endif
}

void JsonHomeScreen::GoToSceneSelection(const std::string &aNewScene) {
  if (mTabView->IsSetVisible())
    mTabView->OnHide();
  mTabView->SetVisiblity(false);
  mList->SetVisiblity(true);
  lv_obj_fade_in(mList->LvglSelf(), 400, 0);
}

void JsonHomeScreen::OnHide() {
  UiOverlayGate::setActive(true);
  prepareForOverlay();
  if (mTabView)
    lv_anim_delete(mTabView->LvglSelf(), nullptr);
  if (mList)
    lv_anim_delete(mList->LvglSelf(), nullptr);
  Base::OnHide();
}

void JsonHomeScreen::OnShow() {
  Base::OnShow();
  if (Screen::Manager::getInstance().screenStackDepth() <= 1) {
    UiOverlayGate::setActive(false);
    resumeSceneTabAfterOverlay();
  }
}

void JsonHomeScreen::OnLvglEvent(lv_event_t *aEvent) {
  if (lv_event_get_code(aEvent) == LV_EVENT_VALUE_CHANGED) {
    RtcLastState.tabIdx = mTabView->GetCurrentTabIdx();
    // Serial.printf("Restore tab Idx set to: %d\r\n",RtcLastState.tabIdx);
  }
}

void JsonHomeScreen::onBridgeConfigSynced(const std::vector<std::string> &manifestFiles) {
  sBridgeManifestCache = manifestFiles;
  HaRuntime::reloadSettingsFromDisk();

#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
  if (bridge_client::isSyncedConfigFile("DeviceSettings.schema.json")) {
    if (device_settings_schema::loadFromLittleFS(true))
      Serial.println("[JsonHomeScreen] device settings schema reloaded");
  }
  if (bridge_client::isSyncedConfigFile("DeviceSettings.json")) {
    if (device_settings::isDirty()) {
      Serial.println("[JsonHomeScreen] device settings reload skipped — unsaved edits");
    } else if (device_settings::loadFromLittleFS(true)) {
      device_settings::applyToHardware();
      Serial.println("[JsonHomeScreen] device settings applied");
    }
  }
#endif

  RtcLastState.signature = 0;
  Command::Commands::releaseCachedDocuments();

  if (!mLastScene.empty()) {
    sendExitSequence();
    clearScene();
    mLastScene.clear();
    mLastStartSeq.clear();
  }

  populateSceneListFromDisk();
  mTabView->SetVisiblity(false);
  mList->SetVisiblity(true);
  lv_obj_fade_in(mList->LvglSelf(), 400, 0);
  mStatusBar->SetTopButtonLabel("Select Scene");
  HardwareFactory::getAbstract().setInScene(false);

  unsigned sceneCount = 0;
  for (const auto &fileName : sBridgeManifestCache) {
    if (fileName.size() > 7 && fileName.compare(0, 7, "Scenes/") == 0 && fileName.size() > 5 &&
        fileName.compare(fileName.size() - 5, 5, ".json") == 0)
      sceneCount++;
  }
  unsigned shown = 0;
  Serial.printf("[JsonHomeScreen] bridge config synced — %u scene(s) on picker:\n", sceneCount);
  for (const auto &fileName : sBridgeManifestCache) {
    if (fileName.size() <= 7 || fileName.compare(0, 7, "Scenes/") != 0)
      continue;
    if (!sceneFileOnDisk(fileName))
      continue;
    shown++;
    Serial.printf("[JsonHomeScreen]   %s\n", fileName.c_str());
  }
  if (!shown)
    Serial.println("[JsonHomeScreen] picker empty — check bridge Scenes/ and ESP-NOW sync");
}

#if OMOTE_BRIDGE_CLIENT && !defined(IS_SIMULATOR)
void bridge_client_onConfigSynced(bool chainResync, const std::vector<std::string> &manifest) {
  if (!sHomeForBridge)
    return;
  const std::vector<std::string> manifestSnap = manifest;
  LvglResourceManager::GetInstance().QueueForLater([home = sHomeForBridge, manifestSnap, chainResync]() {
    home->onBridgeConfigSynced(manifestSnap);
    if (chainResync)
      bridge_client::requestQueuedResync();
  });
}
#endif
