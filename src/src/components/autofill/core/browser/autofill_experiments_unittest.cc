// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_experiments.h"

#include "base/callback_helpers.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "components/autofill/core/browser/logging/log_manager.h"
#include "components/autofill/core/browser/metrics/autofill_metrics.h"
#include "components/autofill/core/browser/metrics/payments/credit_card_save_metrics.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync/test/test_sync_service.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {

class AutofillExperimentsTest : public testing::Test {
 public:
  AutofillExperimentsTest() {}

 protected:
  void SetUp() override {
    pref_service_.registry()->RegisterBooleanPref(
        prefs::kAutofillWalletImportEnabled, true);
    log_manager_ = LogManager::Create(nullptr, base::NullCallback());
  }

  bool IsCreditCardUploadEnabled(const AutofillSyncSigninState sync_state) {
    return IsCreditCardUploadEnabled("john.smith@gmail.com", sync_state);
  }

  bool IsCreditCardUploadEnabled(const std::string& user_email,
                                 const AutofillSyncSigninState sync_state) {
    return IsCreditCardUploadEnabled(user_email, "US", sync_state);
  }

  bool IsCreditCardUploadEnabled(const std::string& user_email,
                                 const std::string& user_country,
                                 const AutofillSyncSigninState sync_state) {
    return autofill::IsCreditCardUploadEnabled(&pref_service_, &sync_service_,
                                               user_email, user_country,
                                               sync_state, log_manager_.get());
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  TestingPrefServiceSimple pref_service_;
  syncer::TestSyncService sync_service_;
  base::HistogramTester histogram_tester;
  std::unique_ptr<LogManager> log_manager_;
};

// Testing each scenario, followed by logging the metrics for various
// success and failure scenario of IsCreditCardUploadEnabled(). Every scenario
// should also be associated with logging of a metric so it's easy to analyze
// the results.
TEST_F(AutofillExperimentsTest, IsCardUploadEnabled_FeatureEnabled) {
  scoped_feature_list_.InitAndEnableFeature(features::kAutofillUpstream);
  // Use an unsupported country to show that feature flag enablement overrides
  // the client-side country check.
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "john.smith@gmail.com", "ZZ",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledByFlag, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledByFlag, 1);
}

TEST_F(AutofillExperimentsTest, IsCardUploadEnabled_UnsupportedCountry) {
  // "ZZ" is NOT one of the countries in |kAutofillUpstreamLaunchedCountries|.
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "john.smith@gmail.com", "ZZ",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kUnsupportedCountry, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kUnsupportedCountry, 1);
}

TEST_F(AutofillExperimentsTest, IsCardUploadEnabled_SupportedCountry) {
  // Show that a disabled or missing feature flag no longer impedes feature
  // availability. The flag is used only to launch to new countries.
  scoped_feature_list_.InitAndDisableFeature(features::kAutofillUpstream);
  // "US" is one of the countries in |kAutofillUpstreamLaunchedCountries|.
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "john.smith@gmail.com", "US",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 1);
}

TEST_F(AutofillExperimentsTest, IsCardUploadEnabled_AuthError) {
  sync_service_.SetAuthError(
      GoogleServiceAuthError(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS));
  EXPECT_FALSE(IsCreditCardUploadEnabled(AutofillSyncSigninState::kSyncPaused));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kSyncServicePersistentAuthError, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SyncPaused",
      autofill_metrics::CardUploadEnabled::kSyncServicePersistentAuthError, 1);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_SyncDoesNotHaveAutofillWalletDataActiveType) {
  sync_service_.SetActiveDataTypes(syncer::ModelTypeSet());
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::
          kSyncServiceMissingAutofillWalletDataActiveType,
      1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::
          kSyncServiceMissingAutofillWalletDataActiveType,
      1);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_SyncDoesNotHaveAutofillProfileActiveType) {
  sync_service_.SetActiveDataTypes(
      syncer::ModelTypeSet(syncer::AUTOFILL_WALLET_DATA));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::
          kSyncServiceMissingAutofillProfileActiveType,
      1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::
          kSyncServiceMissingAutofillProfileActiveType,
      1);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_SyncServiceUsingExplicitPassphrase) {
  sync_service_.SetIsUsingExplicitPassphrase(true);
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kUsingExplicitSyncPassphrase, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kUsingExplicitSyncPassphrase, 1);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_AutofillWalletImportEnabledPrefIsDisabled) {
  prefs::SetPaymentsIntegrationEnabled(&pref_service_, false);
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kPaymentsIntegrationDisabled, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kPaymentsIntegrationDisabled, 1);
}

