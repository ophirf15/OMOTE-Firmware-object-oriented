#pragma once
#include <cstdint>
#include <string>

namespace SceneKeyDispatch {

using OpenSceneFn = void (*)(const std::string &sceneFile, uint16_t tabIndex);
using SwitchTabFn = void (*)(uint16_t tabIndex);

void registerHandlers(OpenSceneFn openScene, SwitchTabFn switchTab);
bool openScene(const std::string &sceneFile, uint16_t tabIndex);
bool switchTab(uint16_t tabIndex);

} // namespace SceneKeyDispatch
