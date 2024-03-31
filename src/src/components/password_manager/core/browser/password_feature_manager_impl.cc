// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_feature_manager_impl.h"

#include "build/build_config.h"
#include "components/password_manager/core/browser/password_manager_features_util.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/sync/driver/sync_service.h"

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
#include "components/autofill_assistant/browser/public/prefs.h"
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

namespace password_manager {

PasswordFeatureManagerImpl::PasswordFeatureManagerImpl(
    PrefService* pref_service,
    const syncer::SyncService* sync_service)
    : pref_service_(pref_service), sync_service_(sync_service) {}

bool PasswordFeatureManagerImpl::IsGenerationEnabled() const {
  switch (password_manager_util::GetPasswordSyncState(sync_service_)) {
    case SyncState::kNotSyncing:
      return ShouldShowAccountStorageOptIn();
    case SyncState::kSyncingWithCustomPassphrase:
    case SyncState::kSyncingNormalEncryption:
    case SyncState::kAccountPasswordsActiveNormalEncryption:
      return true;
  }
}

bool PasswordFeatureManagerImpl::
    AreRequirementsForAutomatedPasswordChangeFulfilled() const {
  // Only offer APC if Autofill Assistant is not disabled (by user choice
  // or by enterprise policy).
  // TODO(crbug.com/1359959): Also enable for Android once prefs are migrated to
  // profile prefs.
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
  if (!pref_service_->GetBoolean(
          autofill_assistant::prefs::kAutofillAssistantEnabled)) {
    return false;
  }
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

  // TODO(crbug.com/1349782): Re-enable for account store users once
  // adjustments to script fetchers and WebsiteLoginManager are made.
  switch (password_manager_util::GetPasswordSyncState(sync_service_)) {
    case SyncState::kNotSyncing:
    case SyncState::kAccountPasswordsActiveNormalEncryption:
      return false;
    case SyncState::kSyncingWithCustomPassphrase:
    case SyncState::kSyncingNormalEncryption:
      return true;
  }
}

bool PasswordFeatureManagerImpl::IsOptedInForAccountStorage() const {
  return features_util::IsOptedInForAccountStorage(pref_service_,
                                                   sync_service_);
}

bool PasswordFeatureManagerImpl::ShouldShowAccountStorageOptIn() const {
  return features_util::ShouldShowAccountStorageOptIn(pref_service_,
                                                      sync_service_);
}

bool PasswordFeatureManagerImpl::ShouldShowAccountStorageReSignin(
    const GURL& current_page_url) const {
  return features_util::ShouldShowAccountStorageReSignin(
      pref_service_, sync_service_, current_page_url);
}

void PasswordFeatureManagerImpl::OptInToAccountStorage() {
  features_util::OptInToAccountStorage(pref_service_, sync_service_);
}

void PasswordFeatureManagerImpl::OptOutOfAccountStorageAndClearSettings() {
  features_util::OptOutOfAccountStorageAndClearSettings(pref_service_,
                                                        sync_service_);
}

void PasswordFeatureManagerImpl::SetDefaultPasswordStore(
    const PasswordForm::Store& store) {
  features_util::SetDefaultPasswordStore(pref_service_, sync_service_, store);
}

bool PasswordFeatureManagerImpl::ShouldShowAccountStorageBubbleUi() const {
  return features_util::ShouldShowAccountStorageBubbleUi(pref_service_,
                                                         sync_service_);
}

bool PasswordFeatureManagerImpl::
    ShouldOfferOptInAndMoveToAccountStoreAfterSavingLocally() const {
  return ShouldShowAccountStorageOptIn() && !IsDefaultPasswordStoreSet();
}

PasswordForm::Store PasswordFeatureManagerImpl::GetDefaultPasswordStore()
    const {
  DCHECK(pref_service_);
  return features_util::GetDefaultPasswordStore(pref_service_, sync_service_);
}

bool PasswordFeatureManagerImpl::IsDefaultPasswordStoreSet() const {
  return features_util::IsDefaultPasswordStoreSet(pref_service_, sync_service_);
}

metrics_util::PasswordAccountStorageUsageLevel
PasswordFeatureManagerImpl::ComputePasswordAccountStorageUsageLevel() const {
  return features_util::ComputePasswordAccountStorageUsageLevel(pref_service_,
                                                                sync_service_);
}

void PasswordFeatureManagerImpl::RecordMoveOfferedToNonOptedInUser() {
  features_util::RecordMoveOfferedToNonOptedInUser(pref_service_,
                                                   sync_service_);
}

int PasswordFeatureManagerImpl::GetMoveOfferedToNonOptedInUserCount() const {
  return features_util::GetMoveOfferedToNonOptedInUserCount(pref_service_,
                                                            sync_service_);
}

}  // namespace password_manager
