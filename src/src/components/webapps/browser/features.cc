// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/webapps/browser/features.h"

#include "base/feature_list.h"

namespace webapps {
namespace features {

#if BUILDFLAG(IS_ANDROID)
const base::Feature kAddToHomescreenMessaging{
    "AddToHomescreenMessaging", base::FEATURE_DISABLED_BY_DEFAULT};

// Enables or disables the installable ambient badge infobar.
const base::Feature kInstallableAmbientBadgeInfoBar{
    "InstallableAmbientBadgeInfoBar", base::FEATURE_ENABLED_BY_DEFAULT};

// Enables or disables the installable ambient badge message.
const base::Feature kInstallableAmbientBadgeMessage{
    "InstallableAmbientBadgeMessage", base::FEATURE_DISABLED_BY_DEFAULT};

// The capacity of cached domains which do not show message again if
// users do not accept the message.
extern const base::FeatureParam<int>
    kInstallableAmbientBadgeMessage_ThrottleDomainsCapacity{
        &kInstallableAmbientBadgeMessage,
        "installable_ambient_badge_message_throttle_domains_capacity", 100};

// Enables PWA Unique IDs for WebAPKs.
const base::Feature kWebApkUniqueId{"WebApkUniqueId",
                                    base::FEATURE_DISABLED_BY_DEFAULT};
#endif  // BUILDFLAG(IS_ANDROID)

// When the user clicks "Create Shortcut" in the dot menu, the current page is
// used as start-url, instead of the manifest-supplied value.
// This allows subpages of web apps to be bookmarked via shortcuts
// separately from their parent app.
// For installing the parent app, the existing "Install Site" should be used
// instead. With this feature, "Install Site" now also shows up for websites
// without service worker, as long as they have a manifest.
const base::Feature kCreateShortcutIgnoresManifest{
    "CreateShortcutIgnoresManifest", base::FEATURE_DISABLED_BY_DEFAULT};

// Skip the service worker in all install criteria check. This affect both
// "intallable" and "promotable" status of a web app.
const base::Feature kSkipServiceWorkerCheckAll{
    "SkipServiceWorkerCheckAll", base::FEATURE_DISABLED_BY_DEFAULT};

// Skip the service worker install criteria check for installing. This affect
// only the "installable" status but not "promotable".
const base::Feature kSkipServiceWorkerCheckInstallOnly{
  "SkipServiceWorkerCheckInstallOnly",
#if BUILDFLAG(IS_ANDROID)
      base::FEATURE_ENABLED_BY_DEFAULT
#else
      base::FEATURE_DISABLED_BY_DEFAULT
#endif
};

// Enables showing a detailed install dialog for user installs.
const base::Feature kDesktopPWAsDetailedInstallDialog{
    "DesktopPWAsDetailedInstallDialog", base::FEATURE_DISABLED_BY_DEFAULT};

bool SkipBannerServiceWorkerCheck() {
  return base::FeatureList::IsEnabled(kSkipServiceWorkerCheckAll);
}

bool SkipInstallServiceWorkerCheck() {
  return base::FeatureList::IsEnabled(kSkipServiceWorkerCheckAll) ||
         base::FeatureList::IsEnabled(kSkipServiceWorkerCheckInstallOnly);
}

}  // namespace features
}  // namespace webapps
