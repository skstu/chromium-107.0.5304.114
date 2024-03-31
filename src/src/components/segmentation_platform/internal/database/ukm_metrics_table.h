// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_DATABASE_UKM_METRICS_TABLE_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_DATABASE_UKM_METRICS_TABLE_H_

#include <cstdint>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/types/id_type.h"
#include "components/segmentation_platform/internal/database/ukm_types.h"
#include "services/metrics/public/cpp/ukm_source_id.h"

namespace sql {
class Database;
}  // namespace sql

namespace segmentation_platform {

using MetricsRowId = base::IdType64<class MetricsRowTag>;
using MetricsRowEventId = base::IdType64<class MetricsRowEventIdTag>;

// Handles database queries for the UKM metrics table in UKM database.
class UkmMetricsTable {
 public:
  static constexpr char kTableName[] = "metrics";

  // Represents a row in the metrics table.
  struct MetricsRow {
    // Timestamp of the event, all the metrics in the event will have the same
    // timestamp. The timestamp is approximate and generated by the database
    // when getting notifications about UKM. Timestamps are required since its
    // used for deleting old entries.
    base::Time event_timestamp;

    // ID is not required to be filled in when inserting the row, and will not
    // be used. The ID will be generated by sql as primary key.
    MetricsRowId id;

    // ID of the URL, used to join with the URL table row.
    UrlId url_id;
    // UKM source ID for the entry.
    ukm::SourceId source_id = ukm::kInvalidSourceId;
    // Unique event ID associated with the UKM event. All metrics recorded with
    // in the event will have the same event ID.
    MetricsRowEventId event_id;

    UkmEventHash event_hash;
    UkmMetricHash metric_hash;
    int64_t metric_value = 0;
  };

  explicit UkmMetricsTable(sql::Database* db);
  ~UkmMetricsTable();

  UkmMetricsTable(const UkmMetricsTable&) = delete;
  UkmMetricsTable& operator=(const UkmMetricsTable&) = delete;

  // Creates the metrics table if it doesn't exist.
  bool InitTable();

  // Adds the given row to the metrics table, does not check for duplicate
  // entries.
  bool AddUkmEvent(const MetricsRow& row);

  // Updates URL ID of all the rows with |url_id| when the |source_id| matches.
  bool UpdateUrlIdForSource(ukm::SourceId source_id, UrlId url_id);

  // Deletes all rows associated with any of the ID from |urls|.
  bool DeleteEventsForUrls(const std::vector<UrlId>& urls);

  // Deletes all entries that have an event timestamp earlier or equal to
  // `time`. Returns a list of URL IDs that were removed by this task and no
  // longer referred to by any other metrics.
  std::vector<UrlId> DeleteEventsBeforeTimestamp(base::Time time);

 private:
  bool HasEntriesWithUrl(UrlId url_id);

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<sql::Database> db_ GUARDED_BY_CONTEXT(sequence_checker_);
};

}  // namespace segmentation_platform
#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_DATABASE_UKM_METRICS_TABLE_H_
