#include "BridgeSyncPage.hpp"

#include "Button.hpp"
#include "Label.hpp"
#include "bridge_client.hpp"
#include "omote_link.hpp"

#include <cstdio>

using namespace UI::Page;

BridgeSyncPage::BridgeSyncPage()
    : Base(ID::Pages::BridgeSyncPage),
      mTitle(AddNewElement<Widget::Label>("Bridge sync")),
      mBody(AddNewElement<Widget::Label>("")),
      mStatus(AddNewElement<Widget::Label>("")),
      mPullButton(AddNewElement<Widget::Button>([this] { pullFromBridge(); })),
      mPushButton(AddNewElement<Widget::Button>([this] { pushToBridge(); })),
      mForgetButton(AddNewElement<Widget::Button>([this] { forgetBridge(); })) {

  SetBgColor(Color::BLACK);
  mTitle->SetHeight(lv_pct(12));
  mTitle->AlignTo(this, LV_ALIGN_TOP_MID, 0, 8);

  mBody->SetHeight(LV_SIZE_CONTENT);
  mBody->SetWidth(lv_pct(92));
  mBody->SetText(
      "The bridge holds your config (scenes, device settings, HA).\n\n"
      "Saving in the PC editor writes to the bridge. The remote keeps a "
      "backup copy and can pull from the bridge, or push its backup back "
      "after a bridge reflash.\n\n"
      "After swapping bridge hardware, use Forget bridge link so the remote "
      "can pair with the new board.");
  mBody->AlignTo(mTitle, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  mStatus->SetHeight(LV_SIZE_CONTENT);
  mStatus->SetWidth(lv_pct(92));
  mStatus->AlignTo(mBody, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

  mPullButton->SetText("Pull from bridge");
  mPullButton->SetHeight(lv_pct(10));
  mPullButton->SetWidth(lv_pct(80));
  mPullButton->AlignTo(mStatus, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

  mPushButton->SetText("Push to bridge");
  mPushButton->SetHeight(lv_pct(10));
  mPushButton->SetWidth(lv_pct(80));
  mPushButton->AlignTo(mPullButton, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  mForgetButton->SetText("Forget bridge link");
  mForgetButton->SetHeight(lv_pct(10));
  mForgetButton->SetWidth(lv_pct(80));
  mForgetButton->AlignTo(mPushButton, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  refreshStatus();
}

void BridgeSyncPage::refreshStatus() {
  uint8_t peer[6] = {};
  char peerLine[64] = "";
  if (omote_link::peerMac(peer)) {
    snprintf(peerLine, sizeof(peerLine),
             "\nPaired bridge: %02X:%02X:%02X:%02X:%02X:%02X",
             peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
  }
  if (omote_link::state() != omote_link::LinkState::Linked) {
    if (peerLine[0])
      mStatus->SetText(std::string("Status: searching for bridge…") + peerLine);
    else
      mStatus->SetText("Status: bridge not linked (check WiFi / ESP-NOW).");
    return;
  }
  if (bridge_client::syncInProgress()) {
    mStatus->SetText(std::string("Status: syncing with bridge…") + peerLine);
    return;
  }
  if (bridge_client::configSynced()) {
    mStatus->SetText(std::string("Status: config synced with bridge.") + peerLine);
    return;
  }
  mStatus->SetText(std::string("Status: linked — waiting for first sync.") + peerLine);
}

void BridgeSyncPage::pullFromBridge() {
  bridge_client::requestConfigPull();
  mStatus->SetText("Status: pull started…");
}

void BridgeSyncPage::pushToBridge() {
  bridge_client::requestPushToBridge();
  mStatus->SetText("Status: push started…");
}

void BridgeSyncPage::forgetBridge() {
  bridge_client::forgetBridgeLink();
  mStatus->SetText("Status: bridge forgotten — searching for new bridge…");
}

bool BridgeSyncPage::OnKeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) {
  return false;
}
