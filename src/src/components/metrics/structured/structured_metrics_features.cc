// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/structured/structured_metrics_features.h"

#include "base/metrics/field_trial_params.h"

namespace metrics {
namespace structured {

const base::Feature kStructuredMetrics{"EnableStructuredMetrics",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCrOSEvents{"EnableCrOSEvents",
                                base::FEATURE_DISABLED_BY_DEFAULT};

// TODO(b/181724341): Remove this experimental once the feature is rolled out.
const base::Feature kBluetoothSessionizedMetrics{
    "BluetoothSessionizedMetrics", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDelayUploadUntilHwid("DelayUploadUntilHwid",
                                          base::FEATURE_DISABLED_BY_DEFAULT);

bool IsIndependentMetricsUploadEnabled() {
  return base::GetFieldTrialParamByFeatureAsBool(
      kStructuredMetrics, "enable_independent_metrics_upload", true);
}

}  // namespace structured
}  // namespace metrics
