// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/commerce/core/metrics/scheduled_metrics_manager.h"

#include <vector>

#include "base/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/commerce/core/pref_names.h"
#include "components/commerce/core/price_tracking_utils.h"
#include "components/prefs/pref_service.h"

namespace commerce::metrics {

namespace {
const base::TimeDelta kDailyInterval = base::Days(1);
}  // namespace

const char kPriceNotificationEmailHistogramName[] =
    "Commerce.PriceTracking.EmailNotificationsEnabled";
const char kTrackedProductCountHistogramName[] =
    "Commerce.PriceTracking.PriceTrackedProductCount";

ScheduledMetricsManager::ScheduledMetricsManager(
    PrefService* prefs,
    bookmarks::BookmarkModel* bookmark_model)
    : pref_service_(prefs), bookmark_model_(bookmark_model) {
  daily_last_run_ = pref_service_->GetTime(kCommerceDailyMetricsLastUpdateTime);

  base::TimeDelta time_since_last = base::Time::Now() - daily_last_run_;
  int64_t daily_ms_delay = std::clamp(
      (kDailyInterval - time_since_last).InMilliseconds(),
      base::Seconds(0L).InMilliseconds(), kDailyInterval.InMilliseconds());

  daily_scheduled_task_ = std::make_unique<base::CancelableRepeatingClosure>(
      base::BindRepeating(&ScheduledMetricsManager::RunDailyTask,
                          weak_ptr_factory_.GetWeakPtr()));
  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, daily_scheduled_task_->callback(),
      base::Milliseconds(daily_ms_delay));
}

ScheduledMetricsManager::~ScheduledMetricsManager() = default;

void ScheduledMetricsManager::RunDailyTask() {
  // Update the last update time in prefs and immediately schedule the next.
  daily_last_run_ = base::Time::Now();
  pref_service_->SetTime(kCommerceDailyMetricsLastUpdateTime, daily_last_run_);
  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, daily_scheduled_task_->callback(), kDailyInterval);

  std::vector<const bookmarks::BookmarkNode*> tracked_products =
      GetAllPriceTrackedBookmarks(bookmark_model_);

  base::UmaHistogramCounts100(kTrackedProductCountHistogramName,
                              tracked_products.size());

  PriceNotificationEmailState state =
      PriceNotificationEmailState::kNotResponded;
  if (tracked_products.size() > 0) {
    if (pref_service_->GetBoolean(kPriceEmailNotificationsEnabled)) {
      state = PriceNotificationEmailState::kEnabled;
    } else {
      state = PriceNotificationEmailState::kDisabled;
    }
  }
  base::UmaHistogramEnumeration(kPriceNotificationEmailHistogramName, state);
}

}  // namespace commerce::metrics
