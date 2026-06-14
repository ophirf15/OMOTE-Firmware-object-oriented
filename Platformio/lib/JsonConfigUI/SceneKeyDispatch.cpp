#include "SceneKeyDispatch.hpp"

namespace SceneKeyDispatch {

namespace {
OpenSceneFn sOpenScene = nullptr;
SwitchTabFn sSwitchTab = nullptr;
} // namespace

void registerHandlers(OpenSceneFn openScene, SwitchTabFn switchTab) {
  sOpenScene = openScene;
  sSwitchTab = switchTab;
}

bool openScene(const std::string &sceneFile, uint16_t tabIndex) {
  if (!sOpenScene || sceneFile.empty())
    return false;
  sOpenScene(sceneFile, tabIndex);
  return true;
}

bool switchTab(uint16_t tabIndex) {
  if (!sSwitchTab)
    return false;
  sSwitchTab(tabIndex);
  return true;
}

} // namespace SceneKeyDispatch
