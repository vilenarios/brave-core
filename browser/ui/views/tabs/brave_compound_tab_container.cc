/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/tabs/brave_compound_tab_container.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "brave/browser/ui/color/brave_color_id.h"
#include "brave/browser/ui/tabs/brave_tab_prefs.h"
#include "brave/browser/ui/tabs/features.h"
#include "brave/browser/ui/views/frame/vertical_tabs/vertical_tab_strip_region_view.h"
#include "brave/browser/ui/views/tabs/brave_tab_container.h"
#include "brave/browser/ui/views/tabs/vertical_tab_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ui_features.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/controls/scrollbar/scroll_bar_views.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_utils.h"

namespace {
class ContentsView : public views::View {
 public:
  explicit ContentsView(BraveCompoundTabContainer* container)
      : container_(container) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
  }
  ~ContentsView() override = default;

  // views::View:
  void ChildPreferredSizeChanged(views::View* child) override {
    // Bypass ScrollView and notify the BraveCompoundTabContainer directly.
    container_->ChildPreferredSizeChanged(child);
  }

  base::raw_ptr<BraveCompoundTabContainer> container_;
};

// A custom scroll view to avoid bugs from upstream
// At the moment,
//  * ScrollRectToVisible() doesn't work well, so disable layer and make it
//    easier to manipulate offset.
//  * When disabling ScrollWithLayers, OnScrollEvent cause DCHECK failure.
//  * Even when scrollbar is kHiddenButEnabled, the width for contents view is
//  cut off.
//    In order to avoid that, attach overlay scroll bar which doesn't take
//    space.
class CustomScrollView : public views::ScrollView {
  METADATA_HEADER(CustomScrollView, views::ScrollView)
 public:
  explicit CustomScrollView(PrefService* prefs)
      : views::ScrollView(views::ScrollView::ScrollWithLayers::kDisabled) {
    SetDrawOverflowIndicator(false);
    SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);

    should_show_scroll_bar_.Init(
        brave_tabs::kVerticalTabsShowScrollbar, prefs,
        base::BindRepeating(&CustomScrollView::UpdateScrollbarVisibility,
                            base::Unretained(this)));
    UpdateScrollbarVisibility();
  }
  ~CustomScrollView() override = default;

  // views::ScrollView:
  void OnScrollEvent(ui::ScrollEvent* event) override {
    // DO NOTHING to avoid crash when layer is disabled.
  }

 private:
  void UpdateScrollbarVisibility() {
    if (*should_show_scroll_bar_) {
      SetVerticalScrollBarMode(views::ScrollView::ScrollBarMode::kEnabled);
      // We can't use ScrollBarViews on Mac
#if !BUILDFLAG(IS_MAC)
      SetVerticalScrollBar(std::make_unique<views::ScrollBarViews>(
          views::ScrollBar::Orientation::kVertical));
#endif
    } else {
      SetVerticalScrollBarMode(
          views::ScrollView::ScrollBarMode::kHiddenButEnabled);
      SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
          views::ScrollBar::Orientation::kVertical));
    }
    DeprecatedLayoutImmediately();
  }

  BooleanPrefMember should_show_scroll_bar_;
};

BEGIN_METADATA(CustomScrollView)
END_METADATA

}  // namespace

BraveCompoundTabContainer::BraveCompoundTabContainer(
    TabContainerController& controller,
    TabHoverCardController* hover_card_controller,
    TabDragContextBase* drag_context,
    TabSlotController& tab_slot_controller,
    views::View* scroll_contents_view)
    : CompoundTabContainer(controller,
                           hover_card_controller,
                           drag_context,
                           tab_slot_controller,
                           scroll_contents_view),
      tab_slot_controller_(tab_slot_controller) {}

void BraveCompoundTabContainer::SetAvailableWidthCallback(
    base::RepeatingCallback<int()> available_width_callback) {
  CompoundTabContainer::SetAvailableWidthCallback(available_width_callback);
  if (tabs::utils::ShouldShowVerticalTabs(tab_slot_controller_->GetBrowser()) &&
      available_width_callback) {
    pinned_tab_container_->SetAvailableWidthCallback(
        base::BindRepeating(&views::View::width, base::Unretained(this)));
    if (base::FeatureList::IsEnabled(
            tabs::features::kBraveVerticalTabScrollBar)) {
      unpinned_tab_container_->SetAvailableWidthCallback(base::BindRepeating(
          &BraveCompoundTabContainer::GetAvailableWidthConsideringScrollBar,
          base::Unretained(this)));
    } else {
      unpinned_tab_container_->SetAvailableWidthCallback(
          base::BindRepeating(&views::View::width, base::Unretained(this)));
    }
    return;
  }

  // Upstream's compound tab container doesn't use this.
  pinned_tab_container_->SetAvailableWidthCallback(base::NullCallback());
  unpinned_tab_container_->SetAvailableWidthCallback(base::NullCallback());
}

