#include "TabView.hpp"

#include <string>

#include "BackgroundScreen.hpp"
#include "LvglResourceManager.hpp"

using namespace UI::Page;

Tab::Tab(lv_obj_t *aTab)
    : Base(aTab, UI::ID(UI::ID::Pages::INVALID_PAGE_ID)), mContent(nullptr) {}

Tab::Tab(lv_obj_t *aTab, Base::Ptr aContent)
    : Base(aTab, aContent->GetID()),
      mContent(AddElement(std::move(aContent))) {}

void Tab::SetContent(Base::Ptr aContent) {
  if (mContent || !aContent)
    return;
  mContent = AddElement(std::move(aContent));
}

void Tab::ClearContent() {
  if (!mContent)
    return;
  mContent->OnHide();
  RemoveElement(mContent);
  mContent = nullptr;
}

void Tab::OnShow() {
  if (mContent)
    mContent->OnShow();
}

void Tab::OnHide() {
  if (mContent)
    mContent->OnHide();
}

UI::ID Tab::GetID() {
  return mContent ? mContent->GetID() : UI::ID(UI::ID::Pages::INVALID_PAGE_ID);
}

bool Tab::KeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) {
  if (mContent)
    return mContent->KeyEvent(aKeyEvent);
  return false;
}

/////////////////////TabView/////////////////////////////////////

TabView::TabView(ID aId)
    : Base(lv_tabview_create(Screen::BackgroundScreen::getLvInstance()), aId) {
  lv_tabview_set_tab_bar_size(LvglSelf(), lv_pct(10));
  lv_tabview_set_tab_bar_position(LvglSelf(), LV_DIR_BOTTOM);
}

static void configureTabPanel(lv_obj_t *lTab) {
  auto lock = LvglResourceManager::GetInstance().scopeLock();
  lv_obj_remove_flag(lTab, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(lTab, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(lTab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(lTab, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(lTab, LV_SCROLLBAR_MODE_OFF);
}

void TabView::AddPlaceholderTab(const std::string &title) {
  auto *lTab = lv_tabview_add_tab(LvglSelf(), title.c_str());
  configureTabPanel(lTab);
  mTabs.push_back(std::make_unique<Tab>(lTab));
}

void TabView::AddTab(Page::Base::Ptr aPage) {
  auto *lTab = lv_tabview_add_tab(LvglSelf(), aPage->GetTitle().c_str());
  configureTabPanel(lTab);
  mTabs.push_back(std::make_unique<Tab>(lTab, std::move(aPage)));
}

void TabView::LoadTabContent(uint16_t aTabIdx, Page::Base::Ptr aPage) {
  if (aTabIdx >= mTabs.size())
    return;
  mTabs[aTabIdx]->SetContent(std::move(aPage));
}

void TabView::UnloadTabContent(uint16_t aTabIdx) {
  if (aTabIdx >= mTabs.size())
    return;
  mTabs[aTabIdx]->ClearContent();
}

bool TabView::HasTabContent(uint16_t aTabIdx) const {
  return aTabIdx < mTabs.size() && mTabs[aTabIdx]->HasContent();
}

uint16_t TabView::GetCurrentTabIdx() {
  return lv_tabview_get_tab_act(LvglSelf());
}

void TabView::SetCurrentTabIdx(uint16_t aTabToSetActive,
                               lv_anim_enable_t aIsDoAnimation) {
  lv_tabview_set_act(LvglSelf(), aTabToSetActive, aIsDoAnimation);
}

void TabView::HandleTabChange() {
  for (int i = 0; i < mTabs.size(); i++) {
    if (GetCurrentTabIdx() == i) {
      mTabs[i]->OnShow();
    } else {
      mTabs[i]->OnHide();
    }
  }
  if (mTabChangeEventHandler)
    mTabChangeEventHandler(GetCurrentTabIdx());
}

bool TabView::KeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) {
  if (OnKeyEvent(aKeyEvent)) {
    return true;
  }
  if (auto *current = GetCurrentTab(); current) {
    return current->KeyEvent(aKeyEvent);
  }
  return false;
}

void TabView::OnLvglEvent(lv_event_t *anEvent) {
  if (lv_event_get_code(anEvent) == LV_EVENT_VALUE_CHANGED) {
    HandleTabChange();
  }
}

bool TabView::GoToTab(ID anId) {
  auto tab = std::find_if(mTabs.begin(), mTabs.end(),
                          [anId](auto &tab) { return tab->GetID() == anId; });
  if (tab != mTabs.end()) {
    auto tabIdx = std::distance(mTabs.begin(), tab);
    SetCurrentTabIdx(tabIdx);
    return true;
  }
  return false;
}

void TabView::OnShow() {
  if (auto *current = GetCurrentTab(); current) {
    current->OnShow();
  }
}

void TabView::OnHide() {
  if (auto *current = GetCurrentTab(); current) {
    current->OnHide();
  }
}

UI::Page::Tab *TabView::GetCurrentTab() {
  auto idx = GetCurrentTabIdx();
  return idx < mTabs.size() ? mTabs[idx].get() : nullptr;
}
