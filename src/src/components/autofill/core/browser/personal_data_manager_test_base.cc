// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/personal_data_manager_test_base.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"

namespace autofill {

namespace {

const char kPrimaryAccountEmail[] = "syncuser@example.com";
const char kSyncTransportAccountEmail[] = "transport@example.com";

ACTION_P(QuitMessageLoop, loop) {
  loop->Quit();
}

}  // anonymous namespace

PersonalDataLoadedObserverMock::PersonalDataLoadedObserverMock() = default;
PersonalDataLoadedObserverMock::~PersonalDataLoadedObserverMock() = default;

// static
std::vector<base::Feature>
PersonalDataManagerTestBase::GetDefaultEnabledFeatures() {
  // Enable account storage by default, some tests will override this to be
  // false.
  return {features::kAutofillEnableAccountWalletStorage};
}

PersonalDataManagerTestBase::PersonalDataManagerTestBase(
    const std::vector<base::Feature>& additional_enabled_features)
    : identity_test_env_(&test_url_loader_factory_) {
  std::vector<base::Feature> all_enabled_features(
      PersonalDataManagerTestBase::GetDefaultEnabledFeatures());
  base::ranges::copy(additional_enabled_features,
                     std::back_inserter(all_enabled_features));
  scoped_features_.InitWithFeatures(all_enabled_features,
                                    /*disabled_features=*/{});
}

PersonalDataManagerTestBase::~PersonalDataManagerTestBase() = default;

void PersonalDataManagerTestBase::SetUpTest() {
  OSCryptMocker::SetUp();
  prefs_ = test::PrefServiceForTesting();
  base::FilePath path(WebDatabase::kInMemoryPath);
  profile_web_database_ =
      new WebDatabaseService(path, base::ThreadTaskRunnerHandle::Get(),
                             base::ThreadTaskRunnerHandle::Get());

  // Hacky: hold onto a pointer but pass ownership.
  profile_autofill_table_ = new AutofillTable;
  profile_web_database_->AddTable(
      std::unique_ptr<WebDatabaseTable>(profile_autofill_table_));
  profile_web_database_->LoadDatabase();
  profile_database_service_ = new AutofillWebDataService(
      profile_web_database_, base::ThreadTaskRunnerHandle::Get(),
      base::ThreadTaskRunnerHandle::Get());
  profile_database_service_->Init(base::NullCallback());

  account_web_database_ = new WebDatabaseService(
      base::FilePath(WebDatabase::kInMemoryPath),
      base::ThreadTaskRunnerHandle::Get(), base::ThreadTaskRunnerHandle::Get());
  account_autofill_table_ = new AutofillTable;
  account_web_database_->AddTable(
      std::unique_ptr<WebDatabaseTable>(account_autofill_table_));
  account_web_database_->LoadDatabase();
  account_database_service_ = new AutofillWebDataService(
      account_web_database_, base::ThreadTaskRunnerHandle::Get(),
      base::ThreadTaskRunnerHandle::Get());
  account_database_service_->Init(base::NullCallback());

  strike_database_ = std::make_unique<TestInMemoryStrikeDatabase>();

  test::DisableSystemServices(prefs_.get());
}

void PersonalDataManagerTestBase::TearDownTest() {
  // Order of destruction is important as BrowserAutofillManager relies on
  // PersonalDataManager to be around when it gets destroyed.
  test::ReenableSystemServices();
  OSCryptMocker::TearDown();
}

void PersonalDataManagerTestBase::ResetPersonalDataManager(
    bool is_incognito,
    bool use_sync_transport_mode,
    PersonalDataManager* personal_data) {
  personal_data->Init(
      scoped_refptr<AutofillWebDataService>(profile_database_service_),
      base::FeatureList::IsEnabled(
          features::kAutofillEnableAccountWalletStorage)
          ? scoped_refptr<AutofillWebDataService>(account_database_service_)
          : nullptr,
      prefs_.get(), prefs_.get(), identity_test_env_.identity_manager(),
      /*history_service=*/nullptr, strike_database_.get(),
      /*image_fetcher=*/nullptr, is_incognito);

  personal_data->AddObserver(&personal_data_observer_);
  std::string email = use_sync_transport_mode ? kSyncTransportAccountEmail
                                              : kPrimaryAccountEmail;
  // Set the account in both IdentityManager and SyncService.
  CoreAccountInfo account_info;
  signin::ConsentLevel consent_level = use_sync_transport_mode
                                           ? signin::ConsentLevel::kSignin
                                           : signin::ConsentLevel::kSync;
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  identity_test_env_.ClearPrimaryAccount();
  account_info = identity_test_env_.SetPrimaryAccount(email, consent_level);
#else
  // In ChromeOS-Ash, clearing/resetting the primary account is not supported.
  // So if an account already exists, reuse it (and make sure it matches).
  if (identity_test_env_.identity_manager()->HasPrimaryAccount(consent_level)) {
    account_info = identity_test_env_.identity_manager()->GetPrimaryAccountInfo(
        consent_level);
    ASSERT_EQ(account_info.email, email);
  } else {
    account_info = identity_test_env_.SetPrimaryAccount(email, consent_level);
  }
#endif
  sync_service_.SetAccountInfo(account_info);
  sync_service_.SetHasSyncConsent(!use_sync_transport_mode);
  personal_data->OnSyncServiceInitialized(&sync_service_);
  personal_data->OnStateChanged(&sync_service_);

  WaitForOnPersonalDataChangedRepeatedly();
}

[[nodiscard]] bool PersonalDataManagerTestBase::TurnOnSyncFeature(
    PersonalDataManager* personal_data) {
  sync_service_.SetHasSyncConsent(true);
  if (!sync_service_.IsSyncFeatureEnabled())
    return false;
  personal_data->OnStateChanged(&sync_service_);

  return personal_data->IsSyncFeatureEnabled();
}

void PersonalDataManagerTestBase::RemoveByGUIDFromPersonalDataManager(
    const std::string& guid,
    PersonalDataManager* personal_data) {
  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .Times(testing::AnyNumber());

  personal_data->RemoveByGUID(guid);
  run_loop.Run();
}

void PersonalDataManagerTestBase::SetServerCards(
    std::vector<CreditCard> server_cards) {
  test::SetServerCreditCards(account_autofill_table_, server_cards);
}

// Verify that the web database has been updated and the notification sent.
void PersonalDataManagerTestBase::WaitOnceForOnPersonalDataChanged() {
  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  run_loop.Run();
}

// Verifies that the web database has been updated and the notification sent.
void PersonalDataManagerTestBase::WaitForOnPersonalDataChanged() {
  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .Times(testing::AnyNumber());
  run_loop.Run();
}

// Verifies that the web database has been updated and the notification sent.
void PersonalDataManagerTestBase::WaitForOnPersonalDataChangedRepeatedly() {
  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillRepeatedly(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .Times(testing::AnyNumber());
  run_loop.Run();
}

}  // namespace autofill
