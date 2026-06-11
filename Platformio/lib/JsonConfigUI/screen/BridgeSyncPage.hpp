#pragma once

#include "PageBase.hpp"

namespace UI::Widget {
class Button;
class Label;
}

namespace UI::Page {

class BridgeSyncPage : public Base {
public:
  BridgeSyncPage();
  bool OnKeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) override;
  std::string GetTitle() override { return "Bridge sync"; }

private:
  void refreshStatus();
  void pullFromBridge();
  void pushToBridge();
  void forgetBridge();

  Widget::Label *mTitle;
  Widget::Label *mBody;
  Widget::Label *mStatus;
  Widget::Button *mPullButton;
  Widget::Button *mPushButton;
  Widget::Button *mForgetButton;
};

} // namespace UI::Page