BraveCompoundTabContainer::~BraveCompoundTabContainer() {
  if (scroll_view_) {
    // Remove scroll view and set this as the parent of
    // |unpinned_tab_container_| so that clean up can be done by upstream code.
    SetScrollEnabled(false);
  }
}

base::OnceClosure BraveCompoundTabContainer::LockLayout() {
  std::vector<base::OnceClosure> closures;
  for (const auto& tab_container :
       {unpinned_tab_container_, pinned_tab_container_}) {
    closures.push_back(
        static_cast<BraveTabContainer*>(base::to_address(tab_container))
            ->LockLayout());
  }

  return base::BindOnce(
      [](std::vector<base::OnceClosure> closures) {
        std::ranges::for_each(closures,
                              [](auto& closure) { std::move(closure).Run(); });
      },
      std::move(closures));
}

void BraveCompoundTabContainer::SetScrollEnabled(bool enabled) {
  if (enabled == !!scroll_view_) {
    return;
  }

  if (enabled) {
    scroll_view_ = AddChildView(std::make_unique<CustomScrollView>(
        tab_slot_controller_->GetBrowser()->profile()->GetPrefs()));
    scroll_view_->SetBackgroundColor(kColorToolbar);
    auto* contents_view =
        scroll_view_->SetContents(std::make_unique<ContentsView>(this));
    contents_view->AddChildView(base::to_address(unpinned_tab_container_));
    DeprecatedLayoutImmediately();
  } else {
    unpinned_tab_container_->parent()->RemoveChildView(
        base::to_address(unpinned_tab_container_));
    AddChildView(base::to_address(unpinned_tab_container_));

    RemoveChildViewT(scroll_view_.get());
    scroll_view_ = nullptr;
  }
}

void BraveCompoundTabContainer::TransferTabBetweenContainers(
    int from_model_index,
    int to_model_index) {
  const bool was_pinned = to_model_index < NumPinnedTabs();
  CompoundTabContainer::TransferTabBetweenContainers(from_model_index,
                                                     to_model_index);
  if (!ShouldShowVerticalTabs()) {
    return;
  }

  // Override transfer animation so that it goes well with vertical tab strip.
  CompleteAnimationAndLayout();

  const bool is_pinned = to_model_index < NumPinnedTabs();
  bool layout_dirty = false;
  if (is_pinned && !pinned_tab_container_->GetVisible()) {
    // When the browser was initialized without any pinned tabs, pinned tabs
    // could be hidden initially by the FlexLayout.
    pinned_tab_container_->SetVisible(true);
    layout_dirty = true;
  }

  // Animate tab from left to right.
  Tab* tab = GetTabAtModelIndex(to_model_index);
  tab->SetPosition({-tab->width(), 0});
  TabContainer& to_container =
      *(is_pinned ? pinned_tab_container_ : unpinned_tab_container_);
  to_container.AnimateToIdealBounds();

  if (was_pinned != is_pinned) {
    // After transferring a tab from one to another container, we should layout
    // the previous container.
    auto previous_container =
        was_pinned ? pinned_tab_container_ : unpinned_tab_container_;
    previous_container->CompleteAnimationAndLayout();
    PreferredSizeChanged();
    layout_dirty = true;
  }

  if (layout_dirty) {
    // Tab could have different insets per containers(ex, split tabs).
    tab->UpdateInsets();
    DeprecatedLayoutImmediately();
  }
}

void BraveCompoundTabContainer::Layout(PassKey) {
  if (!ShouldShowVerticalTabs()) {
    LayoutSuperclass<CompoundTabContainer>(this);
    return;
  }

  const auto contents_bounds = GetContentsBounds();

  // Pinned container gets however much space it wants.
  pinned_tab_container_->SetBoundsRect(
      gfx::Rect(gfx::Size(contents_bounds.width(),
                          pinned_tab_container_->GetPreferredSize().height())));

  // Unpinned container gets the left over.
  if (scroll_view_) {
    auto bounds = gfx::Rect(
        contents_bounds.x(), pinned_tab_container_->bounds().bottom(), width(),
        contents_bounds.height() - pinned_tab_container_->height());
    scroll_view_->SetBoundsRect(bounds);
    if (scroll_view_->GetMaxHeight() != bounds.height()) {
      scroll_view_->ClipHeightTo(0, scroll_view_->height());
    }

    UpdateUnpinnedContainerSize();
  } else {
    unpinned_tab_container_->SetBoundsRect(
        gfx::Rect(contents_bounds.x(), pinned_tab_container_->bounds().bottom(),
                  contents_bounds.width(),
                  contents_bounds.height() - pinned_tab_container_->height()));
  }
}

