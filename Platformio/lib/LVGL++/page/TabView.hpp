#pragma once

#include "PageBase.hpp"
#include <functional>
#include <string>

namespace UI::Page {
class TabView;

class Tab : public Base {
  friend TabView;

public:
  typedef std::unique_ptr<Tab> Ptr;

  explicit Tab(lv_obj_t *aTab);
  Tab(lv_obj_t *aTab, Base::Ptr aContent);

  bool HasContent() const { return mContent != nullptr; }
  Page::Base *GetContent() const { return mContent; }
  void SetContent(Base::Ptr aContent);
  void ClearContent();

  void OnShow() override;
  void OnHide() override;
  UI::ID GetID() override;
  bool KeyEvent(KeyPressAbstract::KeyEvent aKeyEvent);

private:
  Base *mContent = nullptr;
};

class TabView : public Base {
public:
  TabView(ID aId);
  void AddTab(Page::Base::Ptr aPage);
  void AddPlaceholderTab(const std::string &title);
  void LoadTabContent(uint16_t aTabIdx, Page::Base::Ptr aPage);
  void UnloadTabContent(uint16_t aTabIdx);
  bool HasTabContent(uint16_t aTabIdx) const;
  size_t TabCount() const { return mTabs.size(); }
  Tab *GetCurrentTab();
  const Tab *GetCurrentTab() const;

  uint16_t GetCurrentTabIdx();
  void SetCurrentTabIdx(uint16_t aTabToSetActive,
                        lv_anim_enable_t aIsDoAnimation = LV_ANIM_ON);

  bool KeyEvent(KeyPressAbstract::KeyEvent aKeyEvent) override;

  void OnShow() override;
  void OnHide() override;

  // Attempts to go to tab with id
  // returns true if it was successful
  bool GoToTab(ID anId);

  void OnTabChangeEvent(std::function<void(uint16_t)> aTabChangeEventHandler) {
    mTabChangeEventHandler = aTabChangeEventHandler;
  }

protected:
  void OnLvglEvent(lv_event_t *anEvent) override;

private:
  void HandleTabChange();

  std::vector<Page::Tab::Ptr> mTabs;

  std::function<void(uint16_t)> mTabChangeEventHandler = nullptr;
};

} // namespace UI::Page
