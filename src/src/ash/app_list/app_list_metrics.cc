// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/app_list_metrics.h"

#include <algorithm>
#include <string>

#include "ash/app_list/app_list_controller_impl.h"
#include "ash/app_list/app_list_model_provider.h"
#include "ash/app_list/model/app_list_folder_item.h"
#include "ash/app_list/model/app_list_item.h"
#include "ash/app_list/model/app_list_item_list.h"
#include "ash/app_list/model/search/search_result.h"
#include "ash/app_list/views/continue_section_view.h"
#include "ash/constants/ash_features.h"
#include "ash/public/cpp/app_menu_constants.h"
#include "ash/shell.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "ui/compositor/compositor.h"

namespace ash {

// The number of files removed from the continue section during this session.
int g_continue_file_removals_in_session = 0;

const char kAppListPeekingToFullscreenHistogram[] =
    "Apps.AppListPeekingToFullscreenSource";

// The UMA histogram that logs smoothness of pagination animation.
constexpr char kPaginationTransitionAnimationSmoothness[] =
    "Apps.PaginationTransition.AnimationSmoothness";
constexpr char kPaginationTransitionAnimationSmoothnessInTablet[] =
    "Apps.PaginationTransition.AnimationSmoothness.TabletMode";
constexpr char kPaginationTransitionAnimationSmoothnessInClamshell[] =
    "Apps.PaginationTransition.AnimationSmoothness.ClamshellMode";

// The UMA histogram that logs which state search results are opened from.
constexpr char kAppListSearchResultOpenSourceHistogram[] =
    "Apps.AppListSearchResultOpenedSource";

// The UMA histogram that logs smoothness of cardified animation.
constexpr char kCardifiedStateAnimationSmoothnessEnter[] =
    "Apps.AppList.CardifiedStateAnimation.AnimationSmoothness."
    "EnterCardifiedState";
constexpr char kCardifiedStateAnimationSmoothnessExit[] =
    "Apps.AppList.CardifiedStateAnimation.AnimationSmoothness."
    "ExitCardifiedState";

// The UMA hisotogram that logs the action user performs on zero state
// search result.
constexpr char kAppListZeroStateSearchResultUserActionHistogram[] =
    "Apps.AppList.ZeroStateSearchResultUserActionType";

// The UMA histogram that logs user's decision (remove or cancel) for zero state
// search result removal confirmation.
constexpr char kAppListZeroStateSearchResultRemovalHistogram[] =
    "Apps.AppList.ZeroStateSearchResultRemovalDecision";

// The UMA histogram that logs user's decision (remove or cancel) for search
// result removal confirmation. Recorded if productivity launcher is enabled, in
// which case search result removal is enabled outside zero state search.
// Otherwise, the dialog result is reported using
// `kAppListZeroStateSearchResultRemovalHistogram`.
constexpr char kSearchResultRemovalDialogDecisionHistogram[] =
    "Apps.AppList.SearchResultRemovalDecision";

// The base UMA histogram that logs app launches within the HomeLauncher (tablet
// mode AppList), and the fullscreen AppList (when ProductivityLauncher is
// disabled in clamshell mode) and the Shelf.
constexpr char kAppListAppLaunched[] = "Apps.AppListAppLaunchedV2";

// UMA histograms that log app launches within the app list, and the shelf.
// Split depending on whether tablet mode is active or not.
constexpr char kAppLaunchInTablet[] = "Apps.AppList.AppLaunched.TabletMode";
constexpr char kAppLaunchInClamshell[] =
    "Apps.AppList.AppLaunched.ClamshellMode";

// UMA histograms that log launcher workflow actions (launching an app, search
// result, or a continue section task) in the app list UI. Split depending on
// whether tablet mode is active or not. Note that unlike `kAppListAppLaunched`
// histograms, these do not include actions from shelf, but do include non-app
// launch actions.
constexpr char kLauncherUserActionInTablet[] =
    "Apps.AppList.UserAction.TabletMode";
constexpr char kLauncherUserActionInClamshell[] =
    "Apps.AppList.UserAction.ClamshellMode";

// UMA histograms that log time elapsed from launcher getting shown at the time
// of an user taking a launcher workflow action (launching an app, search
// result, or a continue section task) in the app list UI. Split depending on
// whether tablet mode is active or not.
constexpr char kTimeToLauncherUserActionInTablet[] =
    "Apps.AppList.TimeToUserAction.TabletMode";
constexpr char kTimeToLauncherUserActionInClamshell[] =
    "Apps.AppList.TimeToUserAction.ClamshellMode";

// The UMA histograms that log app launches within the AppList, AppListBubble
// and Shelf. The app launches are divided by histogram for each of the the
// different AppList states.
constexpr char kAppListAppLaunchedBubbleAllApps[] =
    "Apps.AppListAppLaunchedV2.BubbleAllApps";
constexpr char kAppListAppLaunchedClosed[] = "Apps.AppListAppLaunchedV2.Closed";
constexpr char kAppListAppLaunchedPeeking[] =
    "Apps.AppListAppLaunchedV2.Peeking";
constexpr char kAppListAppLaunchedHalf[] = "Apps.AppListAppLaunchedV2.Half";
constexpr char kAppListAppLaunchedFullscreenAllApps[] =
    "Apps.AppListAppLaunchedV2.FullscreenAllApps";
constexpr char kAppListAppLaunchedFullscreenSearch[] =
    "Apps.AppListAppLaunchedV2.FullscreenSearch";
constexpr char kAppListAppLaunchedHomecherClosed[] =
    "Apps.AppListAppLaunchedV2.HomecherClosed";
constexpr char kAppListAppLaunchedHomecherAllApps[] =
    "Apps.AppListAppLaunchedV2.HomecherAllApps";
constexpr char kAppListAppLaunchedHomecherSearch[] =
    "Apps.AppListAppLaunchedV2.HomecherSearch";

constexpr char kClamshellReorderAnimationSmoothnessHistogram[] =
    "Apps.Launcher.ProductivityReorderAnimationSmoothness.ClamshellMode";
constexpr char kTabletReorderAnimationSmoothnessHistogram[] =
    "Apps.Launcher.ProductivityReorderAnimationSmoothness.TabletMode";

constexpr char kClamshellReorderActionHistogram[] =
    "Apps.Launcher.ProductivityReorderAction.ClamshellMode";
constexpr char kTabletReorderActionHistogram[] =
    "Apps.Launcher.ProductivityReorderAction.TabletMode";

// The prefix for all the variants that track how long the app list is kept
// open by open method. Suffix is decided in `GetAppListOpenMethod`
constexpr char kAppListOpenTimePrefix[] = "Apps.AppListOpenTime.";

constexpr char kContinueSectionFilesRemovedInSessionHistogram[] =
    "Apps.AppList.Search.ContinueSectionFilesRemovedPerSession";

// The different sources from which a search result is displayed. These values
// are written to logs.  New enum values can be added, but existing enums must
// never be renumbered or deleted and reused.
enum class ApplistSearchResultOpenedSource {
  kHalfClamshell = 0,
  kFullscreenClamshell = 1,
  kFullscreenTablet = 2,
  kMaxApplistSearchResultOpenedSource = 3,
};

AppLaunchedMetricParams::AppLaunchedMetricParams() = default;

AppLaunchedMetricParams::AppLaunchedMetricParams(
    const AppLaunchedMetricParams&) = default;

AppLaunchedMetricParams& AppLaunchedMetricParams::operator=(
    const AppLaunchedMetricParams&) = default;

AppLaunchedMetricParams::AppLaunchedMetricParams(
    AppListLaunchedFrom launched_from,
    AppListLaunchType launch_type)
    : launched_from(launched_from), launch_type(launch_type) {}

AppLaunchedMetricParams::~AppLaunchedMetricParams() = default;

void AppListRecordPageSwitcherSourceByEventType(ui::EventType type,
                                                bool is_tablet_mode) {
  AppListPageSwitcherSource source;

  switch (type) {
    case ui::ET_MOUSEWHEEL:
      source = kMouseWheelScroll;
      break;
    case ui::ET_SCROLL:
      source = kMousePadScroll;
      break;
    case ui::ET_GESTURE_SCROLL_END:
      source = kSwipeAppGrid;
      break;
    case ui::ET_SCROLL_FLING_START:
      source = kFlingAppGrid;
      break;
    case ui::ET_MOUSE_RELEASED:
      source = kMouseDrag;
      break;
    default:
      NOTREACHED();
      return;
  }
  RecordPageSwitcherSource(source, is_tablet_mode);
}

void RecordPageSwitcherSource(AppListPageSwitcherSource source,
                              bool is_tablet_mode) {
  UMA_HISTOGRAM_ENUMERATION("Apps.AppListPageSwitcherSource", source,
                            kMaxAppListPageSwitcherSource);
  if (is_tablet_mode) {
    UMA_HISTOGRAM_ENUMERATION("Apps.AppListPageSwitcherSource.TabletMode",
                              source, kMaxAppListPageSwitcherSource);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Apps.AppListPageSwitcherSource.ClamshellMode",
                              source, kMaxAppListPageSwitcherSource);
  }
}