gfx::Size BraveCompoundTabContainer::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::CalculatePreferredSize(available_size);
  }

  auto preferred_size =
      CompoundTabContainer::CalculatePreferredSize(available_size);

  const int combined_height =
      pinned_tab_container_->GetPreferredSize().height() +
      unpinned_tab_container_->GetPreferredSize().height();

  // Traverse up the parent hierarchy to find the |VerticalTabStripRegionView|
  for (auto* parent_view = parent(); parent_view;
       parent_view = parent_view->parent()) {
    auto* region_view =
        views::AsViewClass<VerticalTabStripRegionView>(parent_view);
    if (!region_view) {
      continue;
    }

    preferred_size.set_height(
        std::min(combined_height, region_view->GetTabStripViewportMaxHeight()));
    break;
  }

  return preferred_size;
}

gfx::Size BraveCompoundTabContainer::GetMinimumSize() const {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::GetMinimumSize();
  }

  return {};
}

views::SizeBounds BraveCompoundTabContainer::GetAvailableSize(
    const views::View* child) const {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::GetAvailableSize(child);
  }

  return views::SizeBounds(views::SizeBound(width()),
                           /*height=*/views::SizeBound());
}

std::vector<Tab*> BraveCompoundTabContainer::AddTabs(
    std::vector<TabInsertionParams> tabs_params) {
  if (!tabs::utils::ShouldShowVerticalTabs(
          tab_slot_controller_->GetBrowser())) {
    return CompoundTabContainer::AddTabs(std::move(tabs_params));
  }

  // Upstream implementation expects all tabs to be either pinned or unpinned.
  // It also checks that the index of pinned tabs is <= than NumPinnedTabs(),
  // which returns 0 and triggers a check. That check doesn't make sense since
  // if I am adding 2 pinned tabs with model_indexes 0 and 1, when checking
  // for model_index 1 NumPinnedTabs() would still return 0 as we have not added
  // the tab with the index 0 yet. So, add tabs ourselves.
  std::vector<TabInsertionParams> pinned_tabs_params;
  std::vector<TabInsertionParams> unpinned_tabs_params;
  for (auto& params : tabs_params) {
    if (params.pinned == TabPinned::kPinned) {
      pinned_tabs_params.push_back(std::move(params));
    } else {
      unpinned_tabs_params.push_back(std::move(params));
    }
  }

  std::vector<Tab*> new_tabs;
  if (!pinned_tabs_params.empty()) {
    new_tabs = pinned_tab_container_->AddTabs(std::move(pinned_tabs_params));
  }

  if (!unpinned_tabs_params.empty()) {
    for (auto& params : unpinned_tabs_params) {
      CHECK_GE(params.model_index, NumPinnedTabs());
      params.model_index -= NumPinnedTabs();
    }
    std::vector<Tab*> unpinned_new_tabs =
        unpinned_tab_container_->AddTabs(std::move(unpinned_tabs_params));

    // Merge pinned and unpinned tabs into the new_tabs vector.
    new_tabs.insert(new_tabs.end(),
                    std::make_move_iterator(unpinned_new_tabs.begin()),
                    std::make_move_iterator(unpinned_new_tabs.end()));
  }

  for (auto* new_tab : new_tabs) {
    const auto pinned =
        new_tab->data().pinned ? TabPinned::kPinned : TabPinned::kUnpinned;

    if (pinned == TabPinned::kPinned && !pinned_tab_container_->GetVisible()) {
      // When the browser was initialized without any pinned tabs, pinned tabs
      // could be hidden initially by the FlexLayout.
      pinned_tab_container_->SetVisible(true);
    }

    if (scroll_view_ && pinned == TabPinned::kUnpinned && new_tab->IsActive()) {
      ScrollTabToBeVisible(new_tab);
    }
  }
  UpdatePinnedTabContainerBorder();

  return new_tabs;
}

void BraveCompoundTabContainer::MoveTab(int from_model_index,
                                        int to_model_index) {
  CompoundTabContainer::MoveTab(from_model_index, to_model_index);
  UpdatePinnedTabContainerBorder();
}

