#pragma once

#include "DropDown.hpp"
#include "Hardware/BleHandlerInterface.h"
#include "PageBase.hpp"

namespace UI::Widget {
class Label;
class Button;
} // namespace UI::Widget

namespace UI::Page {

class BleSettings : public Base {
public:
  BleSettings();
  ~BleSettings();

  std::string GetTitle() override { return "Bluetooth"; }

protected:
  void OnShow() override;
  void OnHide() override;

private:
  void refreshStatus();
  bool ensureBleReady();
  void applyProfile(const std::string &profileKey);

  std::shared_ptr<BleHandlerInterface> mBle;
  Widget::Label *mStatusLabel = nullptr;
  Widget::Label *mHintLabel = nullptr;
  Widget::DropDown<std::string> *mProfileDropDown = nullptr;
  lv_timer_t *mRefreshTimer = nullptr;
};

} // namespace UI::Page
