// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_MANAGER_H_
#define CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_MANAGER_H_

#include <vector>

#include "base/callback_forward.h"
#include "content/browser/attribution_reporting/attribution_report.h"
#include "content/public/browser/storage_partition.h"

namespace base {
class Time;
}  // namespace base

namespace content {

class AttributionDataHostManager;
class AttributionObserver;
class AttributionTrigger;
class StorableSource;
class StoredSource;
class WebContents;

// Interface that mediates data flow between the network, storage layer, and
// blink.
class AttributionManager {
 public:
  static AttributionManager* FromWebContents(WebContents* web_contents);

  virtual ~AttributionManager() = default;

  virtual void AddObserver(AttributionObserver* observer) = 0;

  virtual void RemoveObserver(AttributionObserver* observer) = 0;

  // Gets manager responsible for tracking pending data hosts targeting `this`.
  virtual AttributionDataHostManager* GetDataHostManager() = 0;

  // Persists the given |source| to storage. Called when a navigation
  // originating from a source tag finishes.
  virtual void HandleSource(StorableSource source) = 0;

  // Process a newly registered trigger. Will create and log any new
  // reports to storage.
  virtual void HandleTrigger(AttributionTrigger trigger) = 0;

  // Get all sources that are currently stored in this partition. Used for
  // populating WebUI.
  virtual void GetActiveSourcesForWebUI(
      base::OnceCallback<void(std::vector<StoredSource>)> callback) = 0;

  // Get all pending reports that are currently stored in this partition. Used
  // for populating WebUI and simulator.
  virtual void GetPendingReportsForInternalUse(
      AttributionReport::ReportTypes report_types,
      int limit,
      base::OnceCallback<void(std::vector<AttributionReport>)> callback) = 0;

  // Sends the given reports immediately, and runs |done| once they have all
  // been sent.
  virtual void SendReportsForWebUI(
      const std::vector<AttributionReport::Id>& ids,
      base::OnceClosure done) = 0;

  // Deletes all data in storage for storage keys matching `filter`, between
  // `delete_begin` and `delete_end` time.
  //
  // If `filter` is null, then consider all storage keys in storage as matching.
  virtual void ClearData(base::Time delete_begin,
                         base::Time delete_end,
                         StoragePartition::StorageKeyMatcherFunction filter,
                         bool delete_rate_limit_data,
                         base::OnceClosure done) = 0;
};

}  // namespace content

#endif  // CONTENT_BROWSER_ATTRIBUTION_REPORTING_ATTRIBUTION_MANAGER_H_