// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_POLICY_CORE_COMMON_PREFERENCES_MAC_H_
#define COMPONENTS_POLICY_CORE_COMMON_PREFERENCES_MAC_H_

#include <CoreFoundation/CoreFoundation.h>

#include <memory>

#include "components/policy/policy_export.h"

// Wraps a small part of the `CFPreferences` and `CFPrefsManagedSource` API
// surface.

// See CFPreferences documentation for following functions' documentation:
//  AppSynchronize()
//  CopyAppValue()
//  AppValueIsForced()
class POLICY_EXPORT MacPreferences {
 public:
  // Wraps Apple's private `CFPrefsManagedSource` API to determine the scope of
  // a policy.
  class PolicyScope {
   public:
    virtual ~PolicyScope() = default;
    virtual void Init(CFStringRef application_id) = 0;
    virtual Boolean IsManagedPolicyAvailable(CFStringRef key) = 0;
    virtual void Enable(bool enable) = 0;
  };

  MacPreferences();
  MacPreferences(const MacPreferences&) = delete;
  MacPreferences& operator=(const MacPreferences&) = delete;
  virtual ~MacPreferences();

  // Calls CFPreferencesAppSynchronize and initialize `policy_scope_`.
  virtual Boolean AppSynchronize(CFStringRef application_id);

  // Calls CFPreferencesCopyAppValue.
  virtual CFPropertyListRef CopyAppValue(CFStringRef key,
                                         CFStringRef application_id);

  // Calls CFPreferencesAppValueIsForced.
  virtual Boolean AppValueIsForced(CFStringRef key, CFStringRef application_id);

  // Calls CFPrefsManagedSource.copyValueForKey to determine if the policy is
  // set at the machine scope for `application_id` that is set by
  // `AppSynchronize()` function above.
  virtual Boolean IsManagedPolicyAvailableForMachineScope(CFStringRef key);

  virtual void LoadPolicyScopeDetectionPolicy(CFStringRef application_id);

 private:
  std::unique_ptr<PolicyScope> policy_scope_;
};

#endif  // COMPONENTS_POLICY_CORE_COMMON_PREFERENCES_MAC_H_
