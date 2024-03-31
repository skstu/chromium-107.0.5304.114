// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/translate/core/common/translate_util.h"

#include <string>

#include "base/command_line.h"
#include "base/metrics/field_trial_params.h"
#include "build/build_config.h"
#include "components/translate/core/common/translate_switches.h"

namespace translate {

namespace {

// Parameter for TranslateSubFrames feature to determine whether language
// detection should include the sub frames (or just the main frame).
const char kDetectLanguageInSubFrames[] = "detect_language_in_sub_frames";

}  // namespace

const char kSecurityOrigin[] = "https://translate.googleapis.com/";

const base::Feature kTranslateSubFrames{"TranslateSubFrames",
                                        base::FEATURE_DISABLED_BY_DEFAULT};

// The feature is disabled on iOS since iOS currently does not support TFLite
// model execution. The feature is also explicitly disabled on Webview and
// Weblayer.
// TODO(crbug.com/1292622): Enable the feature on Webview.
// TODO(crbug.com/1247836): Enable the feature on WebLayer.
const base::Feature kTFLiteLanguageDetectionEnabled {
  "TFLiteLanguageDetectionEnabled",
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC) || BUILDFLAG(IS_ANDROID)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

const base::Feature kTFLiteLanguageDetectionIgnoreEnabled{
    "TFLiteLanguageDetectionIgnoreEnabled", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kDesktopPartialTranslate{"DesktopPartialTranslate",
                                             base::FEATURE_DISABLED_BY_DEFAULT};
const base::FeatureParam<int>
    kDesktopPartialTranslateTextSelectionMaxCharacters{
        &kDesktopPartialTranslate,
        "DesktopPartialTranslateTextSelectionMaxCharacters", 150};
const base::FeatureParam<int> kDesktopPartialTranslateBubbleShowDelayMs{
    &kDesktopPartialTranslate, "DesktopPartialTranslateBubbleShowDelayMs", 500};

#if !BUILDFLAG(IS_WIN)
const base::Feature kMmapLanguageDetectionModel{
    "MmapLanguageDetectionModel", base::FEATURE_DISABLED_BY_DEFAULT};
#endif

GURL GetTranslateSecurityOrigin() {
  std::string security_origin(kSecurityOrigin);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kTranslateSecurityOrigin)) {
    security_origin =
        command_line->GetSwitchValueASCII(switches::kTranslateSecurityOrigin);
  }
  return GURL(security_origin);
}

bool IsSubFrameTranslationEnabled() {
  return base::FeatureList::IsEnabled(kTranslateSubFrames);
}

bool IsSubFrameLanguageDetectionEnabled() {
  return base::FeatureList::IsEnabled(kTranslateSubFrames) &&
         base::GetFieldTrialParamByFeatureAsBool(
             kTranslateSubFrames, kDetectLanguageInSubFrames, true);
}

bool IsTFLiteLanguageDetectionEnabled() {
  return base::FeatureList::IsEnabled(kTFLiteLanguageDetectionEnabled);
}

bool IsTFLiteLanguageDetectionIgnoreEnabled() {
  return base::FeatureList::IsEnabled(kTFLiteLanguageDetectionIgnoreEnabled);
}

float GetTFLiteLanguageDetectionThreshold() {
  return base::GetFieldTrialParamByFeatureAsDouble(
      kTFLiteLanguageDetectionEnabled, "reliability_threshold", .7);
}

const base::Feature kTranslateAutoSnackbars{"TranslateAutoSnackbars",
                                            base::FEATURE_ENABLED_BY_DEFAULT};

int GetAutoAlwaysThreshold() {
  static constexpr base::FeatureParam<int> auto_always_threshold{
      &kTranslateAutoSnackbars, "AutoAlwaysThreshold", 5};
  return auto_always_threshold.Get();
}

int GetAutoNeverThreshold() {
  static constexpr base::FeatureParam<int> auto_never_threshold{
      &kTranslateAutoSnackbars, "AutoNeverThreshold", 20};
  return auto_never_threshold.Get();
}

int GetMaximumNumberOfAutoAlways() {
  static constexpr base::FeatureParam<int> auto_always_maximum{
      &kTranslateAutoSnackbars, "AutoAlwaysMaximum", 2};
  return auto_always_maximum.Get();
}

int GetMaximumNumberOfAutoNever() {
  static constexpr base::FeatureParam<int> auto_never_maximum{
      &kTranslateAutoSnackbars, "AutoNeverMaximum", 2};
  return auto_never_maximum.Get();
}

}  // namespace translate
