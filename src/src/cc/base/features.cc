// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/base/features.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "ui/base/ui_base_features.h"

namespace features {

// Uses the Resume method instead of the Catch-up method for animated images.
// - Catch-up behavior tries to keep animated images in pace with wall-clock
//   time. This might require decoding several animation frames if the
//   animation has fallen behind.
// - Resume behavior presents what would have been the next presented frame.
//   This means it might only decode one frame, resuming where it left off.
//   However, if the animation updates faster than the display's refresh rate,
//   it is possible to decode more than a single frame.
const base::Feature kAnimatedImageResume = {"AnimatedImageResume",
                                            base::FEATURE_DISABLED_BY_DEFAULT};

bool IsImpulseScrollAnimationEnabled() {
  return base::FeatureList::IsEnabled(features::kWindowsScrollingPersonality);
}

// Whether the compositor should attempt to sync with the scroll handlers before
// submitting a frame.
const base::Feature kSynchronizedScrolling = {
    "SynchronizedScrolling",
#if BUILDFLAG(IS_ANDROID)
    base::FEATURE_DISABLED_BY_DEFAULT};
#else
    base::FEATURE_ENABLED_BY_DEFAULT};
#endif

const base::Feature kRemoveMobileViewportDoubleTap{
    "RemoveMobileViewportDoubleTap", base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kScrollUnification{"ScrollUnification",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSchedulerSmoothnessForAnimatedScrolls{
    "SmoothnessModeForAnimatedScrolls", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kHudDisplayForPerformanceMetrics{
    "HudDisplayForPerformanceMetrics", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kJankInjectionAblationFeature{
    "JankInjectionAblation", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPreferNewContentForCheckerboardedScrolls{
    "PreferNewContentForCheckerboardedScrolls",
    base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kDurationEstimatesInCompositorTimingHistory{
    "DurationEstimatesInCompositorTimingHistory",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNonBlockingCommit{"NonBlockingCommit",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSlidingWindowForDroppedFrameCounter{
    "SlidingWindowForDroppedFrameCounter", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNormalPriorityImageDecoding{
    "NormalPriorityImageDecoding", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSkipCommitsIfNotSynchronizingCompositorState{
    "SkipCommitsIfNotSynchronizingCompositorState",
    base::FEATURE_ENABLED_BY_DEFAULT};
}  // namespace features