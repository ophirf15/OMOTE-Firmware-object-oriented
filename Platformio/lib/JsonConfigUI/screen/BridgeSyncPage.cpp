#include "BridgeSyncPage.hpp"

#include "Button.hpp"
#include "Label.hpp"
#include "bridge_client.hpp"
#include "omote_link.hpp"

using namespace UI::Page;

BridgeSyncPage::BridgeSyncPage()
    : Base(ID::Pages::BridgeSyncPage),
      mTitle(AddNewElement<Widget::Label>("Bridge sync")),
      mBody(AddNewElement<Widget::Label>("")),
      mStatus(AddNewElement<Widget::Label>("")),
      mPullButton(AddNewElement<Widget::Button>([this] { pullFromBridge(); })),
      mPushButton(AddNewElement<Widget::Button>([this] { pushToBridge(); })) {

  SetBgColor(Color::BLACK);
  mTitle->SetHeight(lv_pct(12));
  mTitle->AlignTo(this, LV_ALIGN_TOP_MID, 0, 8);

  mBody->SetHeight(LV_SIZE_CONTENT);
  mBody->SetWidth(lv_pct(92));
  mBody->SetText(
      "The bridge holds your config (scenes, device settings, HA).\n\n"
      "Saving in the PC editor writes to the bridge. The remote keeps a "
      "backup copy and can pull from the bridge, or push its backup back "
      "after a bridge reflash.");
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

  refreshStatus();
}

void BridgeSyncPage::refreshStatus() {
  if (omote_link::state() != omote_link::LinkState::Linked) {
    mStatus->SetText("Status: bridge not linked (check WiFi / ESP-NOW).");
    return;
  }
  if (bridge_client::syncInProgress()) {
    mStatus->SetText("Status: syncing with bridge…");
    return;
  }
  if (bridge_client::configSynced()) {
    mStatus->SetText("Status: config synced with bridge.");
    return;
  }
  mStatus->SetText("Status: linked — waiting for first sync.");
}

void BridgeSyncPage::pullFromBridge() {
  bridge_client::requestConfigPull();
  mStatus->SetText("Status: pull started…");
}

void BridgeSyncPage::pushToBridge() {
  bridge_client::requestPushToBridge();
  mStatus->SetText("Status: push started…");
}

bool BridgeSyncPage::OnKeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) {
  return false;
}
