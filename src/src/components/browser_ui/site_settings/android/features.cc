// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browser_ui/site_settings/android/features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"

namespace browser_ui {

const base::Feature kSiteDataImprovements{"SiteDataImprovements",
                                          base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kRequestDesktopSiteExceptionsDowngrade{
    "RequestDesktopSiteExceptionsDowngrade", base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace browser_ui