void RecordSearchResultOpenSource(const SearchResult* result,
                                  AppListViewState state,
                                  bool is_tablet_mode) {
  // Record the search metric if the SearchResult is not a suggested app.
  if (result->is_recommendation())
    return;

  ApplistSearchResultOpenedSource source;
  if (is_tablet_mode) {
    source = ApplistSearchResultOpenedSource::kFullscreenTablet;
  } else {
    source = state == AppListViewState::kHalf
                 ? ApplistSearchResultOpenedSource::kHalfClamshell
                 : ApplistSearchResultOpenedSource::kFullscreenClamshell;
  }
  UMA_HISTOGRAM_ENUMERATION(
      kAppListSearchResultOpenSourceHistogram, source,
      ApplistSearchResultOpenedSource::kMaxApplistSearchResultOpenedSource);
}

void RecordZeroStateSearchResultUserActionHistogram(
    ZeroStateSearchResultUserActionType action) {
  UMA_HISTOGRAM_ENUMERATION(kAppListZeroStateSearchResultUserActionHistogram,
                            action);
}

void RecordZeroStateSearchResultRemovalHistogram(
    SearchResultRemovalConfirmation removal_decision) {
  UMA_HISTOGRAM_ENUMERATION(kAppListZeroStateSearchResultRemovalHistogram,
                            removal_decision);
}

