// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/history_clusters/core/cluster_metrics_utils.h"

#include "base/notreached.h"

namespace history_clusters {

std::string ClusterActionToString(ClusterAction action) {
  switch (action) {
    case ClusterAction::kDeleted:
      return "Deleted";
    case ClusterAction::kOpenedInTabGroup:
      return "OpenedInTabGroup";
    case ClusterAction::kRelatedSearchClicked:
      return "RelatedSearchClicked";
    case ClusterAction::kRelatedVisitsVisibilityToggled:
      return "RelatedVisitsVisibilityToggled";
    case ClusterAction::kVisitClicked:
      return "VisitClicked";
  }
  NOTREACHED();
  return std::string();
}

std::string VisitActionToString(VisitAction action) {
  switch (action) {
    case VisitAction::kDeleted:
      return "Deleted";
    case VisitAction::kClicked:
      return "Clicked";
  }
  NOTREACHED();
  return std::string();
}

std::string VisitTypeToString(VisitType action) {
  switch (action) {
    case VisitType::kSRP:
      return "SRP";
    case VisitType::kNonSRP:
      return "nonSRP";
  }
  NOTREACHED();
  return std::string();
}

std::string RelatedSearchActionToString(RelatedSearchAction action) {
  switch (action) {
    case RelatedSearchAction::kClicked:
      return "Clicked";
  }
  NOTREACHED();
  return std::string();
}

}  // namespace history_clusters
