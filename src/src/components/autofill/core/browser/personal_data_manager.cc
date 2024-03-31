// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/personal_data_manager.h"

#include <stddef.h>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/containers/contains.h"
#include "base/containers/cxx20_erase.h"
#include "base/feature_list.h"
#include "base/guid.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/timezone.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/observer_list.h"
#include "base/ranges/algorithm.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_download_manager.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/autofill_profile_save_strike_database.h"
#include "components/autofill/core/browser/data_model/autofill_profile_comparator.h"
#include "components/autofill/core/browser/data_model/credit_card_art_image.h"
#include "components/autofill/core/browser/data_model/phone_number.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/geo/address_i18n.h"
#include "components/autofill/core/browser/geo/autofill_country.h"
#include "components/autofill/core/browser/geo/country_data.h"
#include "components/autofill/core/browser/geo/country_names.h"
#include "components/autofill/core/browser/geo/phone_number_i18n.h"
#include "components/autofill/core/browser/metrics/autofill_metrics.h"
#include "components/autofill/core/browser/metrics/payments/offers_metrics.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "components/autofill/core/browser/ui/autofill_image_fetcher.h"
#include "components/autofill/core/browser/ui/label_formatter.h"
#include "components/autofill/core/browser/ui/label_formatter_utils.h"
#include "components/autofill/core/browser/ui/suggestion_selection.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/history/core/browser/history_service.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/sync/driver/sync_auth_util.h"
#include "components/sync/driver/sync_service.h"
#include "components/sync/driver/sync_service_utils.h"
#include "components/version_info/version_info.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/address_data.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/address_formatter.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/source.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/storage.h"

namespace autofill {

namespace {

using ::i18n::addressinput::AddressField;
using ::i18n::addressinput::GetStreetAddressLinesAsSingleLine;
using ::i18n::addressinput::STREET_ADDRESS;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class MigrateUserOptedInWalletSyncType {
  kNotMigrated = 0,
  kMigratedFromCanonicalEmail = 1,
  kMigratedFromNonCanonicalEmail = 2,
  kNotMigratedUnexpectedPrimaryAccountIdWithEmail = 3,
  kMaxValue = kNotMigratedUnexpectedPrimaryAccountIdWithEmail,
};

template <typename T>
const T& Deref(T* x) {
  return *x;
}

template <typename T>
const T& Deref(const std::unique_ptr<T>& x) {
  return *x;
}

template <typename T>
const T& Deref(const T& x) {
  return x;
}

template <typename C, typename StringType>
typename C::const_iterator FindElementByGUID(const C& container,
                                             const StringType& guid) {
  return base::ranges::find(container, guid, [](const auto& element) {
    return Deref(element).guid();
  });
}

template <typename C, typename StringType>
bool FindByGUID(const C& container, const StringType& guid) {
  return FindElementByGUID(container, guid) != container.end();
}

template <typename C, typename T>
bool FindByContents(const C& container, const T& needle) {
  return base::ranges::any_of(container, [&needle](const auto& element) {
    return element->Compare(needle) == 0;
  });
}

// Receives the loaded profiles from the web data service and stores them in
// |*dest|. The pending handle is the address of the pending handle
// corresponding to this request type. This function is used to save both server
// and local profiles and credit cards.
template <typename ValueType>
void ReceiveLoadedDbValues(WebDataServiceBase::Handle h,
                           WDTypedResult* result,
                           WebDataServiceBase::Handle* pending_handle,
                           std::vector<std::unique_ptr<ValueType>>* dest) {
  DCHECK_EQ(*pending_handle, h);
  *pending_handle = 0;

  *dest = std::move(
      static_cast<WDResult<std::vector<std::unique_ptr<ValueType>>>*>(result)
          ->GetValue());
}

// A helper function for finding the maximum value in a string->int map.
static bool CompareVotes(const std::pair<std::string, int>& a,
                         const std::pair<std::string, int>& b) {
  return a.second < b.second;
}
}  // namespace

// Helper class to abstract the switching between account and profile storage
// for server cards away from the rest of PersonalDataManager.
class PersonalDatabaseHelper
    : public AutofillWebDataServiceObserverOnUISequence {
 public:
  explicit PersonalDatabaseHelper(PersonalDataManager* personal_data_manager)
      : personal_data_manager_(personal_data_manager) {}

  PersonalDatabaseHelper(const PersonalDatabaseHelper&) = delete;
  PersonalDatabaseHelper& operator=(const PersonalDatabaseHelper&) = delete;

  void Init(scoped_refptr<AutofillWebDataService> profile_database,
            scoped_refptr<AutofillWebDataService> account_database) {
    profile_database_ = profile_database;
    account_database_ = account_database;

    if (!profile_database_) {
      // In some tests, there are no dbs.
      return;
    }

    // Start observing the profile database. Don't observe the account database
    // until we know that we should use it.
    profile_database_->AddObserver(personal_data_manager_);

    // If we don't have an account_database , we always use the profile database
    // for server data.
    if (!account_database_) {
      server_database_ = profile_database_;
    } else {
      // Wait for the call to SetUseAccountStorageForServerData to decide
      // which database to use for server data.
      server_database_ = nullptr;
    }
  }

  ~PersonalDatabaseHelper() override {
    if (profile_database_) {
      profile_database_->RemoveObserver(personal_data_manager_);
    }

    // If we have a different server database, also remove its observer.
    if (server_database_ && server_database_ != profile_database_) {
      server_database_->RemoveObserver(personal_data_manager_);
    }
  }

  // Returns the database that should be used for storing local data.
  scoped_refptr<AutofillWebDataService> GetLocalDatabase() {
    return profile_database_;
  }

  // Returns the database that should be used for storing server data.
  scoped_refptr<AutofillWebDataService> GetServerDatabase() {
    return server_database_;
  }

  // Whether we're currently using the ephemeral account storage for saving
  // server data.
  bool IsUsingAccountStorageForServerData() {
    return server_database_ != profile_database_;
  }

  // Set whether this should use the passed in account storage for server
  // addresses. If false, this will use the profile_storage.
  // It's an error to call this if no account storage was passed in at
  // construction time.
  void SetUseAccountStorageForServerData(
      bool use_account_storage_for_server_cards) {
    if (!profile_database_) {
      // In some tests, there are no dbs.
      return;
    }
    scoped_refptr<AutofillWebDataService> new_server_database =
        use_account_storage_for_server_cards ? account_database_
                                             : profile_database_;
    DCHECK(new_server_database != nullptr)
        << "SetUseAccountStorageForServerData("
        << use_account_storage_for_server_cards << "): storage not available.";

    if (new_server_database == server_database_) {
      // Nothing to do :)
      return;
    }

    if (server_database_ != nullptr) {
      if (server_database_ != profile_database_) {
        // Remove the previous observer if we had any.
        server_database_->RemoveObserver(personal_data_manager_);
      }
      personal_data_manager_->CancelPendingServerQueries();
    }
    server_database_ = new_server_database;
    // We don't need to add an observer if server_database_ is equal to
    // profile_database_, because we're already observing that.
    if (server_database_ != profile_database_) {
      server_database_->AddObserver(personal_data_manager_);
    }
    // Notify the manager that the database changed.
    personal_data_manager_->Refresh();
  }

 private:
  scoped_refptr<AutofillWebDataService> profile_database_;
  scoped_refptr<AutofillWebDataService> account_database_;

  // The database that should be used for server data. This will always be equal
  // to either profile_database_, or account_database_.
  scoped_refptr<AutofillWebDataService> server_database_;

  raw_ptr<PersonalDataManager> personal_data_manager_;
};

PersonalDataManager::PersonalDataManager(
    const std::string& app_locale,
    const std::string& variations_country_code)
    : app_locale_(app_locale),
      variations_country_code_(variations_country_code) {
  database_helper_ = std::make_unique<PersonalDatabaseHelper>(this);
}

PersonalDataManager::PersonalDataManager(const std::string& app_locale)
    : PersonalDataManager(app_locale, std::string()) {}

void PersonalDataManager::Init(
    scoped_refptr<AutofillWebDataService> profile_database,
    scoped_refptr<AutofillWebDataService> account_database,
    PrefService* pref_service,
    PrefService* local_state,
    signin::IdentityManager* identity_manager,
    history::HistoryService* history_service,
    StrikeDatabaseBase* strike_database,
    AutofillImageFetcher* image_fetcher,
    bool is_off_the_record) {
  CountryNames::SetLocaleString(app_locale_);
  database_helper_->Init(profile_database, account_database);

  SetPrefService(pref_service);

  // Listen for the preference changes.
  pref_registrar_.Init(pref_service);

  alternative_state_name_map_updater_ =
      std::make_unique<AlternativeStateNameMapUpdater>(local_state, this);
  AddObserver(alternative_state_name_map_updater_.get());

  // Listen for URL deletions from browsing history.
  history_service_ = history_service;
  if (history_service_)
    history_service_observation_.Observe(history_service_.get());

  // Listen for account cookie deletion by the user.
  identity_manager_ = identity_manager;
  if (identity_manager_)
    identity_manager_->AddObserver(this);

  image_fetcher_ = image_fetcher;

  is_off_the_record_ = is_off_the_record;

  if (!is_off_the_record_) {
    AutofillMetrics::LogIsAutofillEnabledAtStartup(IsAutofillEnabled());
    AutofillMetrics::LogIsAutofillProfileEnabledAtStartup(
        IsAutofillProfileEnabled());
    AutofillMetrics::LogIsAutofillCreditCardEnabledAtStartup(
        IsAutofillCreditCardEnabled());
  }

  if (strike_database) {
    profile_save_strike_database_ =
        std::make_unique<AutofillProfileSaveStrikeDatabase>(strike_database);
    profile_update_strike_database_ =
        std::make_unique<AutofillProfileUpdateStrikeDatabase>(strike_database);
  }

  // WebDataService may not be available in tests.
  if (!database_helper_->GetLocalDatabase()) {
    return;
  }

  database_helper_->GetLocalDatabase()->SetAutofillProfileChangedCallback(
      base::BindRepeating(&PersonalDataManager::OnAutofillProfileChanged,
                          weak_factory_.GetWeakPtr()));

  Refresh();

  personal_data_manager_cleaner_ = std::make_unique<PersonalDataManagerCleaner>(
      this, alternative_state_name_map_updater_.get(), pref_service);
}

PersonalDataManager::~PersonalDataManager() {
  CancelPendingLocalQuery(&pending_profiles_query_);
  CancelPendingLocalQuery(&pending_creditcards_query_);
  CancelPendingLocalQuery(&pending_upi_ids_query_);
  CancelPendingServerQueries();

  if (alternative_state_name_map_updater_)
    RemoveObserver(alternative_state_name_map_updater_.get());
}

void PersonalDataManager::Shutdown() {
  if (sync_service_)
    sync_service_->RemoveObserver(this);
  sync_service_ = nullptr;

  if (history_service_)
    history_service_observation_.Reset();
  history_service_ = nullptr;

  if (identity_manager_)
    identity_manager_->RemoveObserver(this);
  identity_manager_ = nullptr;
}

void PersonalDataManager::OnSyncServiceInitialized(
    syncer::SyncService* sync_service) {
  // Before the sync service pointer gets changed, remove the observer.
  if (sync_service_)
    sync_service_->RemoveObserver(this);
  sync_service_ = sync_service;
  if (sync_service_)
    sync_service_->AddObserver(this);

  // Re-mask all server cards if the upload state is not active.
  const bool is_upload_not_active =
      syncer::GetUploadToGoogleState(sync_service_,
                                     syncer::ModelType::AUTOFILL_WALLET_DATA) ==
      syncer::UploadState::NOT_ACTIVE;
  if (is_upload_not_active)
    ResetFullServerCards();

  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableAccountWalletStorage)) {
    // Use the ephemeral account storage when the user didn't enable the sync
    // feature explicitly.
    database_helper_->SetUseAccountStorageForServerData(
        sync_service && !sync_service_->IsSyncFeatureEnabled());
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  MigrateUserOptedInWalletSyncTransportIfNeeded();
#endif
}