void RecordSearchResultRemovalDialogDecision(
    SearchResultRemovalConfirmation removal_decision) {
  base::UmaHistogramEnumeration(kSearchResultRemovalDialogDecisionHistogram,
                                removal_decision);
}

std::string GetAppListOpenMethod(AppListShowSource source) {
  // This switch determines which metric we submit for the Apps.AppListOpenTime
  // metric. Adding a string requires you update the apps histogram.xml as well.
  switch (source) {
    case kSearchKey:
    case kSearchKeyFullscreen_DEPRECATED:
      return "SearchKey";
    case kShelfButton:
    case kShelfButtonFullscreen:
      return "HomeButton";
    case kSwipeFromShelf:
      return "Swipe";
    case kScrollFromShelf:
      return "Scroll";
    case kTabletMode:
    case kAssistantEntryPoint:
      return "Others";
  }
  NOTREACHED();
}

void RecordAppListUserJourneyTime(AppListShowSource source,
                                  base::TimeDelta time) {
  base::UmaHistogramMediumTimes(
      kAppListOpenTimePrefix + GetAppListOpenMethod(source), time);
}

void RecordPeriodicAppListMetrics() {
  int number_of_apps_in_launcher = 0;
  int number_of_root_level_items = 0;
  int number_of_folders = 0;
  int number_of_non_system_folders = 0;
  int number_of_apps_in_non_system_folders = 0;

  AppListModel* const model = AppListModelProvider::Get()->model();
  AppListItemList* const item_list = model->top_level_item_list();
  for (size_t i = 0; i < item_list->item_count(); ++i) {
    AppListItem* item = item_list->item_at(i);
    if (item->is_page_break())
      continue;
    number_of_root_level_items++;

    // Item is a folder.
    if (item->GetItemType() == AppListFolderItem::kItemType) {
      AppListFolderItem* folder = static_cast<AppListFolderItem*>(item);
      number_of_apps_in_launcher += folder->item_list()->item_count();
      number_of_folders++;

      // Ignore the OEM folder and the "Linux apps" folder because those folders
      // are automatically created. The following metrics are trying to measure
      // how often users engage with folders that they created themselves.
      if (folder->IsSystemFolder())
        continue;
      number_of_apps_in_non_system_folders += folder->item_list()->item_count();
      number_of_non_system_folders++;
      continue;
    }

    // Item is an app that isn't in a folder.
    number_of_apps_in_launcher++;
  }

  UMA_HISTOGRAM_COUNTS_100("Apps.AppList.NumberOfApps",
                           number_of_apps_in_launcher);
  UMA_HISTOGRAM_COUNTS_100("Apps.AppList.NumberOfRootLevelItems",
                           number_of_root_level_items);
  UMA_HISTOGRAM_COUNTS_100("Apps.AppList.NumberOfFolders", number_of_folders);
  UMA_HISTOGRAM_COUNTS_100("Apps.AppList.NumberOfNonSystemFolders",
                           number_of_non_system_folders);
  UMA_HISTOGRAM_COUNTS_100("Apps.AppList.NumberOfAppsInNonSystemFolders",
                           number_of_apps_in_non_system_folders);
}