void BraveCompoundTabContainer::RemoveTab(int index, bool was_active) {
  CompoundTabContainer::RemoveTab(index, was_active);
  UpdatePinnedTabContainerBorder();
}

void BraveCompoundTabContainer::SetTabPinned(int model_index,
                                             TabPinned pinned) {
  CompoundTabContainer::SetTabPinned(model_index, pinned);
  UpdatePinnedTabContainerBorder();
}

int BraveCompoundTabContainer::GetUnpinnedContainerIdealLeadingX() const {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::GetUnpinnedContainerIdealLeadingX();
  }

  return 0;
}

std::optional<BrowserRootView::DropIndex>
BraveCompoundTabContainer::GetDropIndex(const ui::DropTargetEvent& event) {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::GetDropIndex(event);
  }

  TabContainer* sub_drop_target = GetTabContainerAt(event.location());
  CHECK(sub_drop_target);
  CHECK(sub_drop_target->GetDropTarget(
      ConvertPointToTarget(this, sub_drop_target, event.location())));

  // Convert to `sub_drop_target`'s local coordinate space.
  const gfx::Point loc_in_sub_target = ConvertPointToTarget(
      this, sub_drop_target->GetViewForDrop(), event.location());
  const ui::DropTargetEvent adjusted_event = ui::DropTargetEvent(
      event.data(), gfx::PointF(loc_in_sub_target),
      gfx::PointF(loc_in_sub_target), event.source_operations());

  if (sub_drop_target == base::to_address(pinned_tab_container_)) {
    // Pinned tab container shares an index and coordinate space, so no
    // adjustments needed.
    return sub_drop_target->GetDropIndex(adjusted_event);
  } else {
    // For the unpinned container, we need to transform the output to the
    // correct index space.
    auto sub_target_index = sub_drop_target->GetDropIndex(adjusted_event);
    return BrowserRootView::DropIndex{
        .index = sub_target_index->index + NumPinnedTabs(),
        .relative_to_index = sub_target_index->relative_to_index,
        .group_inclusion = sub_target_index->group_inclusion};
  }
}

BrowserRootView::DropTarget* BraveCompoundTabContainer::GetDropTarget(
    gfx::Point loc_in_local_coords) {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::GetDropTarget(loc_in_local_coords);
  }

  // At this moment, upstream doesn't have implementation for this path yet.
  // TODO(1346023): Implement text drag and drop.

  if (!GetLocalBounds().Contains(loc_in_local_coords)) {
    return nullptr;
  }

  if (GetTabContainerAt(loc_in_local_coords)) {
    return this;
  }

  return nullptr;
}

void BraveCompoundTabContainer::PaintChildren(const views::PaintInfo& info) {
  if (ShouldShowVerticalTabs()) {
    // Bypass CompoundTabContainer::PaintChildren() implementation.
    // CompoundTabContainer calls children's View::Paint() even when they have
    // their own layer, which shouldn't happen.
    views::View::PaintChildren(info);
  } else {
    CompoundTabContainer::PaintChildren(info);
  }
}

void BraveCompoundTabContainer::ChildPreferredSizeChanged(views::View* child) {
  if (ShouldShowVerticalTabs() && scroll_view_ &&
      child->Contains(base::to_address(unpinned_tab_container_))) {
    UpdateUnpinnedContainerSize();
  }

  CompoundTabContainer::ChildPreferredSizeChanged(child);
}

void BraveCompoundTabContainer::SetActiveTab(
    std::optional<size_t> prev_active_index,
    std::optional<size_t> new_active_index) {
  CompoundTabContainer::SetActiveTab(prev_active_index, new_active_index);
  if (new_active_index.has_value()) {
    ScrollTabToBeVisible(GetTabAtModelIndex(*new_active_index));
  }
}

views::View* BraveCompoundTabContainer::TargetForRect(views::View* root,
                                                      const gfx::Rect& rect) {
  if (base::FeatureList::IsEnabled(
          tabs::features::kBraveVerticalTabScrollBar) &&
      scroll_view_) {
    auto* scroll_bar = scroll_view_->vertical_scroll_bar();
    const gfx::Rect rect_in_scroll_bar =
        views::View::ConvertRectToTarget(root, scroll_bar, rect);
    if (scroll_bar->GetLocalBounds().Contains(rect_in_scroll_bar)) {
      return scroll_bar->GetEventHandlerForRect(rect_in_scroll_bar);
    }
  }

  return CompoundTabContainer::TargetForRect(root, rect);
}

