#include "UiOverlayGate.hpp"

namespace UiOverlayGate {

namespace {
Handler gHandler = nullptr;
PrepareHandler gPrepareHandler = nullptr;
bool gActive = false;
} // namespace

void setHandler(Handler handler) { gHandler = handler; }

void setPrepareHandler(PrepareHandler handler) { gPrepareHandler = handler; }

void prepareRam() {
  if (gPrepareHandler)
    gPrepareHandler();
}

void setActive(bool active) {
  if (gActive == active)
    return;
  gActive = active;
  if (gHandler)
    gHandler(active);
}

bool isActive() { return gActive; }

} // namespace UiOverlayGate
