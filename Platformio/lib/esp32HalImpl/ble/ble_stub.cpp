#ifndef IS_SIMULATOR

#include "ble_handler.hpp"

#include <memory>

#if !OMOTE_BLE

std::shared_ptr<BleHandlerInterface> createBleHandler() { return nullptr; }

#endif // !OMOTE_BLE

#endif // IS_SIMULATOR