TEST_F(AutofillExperimentsTest, IsCardUploadEnabled_EmptyUserEmail) {
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "", AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEmailEmpty, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEmailEmpty, 1);
}

TEST_F(AutofillExperimentsTest, IsCardUploadEnabled_TransportModeOnly) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillEnableAccountWalletStorage);
  // When we don't have Sync consent, Sync will start in Transport-only mode
  // (if allowed).
  sync_service_.SetHasSyncConsent(false);

  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "john.smith@gmail.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 1);
}

TEST_F(
    AutofillExperimentsTest,
    IsCardUploadEnabled_TransportSyncDoesNotHaveAutofillProfileActiveDataType) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillEnableAccountWalletStorage);
  // When we don't have Sync consent, Sync will start in Transport-only mode
  // (if allowed).
  sync_service_.SetHasSyncConsent(false);

  // Update the active types to only include Wallet. This disables all other
  // types, including profiles.
  sync_service_.SetActiveDataTypes(
      syncer::ModelTypeSet(syncer::AUTOFILL_WALLET_DATA));

  EXPECT_TRUE(IsCreditCardUploadEnabled(
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 1);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 1);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_UserEmailWithGoogleDomain_IsAllowed) {
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "john.smith@gmail.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "googler@google.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "old.school@googlemail.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "code.committer@chromium.org",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 4);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 4);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_UserEmailWithSupportedAdditionalDomain_IsAllowed) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillUpstreamAllowAdditionalEmailDomains);
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "cool.user@hotmail.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "cool.british.user@hotmail.co.uk",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "telecom.user@verizon.net",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "youve.got.mail@aol.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 4);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 4);
}

TEST_F(
    AutofillExperimentsTest,
    IsCardUploadEnabled_UserEmailWithSupportedAdditionalDomain_NotAllowedIfFlagOff) {
  scoped_feature_list_.InitAndDisableFeature(
      features::kAutofillUpstreamAllowAdditionalEmailDomains);
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "cool.user@hotmail.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "cool.british.user@hotmail.co.uk",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "telecom.user@verizon.net",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "youve.got.mail@aol.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEmailDomainNotSupported, 4);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEmailDomainNotSupported, 4);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_UserEmailWithOtherDomain_IsAllowed) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillUpstreamAllowAllEmailDomains);
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "grad.student@university.edu",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "some.ceo@bigcorporation.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "fake.googler@google.net",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_TRUE(IsCreditCardUploadEnabled(
      "fake.committer@chromium.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 4);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEnabledForCountry, 4);
}

TEST_F(AutofillExperimentsTest,
       IsCardUploadEnabled_UserEmailWithOtherDomain_NotAllowedIfFlagOff) {
  scoped_feature_list_.InitWithFeatures(
      /*enabled_features=*/{features::
                                kAutofillUpstreamAllowAdditionalEmailDomains},
      /*disabled_features=*/{features::kAutofillUpstreamAllowAllEmailDomains});
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "grad.student@university.edu",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "some.ceo@bigcorporation.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "fake.googler@google.net",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  EXPECT_FALSE(IsCreditCardUploadEnabled(
      "fake.committer@chromium.com",
      AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled));
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled",
      autofill_metrics::CardUploadEnabled::kEmailDomainNotSupported, 4);
  histogram_tester.ExpectUniqueSample(
      "Autofill.CardUploadEnabled.SignedInAndSyncFeatureEnabled",
      autofill_metrics::CardUploadEnabled::kEmailDomainNotSupported, 4);
}

}  // namespace autofill
