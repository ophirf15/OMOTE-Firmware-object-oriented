#pragma once
#include <string>
#include <vector>

#include "Command.hpp"
#include "DeviceFactory.hpp"
#include "HardwareAbstract.hpp"
// #include "JsonTabView.hpp"
#include "List.hpp"
#include "MainTopBar.hpp"
#include "PageBase.hpp"
#include "ScreenBase.hpp"
#include "StatusBar.hpp"
#include "TabView.hpp"

namespace UI::Page {
class JsonPage;
}

namespace UI::Screen {

#define TOP_BAR_HEIGHT 20

using KeyIds = KeyPressAbstract::KeyId;
using KeyPressTypes = KeyPressAbstract::KeyEvent::Type;

struct ScreensStruct {
  KeyPressTypes pressType = KeyPressTypes::INVALID;
  std::string ScreenFileName;
};

class JsonHomeScreen : public Base {
public:
  JsonHomeScreen(DeviceFactory &factory);

  void SetBgColor(lv_color_t value,
                  lv_part_t selector = LV_PART_MAIN) override;

  void AddPage(Page::Base::Ptr aPage);

  bool GoToPage(ID anId) { return false; }; // return mTabView->GoToTab(anId); }

  void displayScenePage(const std::string &aFileName, bool restoreScene, bool showScene = true);

  /** Re-read page JSON from LittleFS for the current scene (after editor deploy). */
  void reloadCurrentSceneFromDisk();

  /** Free scene tab RAM before settings/other overlays allocate UI (call via UiOverlayGate). */
  void prepareForOverlay();

  void OnLvglEvent(lv_event_t *aEvent);

  void OnShow() override;
  void OnHide() override;

protected:
  bool OnKeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) override;

  void GoToSceneSelection(const std::string &aNewScene);

  bool checkSceneForEntryExit(const std::string &aFileName);

  void clearScene();

  struct SceneTabSpec {
    std::string fileName;
    std::string pageName;
    std::string shortName;
    std::string commandPrefix;
    std::vector<std::string> overrideKeyNames;
    std::vector<std::pair<Command::KeyIds, Command::KeyStruct>> cachedOverrideHandlers;
  };

  struct SceneFinishParams {
    std::string fileName;
    bool restoreScene = false;
    bool bleEnabled = false;
    uint16_t tabIdx = 0;
    bool showScene = true;
  };

  void bindTabChangeHandler();
  UI::Page::Base::Ptr buildTabPage(const SceneTabSpec &spec);
  void applyTabOverrides(uint16_t tabIdx, Page::JsonPage &page);
  void unregisterTabOverrides(uint16_t tabIdx);
  void ensureTabLoaded(uint16_t tabIdx);
  void unloadInactiveTabs(uint16_t activeIdx);
  void suspendSceneTabForOverlay();
  void resumeSceneTabAfterOverlay();
  bool activeSceneBleEnabled() const;
  void setupLazySceneTabs(std::vector<SceneTabSpec> specs, SceneFinishParams finish);
  void finalizeSceneDisplay(const SceneFinishParams &params);

  void sendExitSequence();

  /** Rebuild scene picker and key bindings from Scenes.json. */
  void populateSceneListFromDisk();

private:
  DeviceFactory &mFactory;

  Handler<std::string> mSceneChangeHandler;

  Widget::StatusBar *mStatusBar;
  Widget::List *mList;
  Page::TabView *mTabView;
  std::vector<Command::CommandStruct> mExitCommands;
  std::string mLastScene;
  std::string mLastStartSeq;
  std::string mSavedExitSeq;
  std::multimap<KeyIds, ScreensStruct> mSceneKeyHandlers;
  std::multimap<Command::KeyIds, Command::KeyStruct> mOverrideKeyHandlers;
  std::vector<SceneTabSpec> mSceneTabSpecs;
  std::vector<bool> mTabLoaded;
  int16_t mOverlaySuspendedTabIdx = -1;
  std::string mScreenToLoad;
};

} // namespace UI::Screen