TabContainer* BraveCompoundTabContainer::GetTabContainerAt(
    gfx::Point point_in_local_coords) const {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::GetTabContainerAt(point_in_local_coords);
  }

  auto* container =
      point_in_local_coords.y() < pinned_tab_container_->bounds().bottom()
          ? base::to_address(pinned_tab_container_)
          : base::to_address(unpinned_tab_container_);

  if (!container->GetWidget()) {
    // Note that this could be happen when we're detaching tabs and we're still
    // changing view hierarchy.
    return nullptr;
  }

  return container;
}

gfx::Rect BraveCompoundTabContainer::ConvertUnpinnedContainerIdealBoundsToLocal(
    gfx::Rect ideal_bounds) const {
  if (!ShouldShowVerticalTabs()) {
    return CompoundTabContainer::ConvertUnpinnedContainerIdealBoundsToLocal(
        ideal_bounds);
  }

  if (scroll_view_) {
    return views::View::ConvertRectToTarget(
        /*source=*/base::to_address(unpinned_tab_container_), /*target=*/this,
        ideal_bounds);
  }

  ideal_bounds.Offset(0, unpinned_tab_container_->y());
  return ideal_bounds;
}

bool BraveCompoundTabContainer::ShouldShowVerticalTabs() const {
  return tabs::utils::ShouldShowVerticalTabs(
      tab_slot_controller_->GetBrowser());
}

void BraveCompoundTabContainer::UpdatePinnedTabContainerBorder() {
  // We're using pinned tab container's bottom border as a separator from
  // unpinned tab container. It should be drawn when only both are not empty.
  const bool should_have_separator_between_pinned_and_unpinned =
      ShouldShowVerticalTabs() && pinned_tab_container_->GetTabCount() != 0 &&
      unpinned_tab_container_->GetTabCount() != 0;
  if (should_have_separator_between_pinned_and_unpinned) {
    pinned_tab_container_->SetBorder(views::CreateSolidSidedBorder(
        gfx::Insets().set_bottom(1), kColorBraveVerticalTabSeparator));
  } else {
    pinned_tab_container_->SetBorder(nullptr);
  }
}

void BraveCompoundTabContainer::UpdateUnpinnedContainerSize() {
  DCHECK(scroll_view_);

  auto preferred_size = unpinned_tab_container_->GetPreferredSize();
  preferred_size.set_width(scroll_view_->width());
  preferred_size.set_height(
      std::max(scroll_view_->height(), preferred_size.height()));
  if (scroll_view_->contents()->height() != preferred_size.height()) {
    scroll_view_->contents()->SetSize(preferred_size);
  }
}

void BraveCompoundTabContainer::ScrollTabToBeVisible(Tab* tab) {
  CHECK(tab);
  if (!scroll_view_) {
    return;
  }

  if (tab->data().pinned) {
    return;
  }

  DCHECK(scroll_view_->contents()->Contains(tab));

  gfx::RectF tab_bounds_in_contents_view(tab->GetLocalBounds());
  views::View::ConvertRectToTarget(tab, scroll_view_->contents(),
                                   &tab_bounds_in_contents_view);

  const auto visible_rect = scroll_view_->GetVisibleRect();
  if (visible_rect.Contains(gfx::Rect(0, tab_bounds_in_contents_view.y(),
                                      1 /*in order to ignore width */,
                                      tab_bounds_in_contents_view.height()))) {
    return;
  }

  if (visible_rect.CenterPoint().y() >=
      tab_bounds_in_contents_view.CenterPoint().y()) {
    // Scroll Up
    scroll_view_->ScrollToOffset(
        gfx::PointF(0, static_cast<int>(tab_bounds_in_contents_view.y())));
  } else {
    // Scroll Down
    scroll_view_->ScrollToOffset(gfx::PointF(
        0, -std::min(0, scroll_view_->height() -
                            static_cast<int>(
                                tab_bounds_in_contents_view.bottom() +
                                tabs::kMarginForVerticalTabContainers))));
  }
}

int BraveCompoundTabContainer::GetAvailableWidthConsideringScrollBar() {
  CHECK(
      base::FeatureList::IsEnabled(tabs::features::kBraveVerticalTabScrollBar));
  if (scroll_view_) {
    auto* scroll_bar = scroll_view_->vertical_scroll_bar();
    if (scroll_bar->GetVisible()) {
      return width() - scroll_view_->GetScrollBarLayoutWidth();
    }
  }
  return width();
}

BEGIN_METADATA(BraveCompoundTabContainer)
END_METADATA