void PersonalDataManager::OnURLsDeleted(
    history::HistoryService* /* history_service */,
    const history::DeletionInfo& deletion_info) {
  for (PersonalDataManagerObserver& observer : observers_) {
    observer.OnBrowsingHistoryCleared(deletion_info);
  }

  if (!deletion_info.is_from_expiration() && deletion_info.IsAllHistory()) {
    AutofillDownloadManager::ClearUploadHistory(pref_service_);
  }

  if (profile_save_strike_database_) {
    if (deletion_info.IsAllHistory()) {
      // If the whole history is deleted, clear all strikes.
      profile_save_strike_database_->ClearAllStrikes();
    } else {
      std::set<std::string> deleted_hosts;
      for (const auto& url_row : deletion_info.deleted_rows()) {
        deleted_hosts.insert(url_row.url().host());
      }
      if (deletion_info.time_range().IsValid() &&
          !deletion_info.time_range().IsAllTime()) {
        profile_save_strike_database_->ClearStrikesByOriginAndTimeInternal(
            deleted_hosts, deletion_info.time_range().begin(),
            deletion_info.time_range().end());
      } else {
        profile_save_strike_database_->ClearStrikesByOrigin(deleted_hosts);
      }
    }
  }
}

void PersonalDataManager::OnWebDataServiceRequestDone(
    WebDataServiceBase::Handle h,
    std::unique_ptr<WDTypedResult> result) {
  DCHECK(pending_profiles_query_ || pending_server_profiles_query_ ||
         pending_creditcards_query_ || pending_server_creditcards_query_ ||
         pending_server_creditcard_cloud_token_data_query_ ||
         pending_ibans_query_ || pending_customer_data_query_ ||
         pending_upi_ids_query_ || pending_offer_data_query_);

  if (!result) {
    // Error from the web database.
    if (h == pending_profiles_query_)
      pending_profiles_query_ = 0;
    else if (h == pending_server_profiles_query_)
      pending_server_profiles_query_ = 0;
    else if (h == pending_creditcards_query_)
      pending_creditcards_query_ = 0;
    else if (h == pending_server_creditcards_query_)
      pending_server_creditcards_query_ = 0;
    else if (h == pending_server_creditcard_cloud_token_data_query_)
      pending_server_creditcard_cloud_token_data_query_ = 0;
    else if (h == pending_ibans_query_)
      pending_ibans_query_ = 0;
    else if (h == pending_customer_data_query_)
      pending_customer_data_query_ = 0;
    else if (h == pending_upi_ids_query_)
      pending_upi_ids_query_ = 0;
    else if (h == pending_offer_data_query_)
      pending_offer_data_query_ = 0;
  } else {
    switch (result->GetType()) {
      case AUTOFILL_PROFILES_RESULT:
        if (h == pending_profiles_query_) {
          ReceiveLoadedDbValues(h, result.get(), &pending_profiles_query_,
                                &web_profiles_);
        } else {
          DCHECK_EQ(h, pending_server_profiles_query_)
              << "received profiles from invalid request.";
          ReceiveLoadedDbValues(h, result.get(),
                                &pending_server_profiles_query_,
                                &server_profiles_);
        }
        break;
      case AUTOFILL_CREDITCARDS_RESULT:
        if (h == pending_creditcards_query_) {
          ReceiveLoadedDbValues(h, result.get(), &pending_creditcards_query_,
                                &local_credit_cards_);
        } else {
          DCHECK_EQ(h, pending_server_creditcards_query_)
              << "received creditcards from invalid request.";
          ReceiveLoadedDbValues(h, result.get(),
                                &pending_server_creditcards_query_,
                                &server_credit_cards_);
          OnServerCreditCardsRefreshed();
        }
        break;
      case AUTOFILL_CLOUDTOKEN_RESULT:
        DCHECK_EQ(h, pending_server_creditcard_cloud_token_data_query_)
            << "received credit card cloud token data from invalid request.";
        ReceiveLoadedDbValues(
            h, result.get(), &pending_server_creditcard_cloud_token_data_query_,
            &server_credit_card_cloud_token_data_);
        break;
      case AUTOFILL_IBANS_RESULT:
        DCHECK_EQ(h, pending_ibans_query_)
            << "received ibans from invalid request.";
        ReceiveLoadedDbValues(h, result.get(), &pending_ibans_query_,
                              &local_ibans_);
        break;
      case AUTOFILL_CUSTOMERDATA_RESULT:
        DCHECK_EQ(h, pending_customer_data_query_)
            << "received customer data from invalid request.";
        pending_customer_data_query_ = 0;

        payments_customer_data_ =
            static_cast<WDResult<std::unique_ptr<PaymentsCustomerData>>*>(
                result.get())
                ->GetValue();
        break;
      case AUTOFILL_UPI_RESULT:
        DCHECK_EQ(h, pending_upi_ids_query_)
            << "received UPI IDs from invalid request.";
        pending_upi_ids_query_ = 0;

        upi_ids_ =
            static_cast<WDResult<std::vector<std::string>>*>(result.get())
                ->GetValue();
        break;
      case AUTOFILL_OFFER_DATA:
        DCHECK_EQ(h, pending_offer_data_query_)
            << "received autofill offer data from invalid request.";
        ReceiveLoadedDbValues(h, result.get(), &pending_offer_data_query_,
                              &autofill_offer_data_);
        break;
      default:
        NOTREACHED();
    }
  }

  if (HasPendingQueries())
    return;

  if (!database_helper_->GetServerDatabase()) {
    DLOG(WARNING) << "There are no pending queries but the server database "
                     "wasn't set yet, so some data might be missing. Maybe "
                     "OnSyncServiceInitialized() wasn't called yet.";
    return;
  }

  // All personal data is loaded, notify observers. |is_data_loaded_| is false
  // if this is the initial load.
  if (!is_data_loaded_) {
    is_data_loaded_ = true;
    personal_data_manager_cleaner_->CleanupDataAndNotifyPersonalDataObservers();
  } else {
    NotifyPersonalDataObserver();
  }
}

void PersonalDataManager::AutofillMultipleChangedBySync() {
  // After each change coming from sync we go through a two-step process:
  //  - First, we post a task on the DB sequence to (potentially) convert server
  // addresses to local addresses and update cards accordingly.
  //  - This conversion task is concluded by a
  //  AutofillAddressConversionCompleted() notification. As a second step, we
  // need to refresh the PDM's view of the data.
  ConvertWalletAddressesAndUpdateWalletCards();
}

void PersonalDataManager::AutofillAddressConversionCompleted() {
  Refresh();
}

void PersonalDataManager::SyncStarted(syncer::ModelType model_type) {
  personal_data_manager_cleaner_->SyncStarted(model_type);
}

void PersonalDataManager::OnStateChanged(syncer::SyncService* sync_service) {
  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableAccountWalletStorage)) {
    // Use the ephemeral account storage when the user didn't enable the sync
    // feature explicitly.
    database_helper_->SetUseAccountStorageForServerData(
        !sync_service->IsSyncFeatureEnabled());
  }
}

void PersonalDataManager::OnSyncShutdown(syncer::SyncService* sync_service) {
  DCHECK_EQ(sync_service_, sync_service);
  sync_service_->RemoveObserver(this);
  sync_service_ = nullptr;
}

CoreAccountInfo PersonalDataManager::GetAccountInfoForPaymentsServer() const {
  // Return the account of the active signed-in user irrespective of whether
  // they enabled sync or not.
  return identity_manager_->GetPrimaryAccountInfo(
      signin::ConsentLevel::kSignin);
}

bool PersonalDataManager::IsSyncFeatureEnabled() const {
  return sync_service_ && sync_service_->IsSyncFeatureEnabled();
}

void PersonalDataManager::OnAccountsCookieDeletedByUserAction() {
  // Clear all the Sync Transport feature opt-ins.
  prefs::ClearSyncTransportOptIns(pref_service_);
}

AutofillSyncSigninState PersonalDataManager::GetSyncSigninState() const {
  // Check if the user is signed out.
  if (!sync_service_ || !identity_manager_ ||
      syncer::DetermineAccountToUse(identity_manager_).account_info.IsEmpty()) {
    return AutofillSyncSigninState::kSignedOut;
  }

  if (sync_service_->GetTransportState() ==
      syncer::SyncService::TransportState::PAUSED) {
    return AutofillSyncSigninState::kSyncPaused;
  }

  // Check if the user has turned on sync.
  if (sync_service_->IsSyncFeatureEnabled()) {
    return AutofillSyncSigninState::kSignedInAndSyncFeatureEnabled;
  }

  // Check if the feature is enabled and if Wallet data types are supported.
  if (base::FeatureList::IsEnabled(
          features::kAutofillEnableAccountWalletStorage) &&
      sync_service_->GetActiveDataTypes().Has(syncer::AUTOFILL_WALLET_DATA)) {
    return AutofillSyncSigninState::kSignedInAndWalletSyncTransportEnabled;
  }

  return AutofillSyncSigninState::kSignedIn;
}

