#pragma once

namespace UiOverlayGate {

using Handler = void (*)(bool active);
using PrepareHandler = void (*)();

void setHandler(Handler handler);
void setPrepareHandler(PrepareHandler handler);
void prepareRam();
void setActive(bool active);
bool isActive();

} // namespace UiOverlayGate