void RecordAppListAppLaunched(AppListLaunchedFrom launched_from,
                              AppListViewState app_list_state,
                              bool is_tablet_mode,
                              bool app_list_shown) {
  UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunched, launched_from);

  if (is_tablet_mode) {
    base::UmaHistogramEnumeration(kAppLaunchInTablet, launched_from);

  } else {
    base::UmaHistogramEnumeration(kAppLaunchInClamshell, launched_from);
  }

  if (features::IsProductivityLauncherEnabled() && !is_tablet_mode) {
    if (!app_list_shown) {
      UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedClosed, launched_from);
    } else {
      // TODO(newcomer): Handle the case where search is open.
      UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedBubbleAllApps,
                                launched_from);
    }
    return;
  }

  switch (app_list_state) {
    case AppListViewState::kClosed:
      DCHECK(!features::IsProductivityLauncherEnabled());
      // Only exists in clamshell mode with ProductivityLauncher disabled.
      UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedClosed, launched_from);
      break;
    case AppListViewState::kPeeking:
      DCHECK(!features::IsProductivityLauncherEnabled());
      // Only exists in clamshell mode with ProductivityLauncher disabled.
      UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedPeeking, launched_from);
      break;
    case AppListViewState::kHalf:
      DCHECK(!features::IsProductivityLauncherEnabled());
      // Only exists in clamshell mode with ProductivityLauncher disabled.
      UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedHalf, launched_from);
      break;
    case AppListViewState::kFullscreenAllApps:
      if (is_tablet_mode) {
        if (app_list_shown) {
          UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedHomecherAllApps,
                                    launched_from);
        } else {
          UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedHomecherClosed,
                                    launched_from);
        }
      } else {
        UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedFullscreenAllApps,
                                  launched_from);
      }
      break;
    case AppListViewState::kFullscreenSearch:
      if (is_tablet_mode) {
        if (app_list_shown) {
          UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedHomecherSearch,
                                    launched_from);
        } else {
          // (http://crbug.com/947729) Search box still expanded when opening
          // launcher in tablet mode
          UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedHomecherClosed,
                                    launched_from);
        }
      } else {
        UMA_HISTOGRAM_ENUMERATION(kAppListAppLaunchedFullscreenSearch,
                                  launched_from);
      }
      break;
  }
}

