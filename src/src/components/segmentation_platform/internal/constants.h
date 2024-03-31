// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_CONSTANTS_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_CONSTANTS_H_

namespace segmentation_platform {

// The path to the pref storing the segmentation result.
extern const char kSegmentationResultPref[];

// The path to the pref storing when UKM are allowed recently.
extern const char kSegmentationUkmMostRecentAllowedTimeKey[];

// Last metrics collection time for the segmentation platform.
extern const char kSegmentationLastCollectionTimePref[];

extern const char kSegmentationPlatformRefreshResultsSwitch[];

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_CONSTANTS_H_