void PersonalDataManager::AddObserver(PersonalDataManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void PersonalDataManager::RemoveObserver(
    PersonalDataManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void PersonalDataManager::MarkObserversInsufficientFormDataForImport() {
  for (PersonalDataManagerObserver& observer : observers_)
    observer.OnInsufficientFormData();
}

void PersonalDataManager::RecordUseOf(
    absl::variant<const AutofillProfile*, const CreditCard*>
        profile_or_credit_card) {
  if (is_off_the_record_)
    return;

  if (absl::holds_alternative<const CreditCard*>(profile_or_credit_card)) {
    CreditCard* credit_card = GetCreditCardByGUID(
        absl::get<const CreditCard*>(profile_or_credit_card)->guid());

    if (credit_card) {
      credit_card->RecordAndLogUse();

      if (credit_card->record_type() == CreditCard::LOCAL_CARD) {
        // Fail silently if there's no local database, because we need to
        // support this for tests.
        if (database_helper_->GetLocalDatabase()) {
          database_helper_->GetLocalDatabase()->UpdateCreditCard(*credit_card);
        }
      } else {
        DCHECK(database_helper_->GetServerDatabase())
            << "Recording use of server card without server storage.";
        database_helper_->GetServerDatabase()->UpdateServerCardMetadata(
            *credit_card);
      }

      Refresh();
      return;
    }
  }

  if (absl::holds_alternative<const AutofillProfile*>(profile_or_credit_card)) {
    // TODO(crbug.com/941498): Server profiles are not recorded therefore
    // GetProfileByGUID returns null for them.
    AutofillProfile* profile = GetProfileByGUID(
        absl::get<const AutofillProfile*>(profile_or_credit_card)->guid());

    if (profile) {
      profile->RecordAndLogUse();

      switch (profile->record_type()) {
        case AutofillProfile::LOCAL_PROFILE:
          UpdateProfileInDB(*profile, /*enforced=*/true);
          break;
        case AutofillProfile::SERVER_PROFILE:
          DCHECK(database_helper_->GetServerDatabase())
              << "Recording use of server address without server storage.";
          database_helper_->GetServerDatabase()->UpdateServerAddressMetadata(
              *profile);
          Refresh();
          break;
      }
    }
  }
}

void PersonalDataManager::AddUpiId(const std::string& upi_id) {
  DCHECK(!upi_id.empty());
  if (is_off_the_record_ || !database_helper_->GetLocalDatabase())
    return;

  // Don't add a duplicate.
  if (base::Contains(upi_ids_, upi_id))
    return;

  database_helper_->GetLocalDatabase()->AddUpiId(upi_id);

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

std::vector<std::string> PersonalDataManager::GetUpiIds() {
  return upi_ids_;
}

void PersonalDataManager::AddProfile(const AutofillProfile& profile) {
  if (!IsAutofillProfileEnabled())
    return;

  if (is_off_the_record_)
    return;

  if (!database_helper_->GetLocalDatabase())
    return;

  AddProfileToDB(profile);
}

void PersonalDataManager::UpdateProfile(const AutofillProfile& profile) {
  if (is_off_the_record_)
    return;

  if (!database_helper_->GetLocalDatabase())
    return;

  // If the profile is empty, remove it unconditionally.
  if (profile.IsEmpty(app_locale_)) {
    RemoveByGUID(profile.guid());
    return;
  }

  // The profile is a duplicate of an existing profile if it has a distinct GUID
  // but the same content.
  auto duplicate_profile_iter = base::ranges::find_if(
      web_profiles_, [&profile](const auto& other_profile) {
        return profile.guid() != other_profile->guid() &&
               other_profile->Compare(profile) == 0;
      });

  // Remove the profile if it is a duplicate of another already existing
  // profile.
  if (duplicate_profile_iter != web_profiles_.end()) {
    // Keep the more recently used version of the profile.
    if (profile.use_date() > duplicate_profile_iter->get()->use_date()) {
      UpdateProfileInDB(profile);
      RemoveByGUID(duplicate_profile_iter->get()->guid());
    } else {
      RemoveByGUID(profile.guid());
    }
    return;
  }

  UpdateProfileInDB(profile);
}

AutofillProfile* PersonalDataManager::GetProfileByGUID(
    const std::string& guid) {
  return GetProfileFromProfilesByGUID(guid, GetProfiles());
}

// static
AutofillProfile* PersonalDataManager::GetProfileFromProfilesByGUID(
    const std::string& guid,
    const std::vector<AutofillProfile*>& profiles) {
  auto iter = FindElementByGUID(profiles, guid);
  return iter != profiles.end() ? *iter : nullptr;
}

void PersonalDataManager::AddIBAN(const IBAN& iban) {
  if (!IsAutofillIBANEnabled())
    return;

  if (is_off_the_record_ || FindByGUID(local_ibans_, iban.guid()) ||
      !database_helper_->GetLocalDatabase() ||
      FindByContents(local_ibans_, iban)) {
    return;
  }

  // Add the new iban to the web database.
  database_helper_->GetLocalDatabase()->AddIBAN(iban);

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

void PersonalDataManager::UpdateIBAN(const IBAN& iban) {
  DCHECK_EQ(IBAN::LOCAL_IBAN, iban.record_type());
  if (is_off_the_record_) {
    return;
  }
  IBAN* existing_iban = GetIBANByGUID(iban.guid());
  if (!existing_iban) {
    return;
  }

  // Do not overwrite iban if it's existed already.
  if (existing_iban->Compare(iban) == 0) {
    return;
  }

  // Update the cached version.
  *existing_iban = iban;
  if (!database_helper_->GetLocalDatabase()) {
    return;
  }

  // Make the update.
  database_helper_->GetLocalDatabase()->UpdateIBAN(iban);

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

void PersonalDataManager::AddCreditCard(const CreditCard& credit_card) {
  if (!IsAutofillCreditCardEnabled())
    return;

  if (is_off_the_record_)
    return;

  if (credit_card.IsEmpty(app_locale_))
    return;

  if (FindByGUID(local_credit_cards_, credit_card.guid()))
    return;

  if (!database_helper_->GetLocalDatabase())
    return;

  // Don't add a duplicate.
  if (FindByContents(local_credit_cards_, credit_card))
    return;

  // Add the new credit card to the web database.
  database_helper_->GetLocalDatabase()->AddCreditCard(credit_card);

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

void PersonalDataManager::DeleteLocalCreditCards(
    const std::vector<CreditCard>& cards) {
  DCHECK(database_helper_);
  DCHECK(database_helper_->GetLocalDatabase())
      << "Use of local card without local storage.";

  for (const auto& card : cards)
    database_helper_->GetLocalDatabase()->RemoveCreditCard(card.guid());

  // Refresh the database, so latest state is reflected in all consumers.
  if (!cards.empty())
    Refresh();
}

void PersonalDataManager::UpdateCreditCard(const CreditCard& credit_card) {
  DCHECK_EQ(CreditCard::LOCAL_CARD, credit_card.record_type());
  if (is_off_the_record_)
    return;

  CreditCard* existing_credit_card = GetCreditCardByGUID(credit_card.guid());
  if (!existing_credit_card)
    return;

  // Don't overwrite the origin for a credit card that is already stored.
  if (existing_credit_card->Compare(credit_card) == 0)
    return;

  if (credit_card.IsEmpty(app_locale_)) {
    RemoveByGUID(credit_card.guid());
    return;
  }

  // Update the cached version.
  *existing_credit_card = credit_card;

  if (!database_helper_->GetLocalDatabase())
    return;

  // Make the update.
  database_helper_->GetLocalDatabase()->UpdateCreditCard(credit_card);

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

void PersonalDataManager::AddFullServerCreditCard(
    const CreditCard& credit_card) {
  DCHECK_EQ(CreditCard::FULL_SERVER_CARD, credit_card.record_type());
  DCHECK(!credit_card.IsEmpty(app_locale_));
  DCHECK(!credit_card.server_id().empty());

  if (is_off_the_record_)
    return;

  DCHECK(database_helper_->GetServerDatabase())
      << "Adding server card without server storage.";

  // Don't add a duplicate.
  if (FindByGUID(server_credit_cards_, credit_card.guid()) ||
      FindByContents(server_credit_cards_, credit_card))
    return;

  // Add the new credit card to the web database.
  database_helper_->GetServerDatabase()->AddFullServerCreditCard(credit_card);

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

void PersonalDataManager::UpdateServerCreditCard(
    const CreditCard& credit_card) {
  DCHECK_NE(CreditCard::LOCAL_CARD, credit_card.record_type());

  if (is_off_the_record_ || !database_helper_->GetServerDatabase())
    return;

  // Look up by server id, not GUID.
  const CreditCard* existing_credit_card = nullptr;
  for (const auto& server_card : server_credit_cards_) {
    if (credit_card.server_id() == server_card->server_id()) {
      existing_credit_card = server_card.get();
      break;
    }
  }
  if (!existing_credit_card)
    return;

  DCHECK_NE(existing_credit_card->record_type(), credit_card.record_type());
  DCHECK_EQ(existing_credit_card->Label(), credit_card.Label());
  if (existing_credit_card->record_type() == CreditCard::MASKED_SERVER_CARD) {
    database_helper_->GetServerDatabase()->UnmaskServerCreditCard(
        credit_card, credit_card.number());
  } else {
    database_helper_->GetServerDatabase()->MaskServerCreditCard(
        credit_card.server_id());
  }

  Refresh();
}

void PersonalDataManager::UpdateServerCardsMetadata(
    const std::vector<CreditCard>& credit_cards) {
  if (is_off_the_record_)
    return;

  DCHECK(database_helper_->GetServerDatabase())
      << "Updating server card metadata without server storage.";

  for (const auto& credit_card : credit_cards) {
    DCHECK_NE(CreditCard::LOCAL_CARD, credit_card.record_type());
    database_helper_->GetServerDatabase()->UpdateServerCardMetadata(
        credit_card);
  }

  Refresh();
}

void PersonalDataManager::ResetFullServerCard(const std::string& guid) {
  for (const auto& card : server_credit_cards_) {
    if (card->guid() == guid) {
      DCHECK_EQ(card->record_type(), CreditCard::FULL_SERVER_CARD);
      CreditCard card_copy = *card;
      card_copy.set_record_type(CreditCard::MASKED_SERVER_CARD);
      card_copy.SetNumber(card->LastFourDigits());
      UpdateServerCreditCard(card_copy);
      break;
    }
  }
}

void PersonalDataManager::ResetFullServerCards() {
  for (const auto& card : server_credit_cards_) {
    if (card->record_type() == CreditCard::FULL_SERVER_CARD) {
      CreditCard card_copy = *card;
      card_copy.set_record_type(CreditCard::MASKED_SERVER_CARD);
      card_copy.SetNumber(card->LastFourDigits());
      UpdateServerCreditCard(card_copy);
    }
  }
}

void PersonalDataManager::ClearAllServerData() {
  // This could theoretically be called before we get the data back from the
  // database on startup, and it could get called when the wallet pref is
  // off (meaning this class won't even query for the server data) so don't
  // check the server_credit_cards_/profiles_ before posting to the DB.

  // TODO(crbug.com/864519): Move this nullcheck logic to the database helper.
  // The server database can be null for a limited amount of time before the
  // sync service gets initialized. Not clearing it does not matter in that case
  // since it will not have been created yet (nothing to clear).
  if (database_helper_->GetServerDatabase())
    database_helper_->GetServerDatabase()->ClearAllServerData();

  // The above call will eventually clear our server data by notifying us
  // that the data changed and then this class will re-fetch. Preemptively
  // clear so that tests can synchronously verify that this data was cleared.
  server_credit_cards_.clear();
  server_profiles_.clear();
  payments_customer_data_.reset();
  server_credit_card_cloud_token_data_.clear();
  autofill_offer_data_.clear();
  credit_card_art_images_.clear();
}

void PersonalDataManager::ClearAllLocalData() {
  database_helper_->GetLocalDatabase()->ClearAllLocalData();
  local_credit_cards_.clear();
  web_profiles_.clear();
}

void PersonalDataManager::AddServerCreditCardForTest(
    std::unique_ptr<CreditCard> credit_card) {
  server_credit_cards_.push_back(std::move(credit_card));
}

bool PersonalDataManager::IsUsingAccountStorageForServerDataForTest() const {
  return database_helper_->IsUsingAccountStorageForServerData();
}

void PersonalDataManager::AddOfferDataForTest(
    std::unique_ptr<AutofillOfferData> offer_data) {
  autofill_offer_data_.push_back(std::move(offer_data));
}

void PersonalDataManager::
    RemoveAutofillProfileByGUIDAndBlankCreditCardReference(
        const std::string& guid) {
  RemoveProfileFromDB(guid);

  // Reset the billing_address_id of any card that refered to this profile.
  for (CreditCard* credit_card : GetCreditCards()) {
    if (credit_card->billing_address_id() == guid) {
      credit_card->set_billing_address_id("");

      if (credit_card->record_type() == CreditCard::LOCAL_CARD) {
        database_helper_->GetLocalDatabase()->UpdateCreditCard(*credit_card);
      } else {
        DCHECK(database_helper_->GetServerDatabase())
            << "Updating metadata on null server db.";
        database_helper_->GetServerDatabase()->UpdateServerCardMetadata(
            *credit_card);
      }
    }
  }
}

void PersonalDataManager::RemoveByGUID(const std::string& guid) {
  if (is_off_the_record_)
    return;

  if (!database_helper_->GetLocalDatabase())
    return;

  if (FindByGUID(local_credit_cards_, guid)) {
    database_helper_->GetLocalDatabase()->RemoveCreditCard(guid);
    // Refresh our local cache and send notifications to observers.
    Refresh();
  } else if (FindByGUID(local_ibans_, guid)) {
    database_helper_->GetLocalDatabase()->RemoveIBAN(guid);
    // Refresh our local cache and send notifications to observers.
    Refresh();
  } else {
    RemoveAutofillProfileByGUIDAndBlankCreditCardReference(guid);
  }
}

IBAN* PersonalDataManager::GetIBANByGUID(const std::string& guid) {
  const std::vector<IBAN*>& ibans = GetIBANs();
  auto iter = FindElementByGUID(ibans, guid);
  return iter != ibans.end() ? *iter : nullptr;
}

CreditCard* PersonalDataManager::GetCreditCardByGUID(const std::string& guid) {
  const std::vector<CreditCard*>& credit_cards = GetCreditCards();
  auto iter = FindElementByGUID(credit_cards, guid);
  return iter != credit_cards.end() ? *iter : nullptr;
}

CreditCard* PersonalDataManager::GetCreditCardByNumber(
    const std::string& number) {
  CreditCard numbered_card;
  numbered_card.SetNumber(base::ASCIIToUTF16(number));
  for (CreditCard* credit_card : GetCreditCards()) {
    DCHECK(credit_card);
    if (credit_card->HasSameNumberAs(numbered_card))
      return credit_card;
  }
  return nullptr;
}

CreditCard* PersonalDataManager::GetCreditCardByInstrumentId(
    int64_t instrument_id) {
  const std::vector<CreditCard*> credit_cards = GetCreditCards();
  for (CreditCard* credit_card : credit_cards) {
    if (credit_card->instrument_id() == instrument_id)
      return credit_card;
  }
  return nullptr;
}

CreditCard* PersonalDataManager::GetCreditCardByServerId(
    const std::string& server_id) {
  const std::vector<CreditCard*> server_credit_cards = GetServerCreditCards();
  for (CreditCard* credit_card : server_credit_cards) {
    if (credit_card->server_id() == server_id)
      return credit_card;
  }
  return nullptr;
}

void PersonalDataManager::GetNonEmptyTypes(
    ServerFieldTypeSet* non_empty_types) const {
  for (AutofillProfile* profile : GetProfiles())
    profile->GetNonEmptyTypes(app_locale_, non_empty_types);
  for (CreditCard* card : GetCreditCards())
    card->GetNonEmptyTypes(app_locale_, non_empty_types);
}

bool PersonalDataManager::IsDataLoaded() const {
  return is_data_loaded_;
}

std::vector<AutofillProfile*> PersonalDataManager::GetProfiles() const {
  std::vector<AutofillProfile*> result;
  result.reserve(web_profiles_.size());
  for (const auto& profile : web_profiles_)
    result.push_back(profile.get());
  return result;
}

std::vector<AutofillProfile*> PersonalDataManager::GetServerProfiles() const {
  std::vector<AutofillProfile*> result;
  if (!IsAutofillProfileEnabled())
    return result;
  result.reserve(server_profiles_.size());
  for (const auto& profile : server_profiles_)
    result.push_back(profile.get());
  return result;
}

std::vector<CreditCard*> PersonalDataManager::GetLocalCreditCards() const {
  std::vector<CreditCard*> result;
  result.reserve(local_credit_cards_.size());
  for (const auto& card : local_credit_cards_)
    result.push_back(card.get());
  return result;
}

std::vector<CreditCard*> PersonalDataManager::GetServerCreditCards() const {
  std::vector<CreditCard*> result;
  if (!IsAutofillWalletImportEnabled())
    return result;

  result.reserve(server_credit_cards_.size());
  for (const auto& card : server_credit_cards_) {
    result.push_back(card.get());
  }
  return result;
}

std::vector<CreditCard*> PersonalDataManager::GetCreditCards() const {
  std::vector<CreditCard*> result;
  result.reserve(local_credit_cards_.size() + server_credit_cards_.size());
  for (const auto& card : local_credit_cards_)
    result.push_back(card.get());
  if (IsAutofillWalletImportEnabled()) {
    for (const auto& card : server_credit_cards_) {
      result.push_back(card.get());
    }
  }
  return result;
}

std::vector<IBAN*> PersonalDataManager::GetIBANs() const {
  std::vector<IBAN*> result;
  result.reserve(local_ibans_.size());
  for (const auto& iban : local_ibans_) {
    result.push_back(iban.get());
  }
  return result;
}

PaymentsCustomerData* PersonalDataManager::GetPaymentsCustomerData() const {
  return payments_customer_data_ ? payments_customer_data_.get() : nullptr;
}

std::vector<CreditCardCloudTokenData*>
PersonalDataManager::GetCreditCardCloudTokenData() const {
  std::vector<CreditCardCloudTokenData*> result;
  if (!IsAutofillWalletImportEnabled())
    return result;

  result.reserve(server_credit_card_cloud_token_data_.size());
  for (const auto& data : server_credit_card_cloud_token_data_)
    result.push_back(data.get());
  return result;
}

std::vector<AutofillOfferData*> PersonalDataManager::GetAutofillOffers() const {
  if (!IsAutofillWalletImportEnabled() || !IsAutofillCreditCardEnabled())
    return {};

  std::vector<AutofillOfferData*> result;
  result.reserve(autofill_offer_data_.size());
  for (const auto& data : autofill_offer_data_)
    result.push_back(data.get());
  return result;
}

std::vector<const AutofillOfferData*>
PersonalDataManager::GetActiveAutofillPromoCodeOffersForOrigin(
    GURL origin) const {
  if (!IsAutofillWalletImportEnabled() || !IsAutofillCreditCardEnabled())
    return {};

  std::vector<const AutofillOfferData*> promo_code_offers_for_origin;
  base::ranges::for_each(
      autofill_offer_data_,
      [&](const std::unique_ptr<AutofillOfferData>& autofill_offer_data) {
        if (autofill_offer_data.get()->IsPromoCodeOffer() &&
            autofill_offer_data.get()->IsActiveAndEligibleForOrigin(origin)) {
          promo_code_offers_for_origin.push_back(autofill_offer_data.get());
        }
      });
  return promo_code_offers_for_origin;
}

gfx::Image* PersonalDataManager::GetCreditCardArtImageForUrl(
    const GURL& card_art_url) const {
  gfx::Image* cached_image = GetCachedCardArtImageForUrl(card_art_url);
  if (cached_image)
    return cached_image;

  FetchImagesForUrls({card_art_url});
  return nullptr;
}

gfx::Image* PersonalDataManager::GetCachedCardArtImageForUrl(
    const GURL& card_art_url) const {
  if (!IsAutofillWalletImportEnabled())
    return nullptr;

  if (!card_art_url.is_valid())
    return nullptr;

  auto images_iterator = credit_card_art_images_.find(card_art_url);

  // If the cache contains the image, return it.
  if (images_iterator != credit_card_art_images_.end()) {
    gfx::Image* image = images_iterator->second.get();
    if (!image->IsEmpty())
      return image;
  }

  // The cache does not contain the image, return nullptr.
  return nullptr;
}

void PersonalDataManager::Refresh() {
  LoadProfiles();
  LoadCreditCards();
  LoadCreditCardCloudTokenData();
  LoadIBANs();
  LoadPaymentsCustomerData();
  LoadUpiIds();
  LoadAutofillOffers();
}

std::vector<AutofillProfile*> PersonalDataManager::GetProfilesToSuggest()
    const {
  if (!IsAutofillProfileEnabled())
    return std::vector<AutofillProfile*>{};

  std::vector<AutofillProfile*> profiles = GetProfiles();

  // Rank the suggestions by ranking score.
  const base::Time comparison_time = AutofillClock::Now();
  std::sort(
      profiles.begin(), profiles.end(),
      [comparison_time](const AutofillProfile* a, const AutofillProfile* b) {
        return a->HasGreaterRankingThan(b, comparison_time);
      });

  return profiles;
}

std::vector<Suggestion> PersonalDataManager::GetProfileSuggestions(
    const AutofillType& type,
    const std::u16string& field_contents,
    bool field_is_autofilled,
    const std::vector<ServerFieldType>& field_types) {
  if (IsInAutofillSuggestionsDisabledExperiment())
    return std::vector<Suggestion>();

  const AutofillProfileComparator comparator(app_locale_);
  std::u16string field_contents_canon =
      comparator.NormalizeForComparison(field_contents);

  // Get the profiles to suggest, which are already sorted.
  std::vector<AutofillProfile*> sorted_profiles = GetProfilesToSuggest();

  // When suggesting with no prefix to match, suppress disused address
  // suggestions as well as those based on invalid profile data.
  if (field_contents_canon.empty()) {
    const base::Time min_last_used =
        AutofillClock::Now() - kDisusedDataModelTimeDelta;
    suggestion_selection::RemoveProfilesNotUsedSinceTimestamp(min_last_used,
                                                              &sorted_profiles);
  }

  std::vector<AutofillProfile*> matched_profiles;
  std::vector<Suggestion> suggestions =
      suggestion_selection::GetPrefixMatchedSuggestions(
          type, field_contents, field_contents_canon, comparator,
          field_is_autofilled, sorted_profiles, &matched_profiles);

  // Don't show two suggestions if one is a subset of the other.
  std::vector<AutofillProfile*> unique_matched_profiles;
  std::vector<Suggestion> unique_suggestions =
      suggestion_selection::GetUniqueSuggestions(
          field_types, comparator, app_locale_, matched_profiles, suggestions,
          &unique_matched_profiles);

  std::unique_ptr<LabelFormatter> formatter;
  bool use_formatter;

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
  use_formatter = base::FeatureList::IsEnabled(
      features::kAutofillUseImprovedLabelDisambiguation);
#else
  use_formatter = base::FeatureList::IsEnabled(
      features::kAutofillUseMobileLabelDisambiguation);
#endif  // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)

  // The formatter stores a constant reference to |unique_matched_profiles|.
  // This is safe since the formatter is destroyed when this function returns.
  formatter = use_formatter
                  ? LabelFormatter::Create(unique_matched_profiles, app_locale_,
                                           type.GetStorableType(), field_types)
                  : nullptr;

  // Generate disambiguating labels based on the list of matches.
  std::vector<std::u16string> labels;
  if (formatter) {
    labels = formatter->GetLabels();
  } else {
    AutofillProfile::CreateInferredLabels(unique_matched_profiles, &field_types,
                                          type.GetStorableType(), 1,
                                          app_locale_, &labels);
  }

  if (use_formatter && !unique_suggestions.empty()) {
    AutofillMetrics::LogProfileSuggestionsMadeWithFormatter(formatter !=
                                                            nullptr);
  }

  suggestion_selection::PrepareSuggestions(labels, &unique_suggestions,
                                           comparator);

  // If this feature is enabled, we add an icon to the address (profile)
  // suggestion if there is more than on profile related field in the form.
  if (base::FeatureList::IsEnabled(
          features::kAutofillUseConsistentPopupSettingsIcons)) {
    // Returns true if |type| is related to address profiles.
    auto is_field_type_profile_related = [](ServerFieldType type) {
      FieldTypeGroup group = AutofillType(type).group();
      return group == FieldTypeGroup::kName ||
             group == FieldTypeGroup::kAddressHome ||
             group == FieldTypeGroup::kPhoneHome ||
             group == FieldTypeGroup::kEmail;
    };

    if (base::ranges::count_if(field_types, is_field_type_profile_related) >
        1) {
      for (auto& suggestion : unique_suggestions) {
        suggestion.icon = "accountIcon";
      }
    }
  }

  return unique_suggestions;
}

const std::vector<CreditCard*> PersonalDataManager::GetCreditCardsToSuggest(
    bool include_server_cards) const {
  if (!IsAutofillCreditCardEnabled())
    return std::vector<CreditCard*>{};

  std::vector<CreditCard*> credit_cards;
  if (include_server_cards && ShouldSuggestServerCards()) {
    credit_cards = GetCreditCards();
  } else {
    credit_cards = GetLocalCreditCards();
  }

  std::list<CreditCard*> cards_to_dedupe(credit_cards.begin(),
                                         credit_cards.end());

  DedupeCreditCardToSuggest(&cards_to_dedupe);

  std::vector<CreditCard*> cards_to_suggest(
      std::make_move_iterator(std::begin(cards_to_dedupe)),
      std::make_move_iterator(std::end(cards_to_dedupe)));

  // Rank the cards by ranking score (see AutofillDataModel for details). All
  // expired cards should be suggested last, also by ranking score.
  base::Time comparison_time = AutofillClock::Now();
  std::stable_sort(cards_to_suggest.begin(), cards_to_suggest.end(),
                   [comparison_time](const CreditCard* a, const CreditCard* b) {
                     bool a_is_expired = a->IsExpired(comparison_time);
                     if (a_is_expired != b->IsExpired(comparison_time))
                       return !a_is_expired;

                     return a->HasGreaterRankingThan(b, comparison_time);
                   });

  return cards_to_suggest;
}

bool PersonalDataManager::IsAutofillEnabled() const {
  return IsAutofillProfileEnabled() || IsAutofillCreditCardEnabled() ||
         IsAutofillIBANEnabled();
}

bool PersonalDataManager::IsAutofillProfileEnabled() const {
  return prefs::IsAutofillProfileEnabled(pref_service_);
}

bool PersonalDataManager::IsAutofillCreditCardEnabled() const {
  return prefs::IsAutofillCreditCardEnabled(pref_service_);
}

bool PersonalDataManager::IsAutofillIBANEnabled() const {
  return prefs::IsAutofillIBANEnabled(pref_service_);
}

bool PersonalDataManager::IsAutofillWalletImportEnabled() const {
  return prefs::IsPaymentsIntegrationEnabled(pref_service_);
}

bool PersonalDataManager::ShouldSuggestServerCards() const {
  if (!IsAutofillWalletImportEnabled())
    return false;

  if (is_syncing_for_test_)
    return true;

  if (!sync_service_)
    return false;

  // Check if the user is in sync transport mode for wallet data.
  if (!sync_service_->IsSyncFeatureEnabled() &&
      base::FeatureList::IsEnabled(
          features::kAutofillEnableAccountWalletStorage)) {
    // For SyncTransport, only show server cards if the user has opted in to
    // seeing them in the dropdown.
    if (!prefs::IsUserOptedInWalletSyncTransport(
            pref_service_, sync_service_->GetAccountInfo().account_id)) {
      return false;
    }
  }

  // Server cards should be suggested if the sync service is active.
  return sync_service_->GetActiveDataTypes().Has(syncer::AUTOFILL_WALLET_DATA);
}

std::string PersonalDataManager::CountryCodeForCurrentTimezone() const {
  return base::CountryCodeForCurrentTimezone();
}

void PersonalDataManager::SetPrefService(PrefService* pref_service) {
  wallet_enabled_pref_ = std::make_unique<BooleanPrefMember>();
  profile_enabled_pref_ = std::make_unique<BooleanPrefMember>();
  credit_card_enabled_pref_ = std::make_unique<BooleanPrefMember>();
  pref_service_ = pref_service;
  // |pref_service_| can be nullptr in tests. Using base::Unretained(this) is
  // safe because observer instances are destroyed once |this| is destroyed.
  if (pref_service_) {
    credit_card_enabled_pref_->Init(
        prefs::kAutofillCreditCardEnabled, pref_service_,
        base::BindRepeating(&PersonalDataManager::EnableAutofillPrefChanged,
                            base::Unretained(this)));
    profile_enabled_pref_->Init(
        prefs::kAutofillProfileEnabled, pref_service_,
        base::BindRepeating(&PersonalDataManager::EnableAutofillPrefChanged,
                            base::Unretained(this)));
    wallet_enabled_pref_->Init(
        prefs::kAutofillWalletImportEnabled, pref_service_,
        base::BindRepeating(
            &PersonalDataManager::EnableWalletIntegrationPrefChanged,
            base::Unretained(this)));
  }
}

void PersonalDataManager::FetchImagesForUrls(
    const std::vector<GURL>& updated_urls) const {
  if (!image_fetcher_)
    return;

  image_fetcher_->FetchImagesForUrls(
      updated_urls, base::BindOnce(&PersonalDataManager::OnCardArtImagesFetched,
                                   weak_factory_.GetWeakPtr()));
}

const std::string& PersonalDataManager::GetDefaultCountryCodeForNewAddress()
    const {
  if (default_country_code_.empty())
    default_country_code_ = MostCommonCountryCodeFromProfiles();

  // Failing that, use the country code determined for experiment groups.
  if (default_country_code_.empty())
    default_country_code_ = GetCountryCodeForExperimentGroup();

  return default_country_code_;
}

const std::string& PersonalDataManager::GetCountryCodeForExperimentGroup()
    const {
  // Set to |variations_country_code_| if it exists.
  if (experiment_country_code_.empty())
    experiment_country_code_ = variations_country_code_;

  // Failing that, guess based on system timezone.
  if (experiment_country_code_.empty())
    experiment_country_code_ = CountryCodeForCurrentTimezone();

  // Failing that, guess based on locale. This returns "US" if there is no good
  // guess.
  if (experiment_country_code_.empty()) {
    experiment_country_code_ =
        AutofillCountry::CountryCodeForLocale(app_locale());
  }

  return experiment_country_code_;
}

// static
void PersonalDataManager::DedupeCreditCardToSuggest(
    std::list<CreditCard*>* cards_to_suggest) {
  for (auto outer_it = cards_to_suggest->begin();
       outer_it != cards_to_suggest->end(); ++outer_it) {
    // If considering a full server card, look for local cards that are
    // duplicates of it and remove them.
    if ((*outer_it)->record_type() == CreditCard::FULL_SERVER_CARD) {
      for (auto inner_it = cards_to_suggest->begin();
           inner_it != cards_to_suggest->end();) {
        auto inner_it_copy = inner_it++;
        if ((*inner_it_copy)->IsLocalDuplicateOfServerCard(**outer_it))
          cards_to_suggest->erase(inner_it_copy);
      }
      // If considering a local card, look for masked server cards that are
      // duplicates of it and remove them.
    } else if ((*outer_it)->record_type() == CreditCard::LOCAL_CARD) {
      for (auto inner_it = cards_to_suggest->begin();
           inner_it != cards_to_suggest->end();) {
        auto inner_it_copy = inner_it++;
        if ((*inner_it_copy)->record_type() == CreditCard::MASKED_SERVER_CARD &&
            (*outer_it)->IsLocalDuplicateOfServerCard(**inner_it_copy)) {
          cards_to_suggest->erase(inner_it_copy);
        }
      }
    }
  }
}

void PersonalDataManager::SetProfiles(std::vector<AutofillProfile>* profiles) {
  if (is_off_the_record_) {
    // TODO(crbug.com/997629): Remove after investigation is over.
    DLOG(WARNING) << "Cannot SetProfiles because off-the-record";
    return;
  }
  if (!database_helper_->GetLocalDatabase()) {
    // TODO(crbug.com/997629): Remove after investigation is over.
    DLOG(WARNING) << "Cannot SetProfiles because no local DB";
    return;
  }

  ClearOnGoingProfileChanges();

  // Means that a profile was added, removed or updated.
  bool change_happened = false;

  // Any profiles that are not in the new profile list should be removed from
  // the web database
  for (const auto& it : web_profiles_) {
    if (!FindByGUID(*profiles, it->guid())) {
      RemoveProfileFromDB(it->guid());
      change_happened = true;
    }
  }

  // Update the web database with the new and existing profiles.
  for (const AutofillProfile& it : *profiles) {
    const auto* existing_profile = GetProfileByGUID(it.guid());
    // In SetProfiles, exceptionally, profiles are directly added/updated on the
    // web_profiles_ before they are ready to be added or get updated in the
    // database. Enforce the changes to make sure the database is also updated.
    if (existing_profile) {
      if (!existing_profile->EqualsForUpdatePurposes(it)) {
        UpdateProfileInDB(it, /*enforced=*/true);
        change_happened = true;
      }
    } else if (!FindByContents(web_profiles_, it)) {
      AddProfileToDB(it, /*enforced=*/true);
      change_happened = true;
    }
  }

  if (change_happened) {
    // Copy in the new profiles.
    web_profiles_.clear();
    for (const AutofillProfile& it : *profiles) {
      web_profiles_.push_back(std::make_unique<AutofillProfile>(it));
    }
  } else {
    // When a change happens (add, update, remove), we would consequently call
    // the NotifyPersonalDataChanged which notifies the tests to stop waiting.
    // Otherwise, we need to stop them by calling the function directly.
    NotifyPersonalDataObserver();
  }
}

bool PersonalDataManager::IsNewProfileImportBlockedForDomain(
    const GURL& url) const {
  if (!GetProfileSaveStrikeDatabase() || !url.is_valid() || !url.has_host() ||
      !features::kAutofillAutoBlockSaveAddressProfilePrompt.Get()) {
    return false;
  }

  return GetProfileSaveStrikeDatabase()->ShouldBlockFeature(url.host());
}

void PersonalDataManager::AddStrikeToBlockNewProfileImportForDomain(
    const GURL& url) {
  if (!GetProfileSaveStrikeDatabase() || !url.is_valid() || !url.has_host() ||
      !features::kAutofillAutoBlockSaveAddressProfilePrompt.Get()) {
    return;
  }
  GetProfileSaveStrikeDatabase()->AddStrike(url.host());
}

void PersonalDataManager::RemoveStrikesToBlockNewProfileImportForDomain(
    const GURL& url) {
  if (!GetProfileSaveStrikeDatabase() || !url.is_valid() || !url.has_host()) {
    return;
  }
  GetProfileSaveStrikeDatabase()->ClearStrikes(url.host());
}

bool PersonalDataManager::IsProfileUpdateBlocked(
    const std::string& guid) const {
  if (!GetProfileUpdateStrikeDatabase() ||
      !features::kAutofillAutoBlockUpdateAddressProfilePrompt.Get()) {
    return false;
  }

  return GetProfileUpdateStrikeDatabase()->ShouldBlockFeature(guid);
}

void PersonalDataManager::AddStrikeToBlockProfileUpdate(
    const std::string& guid) {
  if (!GetProfileUpdateStrikeDatabase() ||
      !features::kAutofillAutoBlockUpdateAddressProfilePrompt.Get()) {
    return;
  }
  GetProfileUpdateStrikeDatabase()->AddStrike(guid);
}

void PersonalDataManager::RemoveStrikesToBlockProfileUpdate(
    const std::string& guid) {
  if (!GetProfileUpdateStrikeDatabase()) {
    return;
  }
  GetProfileUpdateStrikeDatabase()->ClearStrikes(guid);
}

AutofillProfileSaveStrikeDatabase*
PersonalDataManager::GetProfileSaveStrikeDatabase() {
  return const_cast<AutofillProfileSaveStrikeDatabase*>(
      base::as_const(*this).GetProfileSaveStrikeDatabase());
}

const AutofillProfileSaveStrikeDatabase*
PersonalDataManager::GetProfileSaveStrikeDatabase() const {
  return profile_save_strike_database_.get();
}

AutofillProfileUpdateStrikeDatabase*
PersonalDataManager::GetProfileUpdateStrikeDatabase() {
  return const_cast<AutofillProfileUpdateStrikeDatabase*>(
      base::as_const(*this).GetProfileUpdateStrikeDatabase());
}

const AutofillProfileUpdateStrikeDatabase*
PersonalDataManager::GetProfileUpdateStrikeDatabase() const {
  return profile_update_strike_database_.get();
}

void PersonalDataManager::SetCreditCards(
    std::vector<CreditCard>* credit_cards) {
  if (is_off_the_record_)
    return;

  // Remove empty credit cards from input.
  base::EraseIf(*credit_cards, [this](const CreditCard& credit_card) {
    return credit_card.IsEmpty(app_locale_);
  });

  if (!database_helper_->GetLocalDatabase())
    return;

  // Any credit cards that are not in the new credit card list should be
  // removed.
  for (const auto& card : local_credit_cards_) {
    if (!FindByGUID(*credit_cards, card->guid()))
      database_helper_->GetLocalDatabase()->RemoveCreditCard(card->guid());
  }

  // Update the web database with the existing credit cards.
  for (const CreditCard& card : *credit_cards) {
    if (FindByGUID(local_credit_cards_, card.guid()))
      database_helper_->GetLocalDatabase()->UpdateCreditCard(card);
  }

  // Add the new credit cards to the web database.  Don't add a duplicate.
  for (const CreditCard& card : *credit_cards) {
    if (!FindByGUID(local_credit_cards_, card.guid()) &&
        !FindByContents(local_credit_cards_, card))
      database_helper_->GetLocalDatabase()->AddCreditCard(card);
  }

  // Copy in the new credit cards.
  local_credit_cards_.clear();
  for (const CreditCard& card : *credit_cards)
    local_credit_cards_.push_back(std::make_unique<CreditCard>(card));

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

void PersonalDataManager::LoadProfiles() {
  if (!database_helper_->GetLocalDatabase()) {
    NOTREACHED();
    return;
  }

  CancelPendingLocalQuery(&pending_profiles_query_);
  CancelPendingServerQuery(&pending_server_profiles_query_);

  pending_profiles_query_ =
      database_helper_->GetLocalDatabase()->GetAutofillProfiles(this);
  if (database_helper_->GetServerDatabase()) {
    pending_server_profiles_query_ =
        database_helper_->GetServerDatabase()->GetServerProfiles(this);
  }
}

void PersonalDataManager::LoadCreditCards() {
  if (!database_helper_->GetLocalDatabase()) {
    NOTREACHED();
    return;
  }

  CancelPendingLocalQuery(&pending_creditcards_query_);
  CancelPendingServerQuery(&pending_server_creditcards_query_);

  pending_creditcards_query_ =
      database_helper_->GetLocalDatabase()->GetCreditCards(this);
  if (database_helper_->GetServerDatabase()) {
    pending_server_creditcards_query_ =
        database_helper_->GetServerDatabase()->GetServerCreditCards(this);
  }
}

void PersonalDataManager::LoadCreditCardCloudTokenData() {
  if (!database_helper_->GetServerDatabase())
    return;

  CancelPendingServerQuery(&pending_server_creditcard_cloud_token_data_query_);

  pending_server_creditcard_cloud_token_data_query_ =
      database_helper_->GetServerDatabase()->GetCreditCardCloudTokenData(this);
}

void PersonalDataManager::LoadIBANs() {
  if (!database_helper_->GetLocalDatabase()) {
    NOTREACHED();
    return;
  }

  CancelPendingLocalQuery(&pending_ibans_query_);

  pending_ibans_query_ = database_helper_->GetLocalDatabase()->GetIBANs(this);
}

void PersonalDataManager::LoadUpiIds() {
  if (!database_helper_->GetLocalDatabase()) {
    NOTREACHED();
    return;
  }

  CancelPendingLocalQuery(&pending_upi_ids_query_);

  pending_upi_ids_query_ =
      database_helper_->GetLocalDatabase()->GetAllUpiIds(this);
}

void PersonalDataManager::LoadAutofillOffers() {
  if (!database_helper_->GetServerDatabase())
    return;

  CancelPendingServerQuery(&pending_offer_data_query_);

  pending_offer_data_query_ =
      database_helper_->GetServerDatabase()->GetAutofillOffers(this);
}

void PersonalDataManager::CancelPendingLocalQuery(
    WebDataServiceBase::Handle* handle) {
  if (*handle) {
    if (!database_helper_->GetLocalDatabase()) {
      NOTREACHED();
      return;
    }
    database_helper_->GetLocalDatabase()->CancelRequest(*handle);
  }
  *handle = 0;
}

void PersonalDataManager::CancelPendingServerQuery(
    WebDataServiceBase::Handle* handle) {
  if (*handle) {
    if (!database_helper_->GetServerDatabase()) {
      NOTREACHED();
      return;
    }
    database_helper_->GetServerDatabase()->CancelRequest(*handle);
  }
  *handle = 0;
}

void PersonalDataManager::CancelPendingServerQueries() {
  CancelPendingServerQuery(&pending_server_profiles_query_);
  CancelPendingServerQuery(&pending_server_creditcards_query_);
  CancelPendingServerQuery(&pending_customer_data_query_);
  CancelPendingServerQuery(&pending_server_creditcard_cloud_token_data_query_);
  CancelPendingServerQuery(&pending_offer_data_query_);
}

void PersonalDataManager::LoadPaymentsCustomerData() {
  if (!database_helper_->GetServerDatabase())
    return;

  CancelPendingServerQuery(&pending_customer_data_query_);

  pending_customer_data_query_ =
      database_helper_->GetServerDatabase()->GetPaymentsCustomerData(this);
}

std::string PersonalDataManager::SaveImportedProfile(
    const AutofillProfile& imported_profile) {
  if (is_off_the_record_)
    return std::string();

  std::vector<AutofillProfile> profiles;
  std::string guid = AutofillProfileComparator::MergeProfile(
      imported_profile, web_profiles_, app_locale_, &profiles);
  SetProfiles(&profiles);
  return guid;
}

std::string PersonalDataManager::OnAcceptedLocalCreditCardSave(
    const CreditCard& imported_card) {
  DCHECK(!imported_card.number().empty());
  if (is_off_the_record_)
    return std::string();

  return SaveImportedCreditCard(imported_card);
}

std::string PersonalDataManager::SaveImportedCreditCard(
    const CreditCard& imported_card) {
  // Set to true if |imported_card| is merged into the credit card list.
  bool merged = false;

  std::string guid = imported_card.guid();
  std::vector<CreditCard> credit_cards;
  for (auto& card : local_credit_cards_) {
    // If |imported_card| has not yet been merged, check whether it should be
    // with the current |card|.
    if (!merged && card->UpdateFromImportedCard(imported_card, app_locale_)) {
      guid = card->guid();
      merged = true;
    }

    credit_cards.push_back(*card);
  }

  if (!merged)
    credit_cards.push_back(imported_card);

  SetCreditCards(&credit_cards);

  // After a card is saved locally, notifies the observers.
  OnCreditCardSaved(/*is_local_card=*/true);

  return guid;
}

void PersonalDataManager::LogStoredProfileMetrics() const {
  if (!has_logged_stored_profile_metrics_) {
    // Update the histogram of how many addresses the user has stored.
    AutofillMetrics::LogStoredProfileCount(web_profiles_.size());

    // If the user has stored addresses, log the distribution of days since
    // their last use and how many would be considered disused.
    // Additionally, track the number of profiles without a country.
    if (!web_profiles_.empty()) {
      size_t num_disused_profiles = 0;
      size_t num_profiles_without_country = 0;
      const base::Time now = AutofillClock::Now();
      for (const std::unique_ptr<AutofillProfile>& profile : web_profiles_) {
        const base::TimeDelta time_since_last_use = now - profile->use_date();
        AutofillMetrics::LogStoredProfileDaysSinceLastUse(
            time_since_last_use.InDays());
        if (time_since_last_use > kDisusedDataModelTimeDelta)
          ++num_disused_profiles;
        if (profile->GetRawInfo(ADDRESS_HOME_COUNTRY).empty())
          ++num_profiles_without_country;
      }
      AutofillMetrics::LogStoredProfileDisusedCount(num_disused_profiles);
      AutofillMetrics::LogStoredProfilesWithoutCountry(
          num_profiles_without_country);
    }

    // Only log this info once per chrome user profile load.
    has_logged_stored_profile_metrics_ = true;
  }
}

void PersonalDataManager::LogStoredCreditCardMetrics() const {
  if (!has_logged_stored_credit_card_metrics_) {
    AutofillMetrics::LogStoredCreditCardMetrics(
        local_credit_cards_, server_credit_cards_,
        GetServerCardWithArtImageCount(), kDisusedDataModelTimeDelta);

    // Only log this info once per chrome user profile load.
    has_logged_stored_credit_card_metrics_ = true;
  }
}

void PersonalDataManager::LogStoredOfferMetrics() const {
  if (!has_logged_stored_offer_metrics_) {
    autofill_metrics::LogStoredOfferMetrics(autofill_offer_data_);
    // Only log this info once per chrome user profile load.
    has_logged_stored_offer_metrics_ = true;
  }
}

std::string PersonalDataManager::MostCommonCountryCodeFromProfiles() const {
  if (!IsAutofillEnabled())
    return std::string();

  // Count up country codes from existing profiles.
  std::map<std::string, int> votes;
  // TODO(estade): can we make this GetProfiles() instead? It seems to cause
  // errors in tests on mac trybots. See http://crbug.com/57221
  const std::vector<AutofillProfile*>& profiles = GetProfiles();
  const std::vector<std::string>& country_codes =
      CountryDataMap::GetInstance()->country_codes();
  for (auto* profile : profiles) {
    std::string country_code = base::ToUpperASCII(
        base::UTF16ToASCII(profile->GetRawInfo(ADDRESS_HOME_COUNTRY)));

    if (base::Contains(country_codes, country_code)) {
      // Verified profiles count 100x more than unverified ones.
      votes[country_code] += profile->IsVerified() ? 100 : 1;
    }
  }

  // Take the most common country code.
  if (!votes.empty()) {
    auto iter = std::max_element(votes.begin(), votes.end(), CompareVotes);
    return iter->first;
  }

  return std::string();
}

void PersonalDataManager::EnableWalletIntegrationPrefChanged() {
  if (!prefs::IsPaymentsIntegrationEnabled(pref_service_)) {
    // Re-mask all server cards when the user turns off wallet card
    // integration.
    ResetFullServerCards();
    NotifyPersonalDataObserver();
  }
}

void PersonalDataManager::EnableAutofillPrefChanged() {
  default_country_code_.clear();

  // Refresh our local cache and send notifications to observers.
  Refresh();
}

bool PersonalDataManager::IsKnownCard(const CreditCard& credit_card) const {
  const auto stripped_pan = CreditCard::StripSeparators(credit_card.number());
  for (const auto& card : local_credit_cards_) {
    if (stripped_pan == CreditCard::StripSeparators(card->number()))
      return true;
  }

  const auto masked_info = credit_card.NetworkAndLastFourDigits();
  for (const auto& card : server_credit_cards_) {
    switch (card->record_type()) {
      case CreditCard::FULL_SERVER_CARD:
        if (stripped_pan == CreditCard::StripSeparators(card->number()))
          return true;
        break;
      case CreditCard::MASKED_SERVER_CARD:
        if (masked_info == card->NetworkAndLastFourDigits())
          return true;
        break;
      default:
        NOTREACHED();
    }
  }

  return false;
}

bool PersonalDataManager::IsServerCard(const CreditCard* credit_card) const {
  // Check whether the current card itself is a server card.
  if (credit_card->record_type() != CreditCard::LOCAL_CARD)
    return true;

  std::vector<CreditCard*> server_credit_cards = GetServerCreditCards();
  // Check whether the current card is already uploaded.
  for (const CreditCard* server_card : server_credit_cards) {
    if (credit_card->HasSameNumberAs(*server_card))
      return true;
  }
  return false;
}

bool PersonalDataManager::ShouldShowCardsFromAccountOption() const {
// The feature is only for Linux, Windows, Mac, and Fuchsia.
// TODO(crbug.com/1052397): Revisit the macro expression once build flag switch
// of lacros-chrome is complete.
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS) || \
    BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA)
  // This option should only be shown for users that have not enabled the Sync
  // Feature and that have server credit cards available.
  if (!sync_service_ || sync_service_->IsSyncFeatureEnabled() ||
      GetServerCreditCards().empty()) {
    return false;
  }

  // If we have not returned yet, it should mean that the user is in Sync
  // Transport mode for Wallet data (Sync Feature disabled but has server
  // cards). This should only happen if that feature is enabled.
  DCHECK(base::FeatureList::IsEnabled(
      features::kAutofillEnableAccountWalletStorage));

  bool is_opted_in = prefs::IsUserOptedInWalletSyncTransport(
      pref_service_, sync_service_->GetAccountInfo().account_id);

  // The option should only be shown if the user has not already opted-in.
  return !is_opted_in;
#else
  return false;
#endif  // #if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS) ||
        // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA)
}

void PersonalDataManager::OnUserAcceptedCardsFromAccountOption() {
  DCHECK_EQ(AutofillSyncSigninState::kSignedInAndWalletSyncTransportEnabled,
            GetSyncSigninState());
  prefs::SetUserOptedInWalletSyncTransport(
      pref_service_, sync_service_->GetAccountInfo().account_id,
      /*opted_in=*/true);
}

void PersonalDataManager::OnAutofillProfileChanged(
    const AutofillProfileDeepChange& change) {
  const auto& guid = change.key();
  const auto& change_type = change.type();
  const auto& profile = *(change.profile());
  DCHECK(guid == profile.guid());
  // Happens only in tests.
  if (!ProfileChangesAreOngoing(guid)) {
    DVLOG(1) << "Received an unexpected response from database.";
    return;
  }

  const auto* existing_profile = GetProfileByGUID(guid);
  const bool profile_exists = (existing_profile != nullptr);
  switch (change_type) {
    case AutofillProfileChange::ADD:
      if (!profile_exists && !FindByContents(web_profiles_, profile)) {
        web_profiles_.push_back(std::make_unique<AutofillProfile>(profile));
      }
      break;
    case AutofillProfileChange::UPDATE:
      if (profile_exists &&
          (change.enforced() ||
           !existing_profile->EqualsForUpdatePurposes(profile))) {
        web_profiles_.erase(FindElementByGUID(web_profiles_, guid));
        web_profiles_.push_back(std::make_unique<AutofillProfile>(profile));
      }
      break;
    case AutofillProfileChange::REMOVE:
      if (profile_exists) {
        web_profiles_.erase(FindElementByGUID(web_profiles_, guid));
      }
      break;
    default:
      NOTREACHED();
  }

  OnProfileChangeDone(guid);
}

void PersonalDataManager::OnCardArtImagesFetched(
    std::vector<std::unique_ptr<CreditCardArtImage>> art_images) {
  for (auto& art_image : art_images) {
    if (!art_image->card_art_image.IsEmpty()) {
      credit_card_art_images_[art_image->card_art_url] =
          std::make_unique<gfx::Image>(art_image->card_art_image);
    }
  }
}

void PersonalDataManager::LogServerCardLinkClicked() const {
  AutofillMetrics::LogServerCardLinkClicked(GetSyncSigninState());
}

void PersonalDataManager::OnUserAcceptedUpstreamOffer() {
  // If the user is in sync transport mode for Wallet, record an opt-in.
  if (GetSyncSigninState() ==
      AutofillSyncSigninState::kSignedInAndWalletSyncTransportEnabled) {
    prefs::SetUserOptedInWalletSyncTransport(
        pref_service_, sync_service_->GetAccountInfo().account_id,
        /*opted_in=*/true);
  }
}

void PersonalDataManager::NotifyPersonalDataObserver() {
  bool profile_changes_are_ongoing = ProfileChangesAreOngoing();
  for (PersonalDataManagerObserver& observer : observers_) {
    observer.OnPersonalDataChanged();
  }
  if (!profile_changes_are_ongoing) {
    // Call OnPersonalDataFinishedProfileTasks in a separate loop as
    // the observers might have removed themselves in OnPersonalDataChanged
    for (PersonalDataManagerObserver& observer : observers_) {
      observer.OnPersonalDataFinishedProfileTasks();
    }
  }
}

void PersonalDataManager::OnCreditCardSaved(bool is_local_card) {}

void PersonalDataManager::ConvertWalletAddressesAndUpdateWalletCards() {
  // If the full Sync feature isn't enabled, then do NOT convert any Wallet
  // addresses to local ones.
  if (!IsSyncFeatureEnabled()) {
    // PDM expects that each call to
    // ConvertWalletAddressesAndUpdateWalletCards() is followed by a
    // AutofillAddressConversionCompleted() notification, simulate the
    // notification here.
    AutofillAddressConversionCompleted();
    return;
  }

  database_helper_->GetServerDatabase()
      ->ConvertWalletAddressesAndUpdateWalletCards(
          app_locale_, GetAccountInfoForPaymentsServer().email);
}

void PersonalDataManager::AddProfileToDB(const AutofillProfile& profile,
                                         bool enforced) {
  if (profile.IsEmpty(app_locale_)) {
    NotifyPersonalDataObserver();
    return;
  }

  if (!ProfileChangesAreOngoing(profile.guid())) {
    if (!enforced && (FindByGUID(web_profiles_, profile.guid()) ||
                      FindByContents(web_profiles_, profile))) {
      NotifyPersonalDataObserver();
      return;
    }
  }
  ongoing_profile_changes_[profile.guid()].push_back(
      AutofillProfileDeepChange(AutofillProfileChange::ADD, profile));
  if (enforced)
    ongoing_profile_changes_[profile.guid()].back().set_enforced();
  HandleNextProfileChange(profile.guid());
}

void PersonalDataManager::UpdateProfileInDB(const AutofillProfile& profile,
                                            bool enforced) {
  // if the update is enforced, don't check if a similar profile already exists
  // or not. Otherwise, check if updating the profile makes sense.
  if (!enforced && !ProfileChangesAreOngoing(profile.guid())) {
    const auto* existing_profile = GetProfileByGUID(profile.guid());
    bool profile_exists = (existing_profile != nullptr);
    if (!profile_exists || existing_profile->EqualsForUpdatePurposes(profile)) {
      NotifyPersonalDataObserver();
      return;
    }
  }

  ongoing_profile_changes_[profile.guid()].push_back(
      AutofillProfileDeepChange(AutofillProfileChange::UPDATE, profile));
  if (enforced)
    ongoing_profile_changes_[profile.guid()].back().set_enforced();
  HandleNextProfileChange(profile.guid());
}

void PersonalDataManager::RemoveProfileFromDB(const std::string& guid) {
  auto profile_it = FindElementByGUID(web_profiles_, guid);
  bool profile_exists = profile_it != web_profiles_.end();
  if (!profile_exists && !ProfileChangesAreOngoing(guid)) {
    NotifyPersonalDataObserver();
    return;
  }
  const AutofillProfile* profile =
      profile_exists ? profile_it->get()
                     : ongoing_profile_changes_[guid].back().profile();
  AutofillProfileDeepChange change(AutofillProfileChange::REMOVE, *profile);
  if (!ProfileChangesAreOngoing(guid)) {
    database_helper_->GetLocalDatabase()->RemoveAutofillProfile(guid);
    change.set_is_ongoing_on_background();
  }
  ongoing_profile_changes_[guid].push_back(std::move(change));
}

void PersonalDataManager::HandleNextProfileChange(const std::string& guid) {
  if (!ProfileChangesAreOngoing(guid))
    return;

  const auto& change = ongoing_profile_changes_[guid].front();
  if (change.is_ongoing_on_background())
    return;

  const auto& change_type = change.type();
  const auto* existing_profile = GetProfileByGUID(guid);
  const bool profile_exists = (existing_profile != nullptr);
  const auto& profile = *(ongoing_profile_changes_[guid].front().profile());

  DCHECK(guid == profile.guid());

  if (change_type == AutofillProfileChange::REMOVE) {
    if (!profile_exists) {
      OnProfileChangeDone(guid);
      return;
    }
    database_helper_->GetLocalDatabase()->RemoveAutofillProfile(guid);
    change.set_is_ongoing_on_background();
    return;
  }

  if (change_type == AutofillProfileChange::ADD) {
    if (!change.enforced() &&
        (profile_exists || FindByContents(web_profiles_, profile))) {
      OnProfileChangeDone(guid);
      return;
    }
    database_helper_->GetLocalDatabase()->AddAutofillProfile(profile);
    change.set_is_ongoing_on_background();
    return;
  }

  if (profile_exists && (change.enforced() ||
                         !existing_profile->EqualsForUpdatePurposes(profile))) {
    database_helper_->GetLocalDatabase()->UpdateAutofillProfile(profile);
    change.set_is_ongoing_on_background();
  } else {
    OnProfileChangeDone(guid);
  }
}

bool PersonalDataManager::ProfileChangesAreOngoing(const std::string& guid) {
  return ongoing_profile_changes_.find(guid) !=
             ongoing_profile_changes_.end() &&
         !ongoing_profile_changes_[guid].empty();
}

bool PersonalDataManager::ProfileChangesAreOngoing() {
  for (const auto& [guid, change] : ongoing_profile_changes_) {
    if (ProfileChangesAreOngoing(guid)) {
      return true;
    }
  }
  return false;
}

void PersonalDataManager::OnProfileChangeDone(const std::string& guid) {
  ongoing_profile_changes_[guid].pop_front();

  if (!ProfileChangesAreOngoing()) {
    Refresh();
  } else {
    NotifyPersonalDataObserver();
    HandleNextProfileChange(guid);
  }
}

void PersonalDataManager::ClearOnGoingProfileChanges() {
  ongoing_profile_changes_.clear();
}

bool PersonalDataManager::HasPendingQueries() {
  return pending_profiles_query_ != 0 || pending_creditcards_query_ != 0 ||
         pending_server_profiles_query_ != 0 ||
         pending_server_creditcards_query_ != 0 ||
         pending_server_creditcard_cloud_token_data_query_ != 0 ||
         pending_customer_data_query_ != 0 || pending_upi_ids_query_ != 0 ||
         pending_offer_data_query_ != 0;
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
void PersonalDataManager::MigrateUserOptedInWalletSyncTransportIfNeeded() {
  if (!sync_service_)
    return;

  CoreAccountInfo primary_account = sync_service_->GetAccountInfo();
  if (primary_account.IsEmpty())
    return;

  if (identity_manager_->GetAccountIdMigrationState() ==
      signin::IdentityManager::MIGRATION_NOT_STARTED) {
    return;
  }

  CoreAccountId primary_account_id = primary_account.account_id;

  // When migration is started or done, the primary account is created from a
  // Gaia ID.
  if (primary_account_id.IsEmail()) {
    DLOG(ERROR) << "Unexpected primary account id from an email ["
                << primary_account_id << "].";
    base::UmaHistogramEnumeration(
        "Autofill.MigrateUserOptedInToWalletSync",
        MigrateUserOptedInWalletSyncType::
            kNotMigratedUnexpectedPrimaryAccountIdWithEmail);
    return;
  }

  CoreAccountId legacy_account_id_from_email =
      CoreAccountId::FromEmail(gaia::CanonicalizeEmail(primary_account.email));

  MigrateUserOptedInWalletSyncType migrate =
      prefs::IsUserOptedInWalletSyncTransport(pref_service_,
                                              legacy_account_id_from_email)
          ? MigrateUserOptedInWalletSyncType::kMigratedFromCanonicalEmail
          : MigrateUserOptedInWalletSyncType::kNotMigrated;

  if (migrate == MigrateUserOptedInWalletSyncType::kNotMigrated &&
      prefs::IsUserOptedInWalletSyncTransport(
          pref_service_, CoreAccountId::FromEmail(primary_account.email))) {
    // Only canonicalized emails should be used to create CoreAccountId objects
    // by the IdentityManager. Be overly caution and also check whether
    // the non-canonical email was used when the user opted in to wallet sync.
    legacy_account_id_from_email =
        CoreAccountId::FromEmail(primary_account.email);
    migrate = MigrateUserOptedInWalletSyncType::kMigratedFromNonCanonicalEmail;
  }

  base::UmaHistogramEnumeration("Autofill.MigrateUserOptedInToWalletSync",
                                migrate);

  if (migrate == MigrateUserOptedInWalletSyncType::kNotMigrated)
    return;

  DCHECK(prefs::IsUserOptedInWalletSyncTransport(pref_service_,
                                                 legacy_account_id_from_email));
  prefs::SetUserOptedInWalletSyncTransport(pref_service_,
                                           legacy_account_id_from_email,
                                           /*opted_in=*/false);
  prefs::SetUserOptedInWalletSyncTransport(pref_service_, primary_account_id,
                                           /*opted_in=*/true);
}
#endif

bool PersonalDataManager::IsSyncEnabledFor(syncer::ModelType model_type) {
  return sync_service_ != nullptr && sync_service_->CanSyncFeatureStart() &&
         sync_service_->GetPreferredDataTypes().Has(model_type);
}

scoped_refptr<AutofillWebDataService> PersonalDataManager::GetLocalDatabase() {
  DCHECK(database_helper_);
  return database_helper_->GetLocalDatabase();
}

void PersonalDataManager::OnServerCreditCardsRefreshed() {
  ProcessCardArtUrlChanges();
}

void PersonalDataManager::ProcessCardArtUrlChanges() {
  std::vector<GURL> updated_urls;
  for (auto& card : server_credit_cards_) {
    if (!card->card_art_url().is_valid())
      continue;

    // Try to find the old entry with the same url.
    auto it = credit_card_art_images_.find(card->card_art_url());
    // No existing entry found.
    if (it == credit_card_art_images_.end())
      updated_urls.emplace_back(card->card_art_url());
  }
  if (!updated_urls.empty())
    FetchImagesForUrls(updated_urls);
}

size_t PersonalDataManager::GetServerCardWithArtImageCount() const {
  return base::ranges::count_if(
      server_credit_cards_.begin(), server_credit_cards_.end(),
      [](const auto& card) { return card->card_art_url().is_valid(); });
}

}  // namespace autofill