ASH_EXPORT void RecordLauncherWorkflowMetrics(
    AppListUserAction action,
    bool is_tablet_mode,
    absl::optional<base::TimeTicks> launcher_show_time) {
  if (is_tablet_mode) {
    base::UmaHistogramEnumeration(kLauncherUserActionInTablet, action);

    if (launcher_show_time) {
      base::UmaHistogramMediumTimes(
          kTimeToLauncherUserActionInTablet,
          base::TimeTicks::Now() - *launcher_show_time);
    }
  } else {
    base::UmaHistogramEnumeration(kLauncherUserActionInClamshell, action);

    if (launcher_show_time) {
      base::UmaHistogramMediumTimes(
          kTimeToLauncherUserActionInClamshell,
          base::TimeTicks::Now() - *launcher_show_time);
    }
  }
}

bool IsCommandIdAnAppLaunch(int command_id_number) {
  CommandId command_id = static_cast<CommandId>(command_id_number);

  // Consider all platform app menu options as launches.
  if (command_id >= CommandId::EXTENSIONS_CONTEXT_CUSTOM_FIRST &&
      command_id < CommandId::EXTENSIONS_CONTEXT_CUSTOM_LAST) {
    return true;
  }

  // Consider all arc app shortcut options as launches.
  if (command_id >= CommandId::LAUNCH_APP_SHORTCUT_FIRST &&
      command_id < CommandId::LAUNCH_APP_SHORTCUT_LAST) {
    return true;
  }

  // All app menu items in a ShelfApplicationMenuModel are not launches.
  if (command_id >= CommandId::APP_MENU_ITEM_ID_FIRST &&
      command_id < CommandId::APP_MENU_ITEM_ID_LAST) {
    return false;
  }

  switch (command_id) {
    // Used by AppContextMenu and/or ShelfContextMenu.
    case CommandId::LAUNCH_NEW:
    case CommandId::SHOW_APP_INFO:
    case CommandId::OPTIONS:
    case CommandId::APP_CONTEXT_MENU_NEW_WINDOW:
    case CommandId::APP_CONTEXT_MENU_NEW_INCOGNITO_WINDOW:
    case CommandId::SETTINGS:
    // Used by both AppContextMenu and ShelfContextMenu for app shortcuts.
    case CommandId::LAUNCH_APP_SHORTCUT_FIRST:
    case CommandId::LAUNCH_APP_SHORTCUT_LAST:
      return true;

    // Used by ShelfContextMenu (shelf).
    case CommandId::MENU_CLOSE:
    case CommandId::SWAP_WITH_NEXT:
    case CommandId::SWAP_WITH_PREVIOUS:
    // Used by AppMenuModelAdapter
    case CommandId::NOTIFICATION_CONTAINER:
    // Used by CrostiniShelfContextMenu.
    case CommandId::CROSTINI_USE_LOW_DENSITY:
    case CommandId::CROSTINI_USE_HIGH_DENSITY:
    // Used by AppContextMenu.
    case CommandId::TOGGLE_PIN:
    case CommandId::UNINSTALL:
    case CommandId::REMOVE_FROM_FOLDER:
    case CommandId::INSTALL:
    case CommandId::USE_LAUNCH_TYPE_PINNED:
    case CommandId::USE_LAUNCH_TYPE_REGULAR:
    case CommandId::USE_LAUNCH_TYPE_FULLSCREEN:
    case CommandId::USE_LAUNCH_TYPE_WINDOW:
    case CommandId::USE_LAUNCH_TYPE_TABBED_WINDOW:
    case CommandId::USE_LAUNCH_TYPE_COMMAND_END:
    case CommandId::REORDER_SUBMENU:
    case CommandId::REORDER_BY_NAME_ALPHABETICAL:
    case CommandId::REORDER_BY_NAME_REVERSE_ALPHABETICAL:
    case CommandId::REORDER_BY_COLOR:
    case CommandId::SHUTDOWN_GUEST_OS:
    case CommandId::EXTENSIONS_CONTEXT_CUSTOM_FIRST:
    case CommandId::EXTENSIONS_CONTEXT_CUSTOM_LAST:
    case CommandId::COMMAND_ID_COUNT:
    // Used by ShelfApplicationMenuModel.
    case CommandId::APP_MENU_ITEM_ID_FIRST:
    case CommandId::APP_MENU_ITEM_ID_LAST:
      return false;
    case CommandId::DEPRECATED_MENU_OPEN_NEW:
    case CommandId::DEPRECATED_MENU_PIN:
    case CommandId::DEPRECATED_MENU_NEW_WINDOW:
    case CommandId::DEPRECATED_MENU_NEW_INCOGNITO_WINDOW:
    case CommandId::DEPRECATED_LAUNCH_TYPE_PINNED_TAB:
    case CommandId::DEPRECATED_LAUNCH_TYPE_REGULAR_TAB:
    case CommandId::DEPRECATED_LAUNCH_TYPE_WINDOW:
    case CommandId::DEPRECATED_LAUNCH_TYPE_TABBED_WINDOW:
    case CommandId::DEPRECATED_LAUNCH_TYPE_FULLSCREEN:
      NOTREACHED();
      return false;
  }
  NOTREACHED();
  return false;
}

void ReportPaginationSmoothness(bool is_tablet_mode, int smoothness) {
  UMA_HISTOGRAM_PERCENTAGE(kPaginationTransitionAnimationSmoothness,
                           smoothness);

  if (is_tablet_mode) {
    UMA_HISTOGRAM_PERCENTAGE(kPaginationTransitionAnimationSmoothnessInTablet,
                             smoothness);
  } else {
    UMA_HISTOGRAM_PERCENTAGE(
        kPaginationTransitionAnimationSmoothnessInClamshell, smoothness);
  }
}

void ReportCardifiedSmoothness(bool is_entering_cardified, int smoothness) {
  if (is_entering_cardified) {
    UMA_HISTOGRAM_PERCENTAGE(kCardifiedStateAnimationSmoothnessEnter,
                             smoothness);
  } else {
    UMA_HISTOGRAM_PERCENTAGE(kCardifiedStateAnimationSmoothnessExit,
                             smoothness);
  }
}

// Reports reorder animation smoothness.
void ReportReorderAnimationSmoothness(bool in_tablet, int smoothness) {
  if (in_tablet) {
    base::UmaHistogramPercentage(kTabletReorderAnimationSmoothnessHistogram,
                                 smoothness);
  } else {
    base::UmaHistogramPercentage(kClamshellReorderAnimationSmoothnessHistogram,
                                 smoothness);
  }
}

void RecordAppListSortAction(AppListSortOrder new_order, bool in_tablet) {
  // NOTE: (1) kNameReverseAlphabetical is not used for now; (2) Resetting the
  // sort order is not recorded here.
  DCHECK(new_order != AppListSortOrder::kNameReverseAlphabetical &&
         new_order != AppListSortOrder::kCustom);

  if (in_tablet)
    base::UmaHistogramEnumeration(kTabletReorderActionHistogram, new_order);
  else
    base::UmaHistogramEnumeration(kClamshellReorderActionHistogram, new_order);
}

void RecordMetricsOnSessionEnd() {
  if (ContinueSectionView::EnableContinueSectionFileRemovalMetrics() &&
      g_continue_file_removals_in_session == 0) {
    base::UmaHistogramCounts100(kContinueSectionFilesRemovedInSessionHistogram,
                                0);
  }
}

void RecordCumulativeContinueSectionResultRemovedNumber() {
  base::UmaHistogramCounts100(kContinueSectionFilesRemovedInSessionHistogram,
                              ++g_continue_file_removals_in_session);
}

void ResetContinueSectionFileRemovedCountForTest() {
  g_continue_file_removals_in_session = 0;
}

void RecordHideContinueSectionMetric() {
  // The continue section is a productivity launcher feature.
  if (!features::IsProductivityLauncherEnabled())
    return;

  const bool hide_continue_section =
      Shell::Get()->app_list_controller()->ShouldHideContinueSection();
  if (Shell::Get()->IsInTabletMode()) {
    base::UmaHistogramBoolean(
        "Apps.AppList.ContinueSectionHiddenByUser.TabletMode",
        hide_continue_section);
  } else {
    base::UmaHistogramBoolean(
        "Apps.AppList.ContinueSectionHiddenByUser.ClamshellMode",
        hide_continue_section);
  }
}

}  // namespace ash