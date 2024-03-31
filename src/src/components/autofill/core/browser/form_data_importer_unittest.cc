// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/form_data_importer.h"

#include <stddef.h>

#include <algorithm>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/guid.h"
#include "base/memory/raw_ptr.h"
#include "base/ranges/algorithm.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/data_model/autofill_profile.h"
#include "components/autofill/core/browser/data_model/autofill_structured_address_utils.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/metrics/autofill_metrics.h"
#include "components/autofill/core/browser/payments/test_virtual_card_enrollment_manager.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_autofill_clock.h"
#include "components/autofill/core/browser/webdata/autofill_table.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/autofill/core/common/autocomplete_parsing_util.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "components/sync/test/test_sync_service.h"
#include "components/webdata/common/web_data_service_base.h"
#include "components/webdata/common/web_database_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::UTF8ToUTF16;
using testing::_;

namespace autofill {
namespace {

// Define values for the default address profile.
constexpr char kDefaultFullName[] = "Thomas Neo Anderson";
constexpr char kDefaultFirstName[] = "Thomas";
constexpr char kDefaultLastName[] = "Anderson";
constexpr char kDefaultMail[] = "theone@thematrix.org";
constexpr char kDefaultAddressLine1[] = "21 Laussat St";
constexpr char kDefaultStreetAddress[] = "21 Laussat St\\nApt 123";
constexpr char kDefaultZip[] = "94102";
constexpr char kDefaultCity[] = "Los Angeles";
constexpr char kDefaultState[] = "California";
constexpr char kDefaultCountry[] = "US";
constexpr char kDefaultPhone[] = "+1 650-555-0000";
constexpr char kDefaultPhoneAlternativeFormatting[] = "650-555-0000";
constexpr char kDefaultPhoneDomesticFormatting[] = "(650) 555-0000";
constexpr char kDefaultPhoneAreaCode[] = "650";
constexpr char kDefaultPhonePrefix[] = "555";
constexpr char kDefaultPhoneSuffix[] = "0000";

// Define values for a second address profile.
constexpr char kSecondFirstName[] = "Bruce";
constexpr char kSecondLastName[] = "Wayne";
constexpr char kSecondMail[] = "wayne@bruce.org";
constexpr char kSecondAddressLine1[] = "23 Main St";
constexpr char kSecondZip[] = "94106";
constexpr char kSecondCity[] = "Los Angeles";
constexpr char kSecondState[] = "California";
constexpr char kSecondPhone[] = "+1 651-666-1111";
constexpr char kSecondPhoneAreaCode[] = "651";
constexpr char kSecondPhonePrefix[] = "666";
constexpr char kSecondPhoneSuffix[] = "1111";

// Define values for a third address profile.
constexpr char kThirdFirstName[] = "Homer";
constexpr char kThirdLastName[] = "Simpson";
constexpr char kThirdMail[] = "donut@whatever.net";
constexpr char kThirdAddressLine1[] = "742 Evergreen Terrace";
constexpr char kThirdZip[] = "65619";
constexpr char kThirdCity[] = "Springfield";
constexpr char kThirdState[] = "Oregon";
constexpr char kThirdPhone[] = "+1 851-777-2222";

constexpr char kDefaultCreditCardName[] = "Biggie Smalls";
constexpr char kDefaultCreditCardNumber[] = "4111 1111 1111 1111";
constexpr char kDefaultCreditCardExpMonth[] = "01";
constexpr char kDefaultCreditCardExpYear[] = "2999";

// For a given ServerFieldType |type| returns a pair of field name and label
// that should be parsed into this type by our field type parsers.
std::pair<std::string, std::string> GetLabelAndNameForType(
    ServerFieldType type) {
  static const std::map<ServerFieldType, std::pair<std::string, std::string>>
      name_type_map = {
          {NAME_FULL, {"Full Name:", "full_name"}},
          {NAME_FIRST, {"First Name:", "first_name"}},
          {NAME_MIDDLE, {"Middle Name", "middle_name"}},
          {NAME_LAST, {"Last Name:", "last_name"}},
          {EMAIL_ADDRESS, {"Email:", "email"}},
          {ADDRESS_HOME_LINE1, {"Address:", "address1"}},
          {ADDRESS_HOME_STREET_ADDRESS, {"Address:", "address"}},
          {ADDRESS_HOME_CITY, {"City:", "city"}},
          {ADDRESS_HOME_ZIP, {"Zip:", "zip"}},
          {ADDRESS_HOME_STATE, {"State:", "state"}},
          {ADDRESS_HOME_DEPENDENT_LOCALITY, {"Neighborhood:", "neighborhood"}},
          {ADDRESS_HOME_COUNTRY, {"Country:", "country"}},
          {PHONE_HOME_WHOLE_NUMBER, {"Phone:", "phone"}},
          {CREDIT_CARD_NAME_FULL, {"Name on card:", "name_on_card"}},
          {CREDIT_CARD_NUMBER, {"Credit Card Number:", "card_number"}},
          {CREDIT_CARD_EXP_MONTH, {"Exp Month:", "exp_month"}},
          {CREDIT_CARD_EXP_4_DIGIT_YEAR, {"Exp Year:", "exp_year"}},
      };
  auto it = name_type_map.find(type);
  if (it == name_type_map.end()) {
    NOTIMPLEMENTED() << " field name and label is missing for "
                     << AutofillType(type).ToString();
    return {std::string(), std::string()};
  }
  return it->second;
}

using TypeValuePairs = std::vector<std::pair<ServerFieldType, std::string>>;

// Constructs a FormData instance for |url| from a vector of type value pairs
// that defines a sequence of fields and the filled values.
// The field names and labels for the different types are relieved from
// |GetLabelAndNameForType(type)|
FormData ConstructFormDateFromTypeValuePairs(
    TypeValuePairs type_value_pairs,
    std::string url = "https://www.foo.com") {
  FormData form;
  form.url = GURL(url);

  FormFieldData field;
  for (const auto& [type, value] : type_value_pairs) {
    const auto& [name, label] = GetLabelAndNameForType(type);
    test::CreateTestFormField(
        name.c_str(), label.c_str(), value.c_str(),
        type == ADDRESS_HOME_STREET_ADDRESS ? "textarea" : "text", &field);
    form.fields.push_back(field);
  }

  return form;
}

// Constructs a FormStructure instance from a FormData instance and determines
// the heuristic types.
std::unique_ptr<FormStructure> ConstructFormStructureFromFormData(
    const FormData& form) {
  auto form_structure = std::make_unique<FormStructure>(form);
  form_structure->DetermineHeuristicTypes(nullptr, nullptr);
  return form_structure;
}

// Constructs a FormStructure instance with fields and inserted values given by
// a vector of type and value pairs.
std::unique_ptr<FormStructure> ConstructFormStructureFromTypeValuePairs(
    TypeValuePairs type_value_pairs,
    std::string url = "https://www.foo.com") {
  FormData form = ConstructFormDateFromTypeValuePairs(type_value_pairs, url);
  return ConstructFormStructureFromFormData(form);
}

// Construct and finalizes an AutofillProfile based on a vector of type and
// value pairs. The values are set as |VerificationStatus::kObserved| and the
// profile is finalizes in the end.
AutofillProfile ConstructProfileFromTypeValuePairs(
    TypeValuePairs type_value_pairs) {
  AutofillProfile profile;
  for (const auto& [type, value] : type_value_pairs) {
    profile.SetRawInfoWithVerificationStatus(
        type, base::UTF8ToUTF16(value),
        structured_address::VerificationStatus::kObserved);
  }
  if (!profile.FinalizeAfterImport())
    NOTREACHED();
  return profile;
}

// Returns a vector of ServerFieldType and value pairs used to construct the
// default AutofillProfile, or a FormStructure or FormData instance that carries
// that corresponding information.
TypeValuePairs GetDefaultProfileTypeValuePairs() {
  return {
      {NAME_FIRST, kDefaultFirstName},
      {NAME_LAST, kDefaultLastName},
      {EMAIL_ADDRESS, kDefaultMail},
      {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, kDefaultCountry},
  };
}

// Wraps `GetDefaultProfileTypeValuePairs()` but replaces `kDefaultCountry` with
// `country`. If `country` is empty, ADDRESS_HOME_COUNTRY is removed entirely.
TypeValuePairs GetDefaultProfileTypeValuePairsWithOverriddenCountry(
    const std::string& country) {
  constexpr int kCountryIndex = 8;
  auto pairs = GetDefaultProfileTypeValuePairs();
  DCHECK_EQ(pairs[kCountryIndex].first, ADDRESS_HOME_COUNTRY);
  if (country.empty())
    pairs.erase(pairs.begin() + kCountryIndex);
  else
    pairs[kCountryIndex].second = country;
  return pairs;
}

// Same as |GetDefaultProfileTypeValuePairs()|, but split into two parts to test
// multi-step imports. No part by itself satisfies the import requirements.
// |part| specifies the requested half and can be either 1 or 2.
TypeValuePairs GetSplitDefaultProfileTypeValuePairs(int part) {
  DCHECK(part == 1 || part == 2);
  if (part == 1) {
    return {
        {NAME_FIRST, kDefaultFirstName},
        {NAME_LAST, kDefaultLastName},
        {EMAIL_ADDRESS, kDefaultMail},
        {ADDRESS_HOME_CITY, kDefaultCity},
        {ADDRESS_HOME_STATE, kDefaultState},
        {ADDRESS_HOME_COUNTRY, kDefaultCountry},
    };
  } else {
    return {
        {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
        {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
        {ADDRESS_HOME_ZIP, kDefaultZip},
    };
  }
}

// Same as |GetDefaultProfileTypeValuePairs()| but with the second profile
// information.
TypeValuePairs GetSecondProfileTypeValuePairs() {
  return {
      {NAME_FIRST, kSecondFirstName},
      {NAME_LAST, kSecondLastName},
      {EMAIL_ADDRESS, kSecondMail},
      {PHONE_HOME_WHOLE_NUMBER, kSecondPhone},
      {ADDRESS_HOME_LINE1, kSecondAddressLine1},
      {ADDRESS_HOME_CITY, kSecondCity},
      {ADDRESS_HOME_STATE, kSecondState},
      {ADDRESS_HOME_ZIP, kSecondZip},
      {ADDRESS_HOME_COUNTRY, kDefaultCountry},
  };
}

// Same as |GetDefaultProfileTypeValuePairs()| but with the third profile
// information.
TypeValuePairs GetThirdProfileTypeValuePairs() {
  return {
      {NAME_FIRST, kThirdFirstName},
      {NAME_LAST, kThirdLastName},
      {EMAIL_ADDRESS, kThirdMail},
      {PHONE_HOME_WHOLE_NUMBER, kThirdPhone},
      {ADDRESS_HOME_LINE1, kThirdAddressLine1},
      {ADDRESS_HOME_CITY, kThirdCity},
      {ADDRESS_HOME_STATE, kThirdState},
      {ADDRESS_HOME_ZIP, kThirdZip},
      {ADDRESS_HOME_COUNTRY, kDefaultCountry},
  };
}

// Same as `GetDefaultProfileTypeValuePairs()`, but for credit cards.
TypeValuePairs GetDefaultCreditCardTypeValuePairs() {
  return {
      {CREDIT_CARD_NAME_FULL, kDefaultCreditCardName},
      {CREDIT_CARD_NUMBER, kDefaultCreditCardNumber},
      {CREDIT_CARD_EXP_MONTH, kDefaultCreditCardExpMonth},
      {CREDIT_CARD_EXP_4_DIGIT_YEAR, kDefaultCreditCardExpYear},
  };
}

// Returns the default AutofillProfile used in this test file.
AutofillProfile ConstructDefaultProfile() {
  return ConstructProfileFromTypeValuePairs(GetDefaultProfileTypeValuePairs());
}

// Wraps `ConstructDefaultProfile()`, but overrides ADDRESS_HOME_COUNTRY with
// `country`.
AutofillProfile ConstructDefaultProfileWithOverriddenCountry(
    const std::string& country) {
  return ConstructProfileFromTypeValuePairs(
      GetDefaultProfileTypeValuePairsWithOverriddenCountry(country));
}

// Returns the second AutofillProfile used in this test file.
AutofillProfile ConstructSecondProfile() {
  return ConstructProfileFromTypeValuePairs(GetSecondProfileTypeValuePairs());
}

// Returns the third AutofillProfile used in this test file.
AutofillProfile ConstructThirdProfile() {
  return ConstructProfileFromTypeValuePairs(GetThirdProfileTypeValuePairs());
}

// Returns a form with the default profile. The AutofillProfile that is imported
// from this form should be similar to the profile create by calling
// |ConstructDefaultProfile()|.
std::unique_ptr<FormStructure> ConstructDefaultProfileFormStructure() {
  return ConstructFormStructureFromTypeValuePairs(
      GetDefaultProfileTypeValuePairs());
}

// Same as |ConstructDefaultFormStructure()| but split into two parts to test
// multi-step imports (see |GetSplitDefaultProfileTypeValuePairs()|).
std::unique_ptr<FormStructure> ConstructSplitDefaultProfileFormStructure(
    int part) {
  return ConstructFormStructureFromTypeValuePairs(
      GetSplitDefaultProfileTypeValuePairs(part));
}

// Same as |ConstructDefaultFormStructure()| but for the second profile.
std::unique_ptr<FormStructure> ConstructSecondProfileFormStructure() {
  return ConstructFormStructureFromTypeValuePairs(
      GetSecondProfileTypeValuePairs());
}

// Same as |ConstructDefaultFormStructure()| but for the third profile.
std::unique_ptr<FormStructure> ConstructThirdProfileFormStructure() {
  return ConstructFormStructureFromTypeValuePairs(
      GetThirdProfileTypeValuePairs());
}

// Constructs a FormStructure with two address sections by concatenating
// the default profile and second profile form structures.
std::unique_ptr<FormStructure> ConstructShippingAndBillingFormStructure() {
  TypeValuePairs a = GetDefaultProfileTypeValuePairs();
  TypeValuePairs b = GetSecondProfileTypeValuePairs();
  a.reserve(a.size() + b.size());
  base::ranges::move(b, std::back_inserter(a));
  return ConstructFormStructureFromTypeValuePairs(a);
}

// Same as `ConstructDefaultFormStructure()` but for credit cards.
std::unique_ptr<FormStructure> ConstructDefaultCreditCardFormStructure() {
  return ConstructFormStructureFromTypeValuePairs(
      GetDefaultCreditCardTypeValuePairs());
}

// Constructs a |FormData| instance that carries the information of the default
// profile.
FormData ConstructDefaultFormData() {
  return ConstructFormDateFromTypeValuePairs(GetDefaultProfileTypeValuePairs());
}

// Same as |ConstructDefaultFormData()| but split into two parts to test multi-
// step imports (see |GetSplitDefaultProfileTypeValuePairs()|).
FormData ConstructSplitDefaultFormData(int part) {
  return ConstructFormDateFromTypeValuePairs(
      GetSplitDefaultProfileTypeValuePairs(part));
}

ACTION_P(QuitMessageLoop, loop) {
  loop->Quit();
}

enum UserMode { USER_MODE_NORMAL, USER_MODE_INCOGNITO };

class PersonalDataLoadedObserverMock : public PersonalDataManagerObserver {
 public:
  PersonalDataLoadedObserverMock() = default;
  ~PersonalDataLoadedObserverMock() override = default;

  MOCK_METHOD(void, OnPersonalDataChanged, (), (override));
  MOCK_METHOD(void, OnPersonalDataFinishedProfileTasks, (), (override));
};

// Matches an AddressProfile or CreditCard pointer according to Compare().
// Takes `expected` by value to avoid a dangling reference.
template <typename T>
auto ComparesEqual(T expected) {
  return ::testing::Truly([expected = std::move(expected)](const T& actual) {
    return actual.Compare(expected) == 0;
  });
}

// The below matchers follow ::testing::UnorderedElementsAre[Array] except that
// they accept AutofillProfile or CreditCard *pointers* and compare their
// pointees using ComparesEqual().

template <typename T>
auto UnorderedElementsCompareEqualArray(const std::vector<T>& expected_values) {
  std::vector<::testing::Matcher<T*>> matchers;
  for (const T& expected : expected_values)
    matchers.push_back(::testing::Pointee(ComparesEqual(expected)));
  return ::testing::UnorderedElementsAreArray(matchers);
}

template <typename... Matchers>
auto UnorderedElementsCompareEqual(Matchers... matchers) {
  return ::testing::UnorderedElementsAre(
      ::testing::Pointee(ComparesEqual(std::move(matchers)))...);
}

}  // anonymous namespace

class MockVirtualCardEnrollmentManager
    : public TestVirtualCardEnrollmentManager {
 public:
  MockVirtualCardEnrollmentManager(
      TestPersonalDataManager* personal_data_manager,
      payments::TestPaymentsClient* payments_client,
      TestAutofillClient* autofill_client)
      : TestVirtualCardEnrollmentManager(personal_data_manager,
                                         payments_client,
                                         autofill_client) {}
  MOCK_METHOD(
      void,
      InitVirtualCardEnroll,
      (const CreditCard& credit_card,
       VirtualCardEnrollmentSource virtual_card_enrollment_source,
       absl::optional<
           payments::PaymentsClient::GetDetailsForEnrollmentResponseDetails>
           get_details_for_enrollment_response_details,
       const raw_ptr<PrefService> user_prefs,
       VirtualCardEnrollmentManager::RiskAssessmentFunction
           risk_assessment_function,
       VirtualCardEnrollmentManager::VirtualCardEnrollmentFieldsLoadedCallback
           virtual_card_enrollment_fields_loaded_callback),
      (override));
};

class FormDataImporterTestBase {
 protected:
  FormDataImporterTestBase() : autofill_table_(nullptr) {}

  void ResetPersonalDataManager(UserMode user_mode) {
    // Before invalidating the `personal_data_manager_`, the
    // `form_data_importer` needs to be reset, because it stores a weak
    // reference to `personal_data_manager_` that otherwise points to garbage.
    form_data_importer_.reset();

    if (personal_data_manager_) {
      personal_data_manager_->Shutdown();
    }
    personal_data_manager_ = std::make_unique<PersonalDataManager>("en", "US");
    personal_data_manager_->set_auto_accept_address_imports_for_testing(true);
    personal_data_manager_->Init(
        scoped_refptr<AutofillWebDataService>(autofill_database_service_),
        /*account_database=*/nullptr,
        /*pref_service=*/prefs_.get(),
        /*local_state=*/prefs_.get(),
        /*identity_manager=*/identity_test_env_.identity_manager(),
        /*history_service=*/nullptr,
        /*strike_database=*/nullptr,
        /*image_fetcher=*/nullptr,
        /*is_off_the_record=*/(user_mode == USER_MODE_INCOGNITO));
    personal_data_manager_->AddObserver(&personal_data_observer_);
    personal_data_manager_->OnSyncServiceInitialized(nullptr);

    WaitForOnPersonalDataChanged();

    // Reconstruct the `form_data_importer_` with the new
    // `personal_data_manager_`.
    form_data_importer_ =
        std::make_unique<FormDataImporter>(autofill_client_.get(),
                                           /*payments::PaymentsClient=*/nullptr,
                                           personal_data_manager_.get(), "en");
    auto virtual_card_enrollment_manager =
        std::make_unique<MockVirtualCardEnrollmentManager>(
            nullptr, nullptr, autofill_client_.get());
    virtual_card_enrollment_manager_ = virtual_card_enrollment_manager.get();
    form_data_importer_->virtual_card_enrollment_manager_ =
        std::move(virtual_card_enrollment_manager);
  }

  void SetUpHelper() {
    prefs_ = test::PrefServiceForTesting();
    base::FilePath path(WebDatabase::kInMemoryPath);
    web_database_ =
        new WebDatabaseService(path, base::ThreadTaskRunnerHandle::Get(),
                               base::ThreadTaskRunnerHandle::Get());

    // Hacky: hold onto a pointer but pass ownership.
    autofill_table_ = new AutofillTable;
    web_database_->AddTable(std::unique_ptr<WebDatabaseTable>(autofill_table_));
    web_database_->LoadDatabase();
    autofill_database_service_ = new AutofillWebDataService(
        web_database_, base::ThreadTaskRunnerHandle::Get(),
        base::ThreadTaskRunnerHandle::Get());
    autofill_database_service_->Init(base::NullCallback());

    autofill_client_ = std::make_unique<TestAutofillClient>();

    test::DisableSystemServices(prefs_.get());
    // This will also initialize the `form_data_importer_`.
    ResetPersonalDataManager(USER_MODE_NORMAL);

    // Reset the deduping pref to its default value.
    personal_data_manager_->pref_service_->SetInteger(
        prefs::kAutofillLastVersionDeduped, 0);
  }

  void TearDownHelper() {
    if (personal_data_manager_) {
      personal_data_manager_->Shutdown();
    }
  }

  // Helper method that will add credit card fields in |form|, according to the
  // specified values. If a value is nullptr, the corresponding field won't get
  // added (empty string will add a field with an empty string as the value).
  void AddFullCreditCardForm(FormData* form,
                             const char* name,
                             const char* number,
                             const char* month,
                             const char* year) {
    FormFieldData field;
    if (name) {
      test::CreateTestFormField("Name on card:", "name_on_card", name, "text",
                                &field);
      form->fields.push_back(field);
    }
    if (number) {
      test::CreateTestFormField("Card Number:", "card_number", number, "text",
                                &field);
      form->fields.push_back(field);
    }
    if (month) {
      test::CreateTestFormField("Exp Month:", "exp_month", month, "text",
                                &field);
      form->fields.push_back(field);
    }
    if (year) {
      test::CreateTestFormField("Exp Year:", "exp_year", year, "text", &field);
      form->fields.push_back(field);
    }
  }

  // Helper methods that simply forward the call to the private member (to avoid
  // having to friend every test that needs to access the private
  // PersonalDataManager::ImportAddressProfile or ImportCreditCard).
  void ImportAddressProfiles(bool extraction_successful,
                             const FormStructure& form,
                             bool skip_waiting_on_pdm = false,
                             bool allow_save_prompts = true) {
    // This parameter has no effect unless save prompts for addresses are
    // enabled.
    allow_save_prompts =
        allow_save_prompts || !base::FeatureList::IsEnabled(
                                  features::kAutofillAddressProfileSavePrompt);

    std::vector<FormDataImporter::AddressProfileImportCandidate>
        address_profile_import_candidates;

    EXPECT_EQ(extraction_successful,
              form_data_importer_->ImportAddressProfiles(
                  form, address_profile_import_candidates) > 0);

    if (!extraction_successful) {
      EXPECT_FALSE(form_data_importer_->ProcessAddressProfileImportCandidates(
          address_profile_import_candidates, allow_save_prompts));
      return;
    }

    if (skip_waiting_on_pdm) {
      EXPECT_EQ(form_data_importer_->ProcessAddressProfileImportCandidates(
                    address_profile_import_candidates, allow_save_prompts),
                allow_save_prompts);
      return;
    }

    base::RunLoop run_loop;
    EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
        .WillOnce(QuitMessageLoop(&run_loop));
    EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
    EXPECT_EQ(form_data_importer_->ProcessAddressProfileImportCandidates(
                  address_profile_import_candidates, allow_save_prompts),
              allow_save_prompts);
    run_loop.Run();
  }

  // Verifies that the stored profiles in the PersonalDataManager equal
  // |expected_profiles| with respect to |AutofillProfile::Compare|.
  // Note, that order is taken into account.
  void VerifyExpectationForImportedAddressProfiles(
      const std::vector<AutofillProfile>& expected_profiles) {
    EXPECT_THAT(personal_data_manager_->GetProfiles(),
                UnorderedElementsCompareEqualArray(expected_profiles));
  }

  // Convenience wrapper that calls |FormDataImporter::ImportFormData()| and
  // subsequetly processes the candidates for address profile import.
  // Returns the result of |FormDataImporter::ImportFormData()|.
  bool ImportFormDataAndProcessAddressCandidates(
      const FormStructure& form,
      bool profile_autofill_enabled,
      bool credit_card_autofill_enabled,
      bool should_return_local_card,
      std::unique_ptr<CreditCard>* imported_credit_card,
      absl::optional<std::string>* imported_upi_id) {
    std::vector<FormDataImporter::AddressProfileImportCandidate>
        address_profile_import_candidates;

    bool result = form_data_importer_->ImportFormData(
        form, profile_autofill_enabled, credit_card_autofill_enabled,
        should_return_local_card, imported_credit_card,
        address_profile_import_candidates, imported_upi_id);

    form_data_importer_->ProcessAddressProfileImportCandidates(
        address_profile_import_candidates);

    return result;
  }

  void ImportAddressProfilesAndVerifyExpectation(
      const FormStructure& form,
      const std::vector<AutofillProfile>& expected_profiles) {
    ImportAddressProfiles(/*extraction_successful=*/!expected_profiles.empty(),
                          form);
    VerifyExpectationForImportedAddressProfiles(expected_profiles);
  }

  void ImportAddressProfileAndVerifyImportOfDefaultProfile(
      const FormStructure& form) {
    ImportAddressProfilesAndVerifyExpectation(form,
                                              {ConstructDefaultProfile()});
  }

  void ImportAddressProfileAndVerifyImportOfNoProfile(
      const FormStructure& form) {
    ImportAddressProfilesAndVerifyExpectation(form, {});
  }

  bool ImportCreditCard(const FormStructure& form,
                        bool should_return_local_card,
                        std::unique_ptr<CreditCard>* imported_credit_card) {
    return form_data_importer_->ImportCreditCard(form, should_return_local_card,
                                                 imported_credit_card);
  }

  void SubmitFormAndExpectImportedCardWithData(const FormData& form,
                                               const char* exp_name,
                                               const char* exp_cc_num,
                                               const char* exp_cc_month,
                                               const char* exp_cc_year) {
    FormStructure form_structure(form);
    form_structure.DetermineHeuristicTypes(nullptr, nullptr);
    std::unique_ptr<CreditCard> imported_credit_card;
    EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
    ASSERT_TRUE(imported_credit_card);
    personal_data_manager_->OnAcceptedLocalCreditCardSave(
        *imported_credit_card);

    WaitForOnPersonalDataChanged();
    CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
    test::SetCreditCardInfo(&expected, exp_name, exp_cc_num, exp_cc_month,
                            exp_cc_year, "");
    EXPECT_THAT(personal_data_manager_->GetCreditCards(),
                UnorderedElementsCompareEqual(expected));
  }

  void WaitForOnPersonalDataChanged() {
    base::RunLoop run_loop;
    EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
        .WillOnce(QuitMessageLoop(&run_loop));
    EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
        .Times(testing::AnyNumber());
    run_loop.Run();
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::SingleThreadTaskEnvironment::MainThreadType::UI};
  test::AutofillEnvironment autofill_environment_;
  std::unique_ptr<PrefService> prefs_;
  signin::IdentityTestEnvironment identity_test_env_;
  scoped_refptr<AutofillWebDataService> autofill_database_service_;
  scoped_refptr<WebDatabaseService> web_database_;
  raw_ptr<AutofillTable> autofill_table_;  // weak ref
  PersonalDataLoadedObserverMock personal_data_observer_;
  std::unique_ptr<TestAutofillClient> autofill_client_;
  std::unique_ptr<PersonalDataManager> personal_data_manager_;
  std::unique_ptr<FormDataImporter> form_data_importer_;
  MockVirtualCardEnrollmentManager* virtual_card_enrollment_manager_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

// TODO(crbug.com/1103421): Clean legacy implementation once structured names
// are fully launched. Here, the changes applied in CL 2339350 must be reverted
// by removing the parameterization.
class FormDataImporterTest
    : public FormDataImporterTestBase,
      public testing::Test,
      public testing::WithParamInterface<std::tuple<bool, bool>> {
 protected:
  bool ConsiderVariationCountryCodeForPhoneNumbers() {
    return consider_variation_country_code_for_phone_numbers_;
  }

 private:
  void SetUp() override {
    InitializeFeatures();
    SetUpHelper();
  }

  void TearDown() override { TearDownHelper(); }

  void InitializeFeatures() {
    support_for_apartment_numbers_ = std::get<0>(GetParam());
    consider_variation_country_code_for_phone_numbers_ =
        std::get<1>(GetParam());

    // Enable all those features by default.
    std::vector<base::Feature> enabled_features{
        features::kAutofillEnableSupportForMoreStructureInAddresses,
        features::kAutofillEnableSupportForMoreStructureInNames};

    std::vector<base::Feature> disabled_features;

    (support_for_apartment_numbers_ ? enabled_features : disabled_features)
        .push_back(features::kAutofillEnableSupportForApartmentNumbers);

    (consider_variation_country_code_for_phone_numbers_ ? enabled_features
                                                        : disabled_features)
        .push_back(
            features::kAutofillConsiderVariationCountryCodeForPhoneNumbers);

    scoped_feature_list_.InitWithFeatures(enabled_features, disabled_features);
  }

  bool support_for_apartment_numbers_;
  bool consider_variation_country_code_for_phone_numbers_;
};

TEST_P(FormDataImporterTest, ComplementCountry) {
  auto ImportWithCountry =
      [this](const std::string& form_country,
             const std::vector<AutofillProfile>& expected_profiles) {
        // Remove existing profiles, to prevent an update instead of an import.
        personal_data_manager_->ClearAllLocalData();

        std::unique_ptr<FormStructure> form_structure =
            ConstructFormStructureFromTypeValuePairs(
                GetDefaultProfileTypeValuePairsWithOverriddenCountry(
                    form_country));
        ImportAddressProfilesAndVerifyExpectation(*form_structure,
                                                  expected_profiles);
      };
  // The German profile doesn't expect a state.
  AutofillProfile kDefaultGermanProfile =
      ConstructDefaultProfileWithOverriddenCountry("DE");
  kDefaultGermanProfile.ClearFields({ADDRESS_HOME_STATE});

  // Country part of the form:
  // If a valid country was entered, use that.
  ImportWithCountry("Germany", {kDefaultGermanProfile});
  // Reject the profile if an invalid country was entered.
  ImportWithCountry("Somewhere", {});
  // Country not part of the form: Complement using
  // FormDataImporter::GetPredictedCountryCode
  // If no variation config country code is available, default to locale (US)
  ImportWithCountry("", {ConstructDefaultProfileWithOverriddenCountry("US")});
  // Prefer variation config country code over locale
  autofill_client_->SetVariationConfigCountryCode("DE");
  ImportWithCountry("", {kDefaultGermanProfile});
}

// Tests how invalid countries in submitted forms are treated depending on
// `kAutofillIgnoreInvalidCountryOnImport`.
TEST_P(FormDataImporterTest, InvalidCountry) {
  // Due to the extra 'A', the country of this `form_structure` is invalid.
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(
          GetDefaultProfileTypeValuePairsWithOverriddenCountry("USAA"));
  // With `kAutofillIgnoreInvalidCountryOnImport` disabled, profiles with
  // invalid country information are rejected.
  {
    base::test::ScopedFeatureList ignore_invalid_country_feature;
    ignore_invalid_country_feature.InitAndDisableFeature(
        features::kAutofillIgnoreInvalidCountryOnImport);
    ImportAddressProfileAndVerifyImportOfNoProfile(*form_structure);
  }
  // With the feature enabled, the invalid country is ignored and country
  // complemention overwrites it. It becomes US due to the locale.
  {
    base::test::ScopedFeatureList ignore_invalid_country_feature;
    ignore_invalid_country_feature.InitAndEnableFeature(
        features::kAutofillIgnoreInvalidCountryOnImport);
    ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
  }
}

TEST_P(FormDataImporterTest, InvalidPhoneNumber) {
  TypeValuePairs profile_with_invalid_phone_number =
      GetDefaultProfileTypeValuePairs();
  const int phone_number_index = 3;
  ASSERT_EQ(profile_with_invalid_phone_number[phone_number_index].first,
            PHONE_HOME_WHOLE_NUMBER);
  profile_with_invalid_phone_number[phone_number_index].second = "invalid";
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(
          profile_with_invalid_phone_number);

  // With |kAutofillRemoveInvalidPhoneNumberOnImport| disabled, profiles with
  // invalid phone numbers are rejected.
  {
    base::test::ScopedFeatureList remove_invalid_phone_number_feature;
    remove_invalid_phone_number_feature.InitAndDisableFeature(
        features::kAutofillRemoveInvalidPhoneNumberOnImport);

    ImportAddressProfilesAndVerifyExpectation(*form_structure, {});
  }

  // With the feature enabled, the phone number is removed and the profile
  // imported.
  {
    base::test::ScopedFeatureList remove_invalid_phone_number_feature;
    remove_invalid_phone_number_feature.InitAndEnableFeature(
        features::kAutofillRemoveInvalidPhoneNumberOnImport);

    profile_with_invalid_phone_number.erase(
        profile_with_invalid_phone_number.begin() + phone_number_index);
    ImportAddressProfilesAndVerifyExpectation(
        *form_structure, {ConstructProfileFromTypeValuePairs(
                             profile_with_invalid_phone_number)});
  }
}

TEST_P(FormDataImporterTest, PhoneNumberRegionMetrics) {
  // This test is only applicable if the feature is enabled.
  if (!ConsiderVariationCountryCodeForPhoneNumbers())
    return;

  auto ImportWithPhoneNumber =
      [this](const std::string& number,
             AutofillMetrics::PhoneNumberImportParsingResult expected_result) {
        // Remove existing profiles, to prevent an update instead of an import.
        personal_data_manager_->ClearAllLocalData();

        // In order to test the phone number region deduction via the variation
        // country code and app local, the form cannot contain a country field.
        // Passing an empty country archives that.
        TypeValuePairs profile_with_invalid_phone_number =
            GetDefaultProfileTypeValuePairsWithOverriddenCountry("");
        ASSERT_EQ(profile_with_invalid_phone_number[3].first,
                  PHONE_HOME_WHOLE_NUMBER);
        profile_with_invalid_phone_number[3].second = number;
        std::unique_ptr<FormStructure> form_structure =
            ConstructFormStructureFromTypeValuePairs(
                profile_with_invalid_phone_number);

        // An invalid phone number is removed on import.
        base::HistogramTester histogram_tester;
        ImportAddressProfiles(/*extraction_successful=*/true, *form_structure);
        EXPECT_THAT(
            histogram_tester.GetAllSamples(
                "Autofill.ProfileImport.PhoneNumberParsingResult"),
            testing::UnorderedElementsAre(base::Bucket(expected_result, 1)));
      };

  // `form_data_importer_` is initialized with app locale set to "en".
  autofill_client_->SetVariationConfigCountryCode("DE");

  ImportWithPhoneNumber(
      "invalid", AutofillMetrics::PhoneNumberImportParsingResult::CANNOT_PARSE);

  // The German phone number validation is very lenient and accepts all US
  // numbers we could find. Thus no test for
  // AutofillMetrics::PhoneNumberImportParsingResult::PARSED_WITH_APP_LOCALE.

  // A German phone number only parses with the German variation country code.
  ImportWithPhoneNumber("01578 7912345",
                        AutofillMetrics::PhoneNumberImportParsingResult::
                            PARSED_WITH_VARIATION_COUNTRY_CODE);

  // For phone numbers in international format the region is ignored. So this
  // Austrian phone number parses as both.
  ImportWithPhoneNumber(
      "+43 650 3847567",
      AutofillMetrics::PhoneNumberImportParsingResult::PARSED_WITH_BOTH);
}

// ImportAddressProfiles tests.
TEST_P(FormDataImporterTest, ImportStructuredNameProfile) {
  base::test::ScopedFeatureList structured_addresses_feature;
  structured_addresses_feature.InitAndEnableFeature(
      features::kAutofillEnableSupportForMoreStructureInAddresses);

  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name:", "name", "Pablo Diego Ruiz y Picasso",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());

  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_HOUSE_NUMBER), u"21");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_NAME), u"Laussat St");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_ADDRESS),
            u"21 Laussat St");

  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_HOUSE_NUMBER),
            structured_address::VerificationStatus::kParsed);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_NAME),
            structured_address::VerificationStatus::kParsed);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_ADDRESS),
            structured_address::VerificationStatus::kObserved);
}

TEST_P(FormDataImporterTest,
       ImportStructuredAddressProfile_StreetNameAndHouseNumber) {
  base::test::ScopedFeatureList structured_addresses_feature;
  structured_addresses_feature.InitAndEnableFeature(
      features::kAutofillEnableSupportForMoreStructureInAddresses);

  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name:", "name", "Pablo Diego Ruiz y Picasso",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  form.fields.push_back(field);
  test::CreateTestFormField("Street name:", "street_name", "Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("House number:", "house_number", "21", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());

  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_HOUSE_NUMBER), u"21");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_NAME), u"Laussat St");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_ADDRESS),
            u"21 Laussat St");

  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_HOUSE_NUMBER),
            structured_address::VerificationStatus::kObserved);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_NAME),
            structured_address::VerificationStatus::kObserved);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_ADDRESS),
            structured_address::VerificationStatus::kFormatted);
}

TEST_P(
    FormDataImporterTest,
    ImportStructuredAddressProfile_StreetNameAndHouseNumberAndApartmentNumber) {
  // This test is only applicable for enabled structured addresses and support
  // for apartment numbers.
  if (!base::FeatureList::IsEnabled(
          features::kAutofillEnableSupportForMoreStructureInAddresses) ||
      !base::FeatureList::IsEnabled(
          features::kAutofillEnableSupportForApartmentNumbers)) {
    return;
  }
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name:", "name", "Pablo Diego Ruiz y Picasso",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  form.fields.push_back(field);
  test::CreateTestFormField("Street name:", "street_name", "Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("House number:", "house_number", "21", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Apartment", "apartment", "101", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());

  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_HOUSE_NUMBER), u"21");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_NAME), u"Laussat St");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_ADDRESS),
            u"21 Laussat St APT 101");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_APT_NUM), u"101");
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_HOUSE_NUMBER),
            structured_address::VerificationStatus::kObserved);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_NAME),
            structured_address::VerificationStatus::kObserved);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_ADDRESS),
            structured_address::VerificationStatus::kFormatted);
}

TEST_P(FormDataImporterTest,
       ImportStructuredAddressProfile_GermanStreetNameAndHouseNumber) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name:", "name", "Pablo Diego Ruiz y Picasso",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  form.fields.push_back(field);
  test::CreateTestFormField("Street name:", "street_name", "Hermann Strasse",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("House number:", "house_number", "23", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "Munich", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country", "Germany", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "80992", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());

  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_HOUSE_NUMBER), u"23");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_NAME),
            u"Hermann Strasse");
  EXPECT_EQ(results[0]->GetRawInfo(ADDRESS_HOME_STREET_ADDRESS),
            u"Hermann Strasse 23");

  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_HOUSE_NUMBER),
            structured_address::VerificationStatus::kObserved);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_NAME),
            structured_address::VerificationStatus::kObserved);
  EXPECT_EQ(results[0]->GetVerificationStatus(ADDRESS_HOME_STREET_ADDRESS),
            structured_address::VerificationStatus::kFormatted);
}

// ImportAddressProfiles tests.
TEST_P(FormDataImporterTest, ImportStructuredNameAddressProfile) {
  base::test::ScopedFeatureList structured_addresses_feature;
  structured_addresses_feature.InitAndEnableFeature(
      features::kAutofillEnableSupportForMoreStructureInNames);

  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name:", "name", "Pablo Diego Ruiz y Picasso",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());

  EXPECT_EQ(results[0]->GetRawInfo(NAME_FULL), u"Pablo Diego Ruiz y Picasso");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FIRST), u"Pablo Diego");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_MIDDLE), u"");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST), u"Ruiz y Picasso");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST_FIRST), u"Ruiz");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST_CONJUNCTION), u"y");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST_SECOND), u"Picasso");
}

TEST_P(FormDataImporterTest, ImportAddressProfiles) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultProfileFormStructure();
  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

TEST_P(FormDataImporterTest, ImportSecondAddressProfiles) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructSecondProfileFormStructure();
  ImportAddressProfilesAndVerifyExpectation(*form_structure,
                                            {ConstructSecondProfile()});
}

TEST_P(FormDataImporterTest, ImportThirdAddressProfiles) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructThirdProfileFormStructure();
  ImportAddressProfilesAndVerifyExpectation(*form_structure,
                                            {ConstructThirdProfile()});
}

// Test that with dependent locality parsing enabled, dependent locality fields
// are imported.
TEST_P(FormDataImporterTest, ImportAddressProfiles_DependentLocality) {
  base::test::ScopedFeatureList dependent_locality_feature;
  dependent_locality_feature.InitAndEnableFeature(
      features::kAutofillEnableDependentLocalityParsing);

  // The Mexican address format contains a dependent locality.
  TypeValuePairs mx_profile =
      GetDefaultProfileTypeValuePairsWithOverriddenCountry("MX");
  mx_profile.emplace_back(ADDRESS_HOME_DEPENDENT_LOCALITY,
                          "Bosques de las Lomas");
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(mx_profile);
  ImportAddressProfilesAndVerifyExpectation(
      *form_structure, {ConstructProfileFromTypeValuePairs(mx_profile)});
}

// Test that the storage is prevented if the structured address prompt feature
// is enabled, but address prompts are not allowed.
TEST_P(FormDataImporterTest, ImportAddressProfiles_DontAllowPrompt) {
  base::test::ScopedFeatureList save_prompt_feature;
  save_prompt_feature.InitAndEnableFeature(
      features::kAutofillAddressProfileSavePrompt);

  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultProfileFormStructure();
  ImportAddressProfiles(/*extraction_successful=*/true, *form_structure,
                        /*skip_waiting_on_pdm=*/true,
                        /*allow_save_prompts=*/false);
  VerifyExpectationForImportedAddressProfiles({});

  save_prompt_feature.Reset();
  save_prompt_feature.InitAndDisableFeature(
      features::kAutofillAddressProfileSavePrompt);

  // Verify that the behavior changes when prompts are disabled.
  ImportAddressProfiles(/*extraction_successful=*/true, *form_structure,
                        /*skip_waiting_on_pdm=*/false,
                        /*allow_save_prompts=*/false);
  VerifyExpectationForImportedAddressProfiles({ConstructDefaultProfile()});
}

TEST_P(FormDataImporterTest, ImportAddressProfileFromUnifiedSection) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultProfileFormStructure();

  // Assign the address field another section than the other fields.
  form_structure->field(4)->section =
      Section::FromAutocomplete({.section = "another_section"});

  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_BadEmail) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultProfileFormStructure();

  // Change the value of the email field.
  ASSERT_EQ(form_structure->field(2)->Type().GetStorableType(), EMAIL_ADDRESS);
  form_structure->field(2)->value = u"bogus";

  // Verify that there was no import.
  ImportAddressProfileAndVerifyImportOfNoProfile(*form_structure);
}

// Tests that a 'confirm email' field does not block profile import.
TEST_P(FormDataImporterTest, ImportAddressProfiles_TwoEmails) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(
          {{NAME_FIRST, kDefaultFirstName},
           {NAME_LAST, kDefaultLastName},
           {EMAIL_ADDRESS, kDefaultMail},
           // Add two email fields with the same value.
           {EMAIL_ADDRESS, kDefaultMail},
           {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
           {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
           {ADDRESS_HOME_CITY, kDefaultCity},
           {ADDRESS_HOME_STATE, kDefaultState},
           {ADDRESS_HOME_ZIP, kDefaultZip}});

  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

// Tests two email fields containing different values blocks profile import.
TEST_P(FormDataImporterTest, ImportAddressProfiles_TwoDifferentEmails) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(
          {{NAME_FIRST, kDefaultFirstName},
           {NAME_LAST, kDefaultLastName},
           {EMAIL_ADDRESS, kDefaultMail},
           // Add two email fields with different values.
           {EMAIL_ADDRESS, "another@mail.com"},
           {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
           {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
           {ADDRESS_HOME_CITY, kDefaultCity},
           {ADDRESS_HOME_STATE, kDefaultState},
           {ADDRESS_HOME_ZIP, kDefaultZip}});

  ImportAddressProfileAndVerifyImportOfNoProfile(*form_structure);
}

// Tests that multiple phone numbers do not block profile import and the first
// one is saved.
TEST_P(FormDataImporterTest, ImportAddressProfiles_MultiplePhoneNumbers) {
  base::test::ScopedFeatureList enable_import_when_multiple_phones_feature;
  enable_import_when_multiple_phones_feature.InitAndEnableFeature(
      features::kAutofillEnableImportWhenMultiplePhoneNumbers);

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(
          {{NAME_FIRST, kDefaultFirstName},
           {NAME_LAST, kDefaultLastName},
           {EMAIL_ADDRESS, kDefaultMail},
           {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
           // Add a second phone field with a different number.
           {PHONE_HOME_WHOLE_NUMBER, kSecondPhone},
           {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
           {ADDRESS_HOME_CITY, kDefaultCity},
           {ADDRESS_HOME_STATE, kDefaultState},
           {ADDRESS_HOME_ZIP, kDefaultZip}});

  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

// Tests that multiple phone numbers do not block profile import and the first
// one is saved.
TEST_P(FormDataImporterTest,
       ImportAddressProfiles_MultiplePhoneNumbersSplitAcrossMultipleFields) {
  base::test::ScopedFeatureList enable_import_when_multiple_phones_feature;
  enable_import_when_multiple_phones_feature.InitAndEnableFeature(
      features::kAutofillEnableImportWhenMultiplePhoneNumbers);

  FormData form_data = ConstructFormDateFromTypeValuePairs(
      {{NAME_FIRST, kDefaultFirstName},
       {NAME_LAST, kDefaultLastName},
       {EMAIL_ADDRESS, kDefaultMail},
       // Add two phone number fields, split across 3 fields each.
       // They are all declared as PHONE_HOME_WHOLE_NUMBER, which only affects
       // the label. Local heuristics will classify them correctly.
       {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneAreaCode},
       {PHONE_HOME_WHOLE_NUMBER, kDefaultPhonePrefix},
       {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneSuffix},
       {PHONE_HOME_WHOLE_NUMBER, kSecondPhoneAreaCode},
       {PHONE_HOME_WHOLE_NUMBER, kSecondPhonePrefix},
       {PHONE_HOME_WHOLE_NUMBER, kSecondPhoneSuffix},
       {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
       {ADDRESS_HOME_CITY, kDefaultCity},
       {ADDRESS_HOME_STATE, kDefaultState},
       {ADDRESS_HOME_ZIP, kDefaultZip},
       {ADDRESS_HOME_COUNTRY, kDefaultCountry}});

  form_data.fields[3].max_length = 3;
  form_data.fields[4].max_length = 3;
  form_data.fields[5].max_length = 4;
  form_data.fields[6].max_length = 3;
  form_data.fields[7].max_length = 3;
  form_data.fields[8].max_length = 4;

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromFormData(form_data);

  ImportAddressProfilesAndVerifyExpectation(
      *form_structure,
      {ConstructProfileFromTypeValuePairs(
          {{NAME_FIRST, kDefaultFirstName},
           {NAME_LAST, kDefaultLastName},
           {EMAIL_ADDRESS, kDefaultMail},
           // Note that this formatting is without a country code.
           {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneDomesticFormatting},
           {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
           {ADDRESS_HOME_CITY, kDefaultCity},
           {ADDRESS_HOME_STATE, kDefaultState},
           {ADDRESS_HOME_ZIP, kDefaultZip},
           {ADDRESS_HOME_COUNTRY, kDefaultCountry}})});
}

// Tests that not enough filled fields will result in not importing an address.
TEST_P(FormDataImporterTest, ImportAddressProfiles_NotEnoughFilledFields) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(
          {{NAME_FIRST, kDefaultFirstName},
           {NAME_LAST, kDefaultLastName},
           {CREDIT_CARD_NUMBER, kDefaultCreditCardNumber}});

  ImportAddressProfileAndVerifyImportOfNoProfile(*form_structure);
  // Also verify that there was no import of a credit card.
  ASSERT_EQ(0U, personal_data_manager_->GetCreditCards().size());
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_MinimumAddressUSA) {
  TypeValuePairs type_value_pairs = {
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, "US"},
  };

  AutofillProfile profile =
      ConstructProfileFromTypeValuePairs(type_value_pairs);

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {profile});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_MinimumAddressGB) {
  TypeValuePairs type_value_pairs = {
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, "GB"},
  };

  AutofillProfile profile =
      ConstructProfileFromTypeValuePairs(type_value_pairs);

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {profile});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_MinimumAddressGI) {
  TypeValuePairs type_value_pairs = {
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_COUNTRY, "GI"},
  };

  AutofillProfile profile =
      ConstructProfileFromTypeValuePairs(type_value_pairs);

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {profile});
}

TEST_P(FormDataImporterTest,
       ImportAddressProfiles_PhoneNumberSplitAcrossMultipleFields) {
  FormData form_data = ConstructFormDateFromTypeValuePairs(
      {{NAME_FIRST, kDefaultFirstName},
       {NAME_LAST, kDefaultLastName},
       {EMAIL_ADDRESS, kDefaultMail},
       // Add three phone number fields.
       {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneAreaCode},
       {PHONE_HOME_WHOLE_NUMBER, kDefaultPhonePrefix},
       {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneSuffix},
       {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
       {ADDRESS_HOME_CITY, kDefaultCity},
       {ADDRESS_HOME_STATE, kDefaultState},
       {ADDRESS_HOME_ZIP, kDefaultZip},
       {ADDRESS_HOME_COUNTRY, kDefaultCountry}});

  // Define the length of the phone number fields to allow the parser to
  // identify them as area code, prefix and suffix.
  form_data.fields[3].max_length = 3;
  form_data.fields[4].max_length = 3;
  form_data.fields[5].max_length = 4;

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromFormData(form_data);
  ImportAddressProfilesAndVerifyExpectation(
      *form_structure,
      {ConstructProfileFromTypeValuePairs(
          {{NAME_FIRST, kDefaultFirstName},
           {NAME_LAST, kDefaultLastName},
           {EMAIL_ADDRESS, kDefaultMail},
           {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneDomesticFormatting},
           {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
           {ADDRESS_HOME_CITY, kDefaultCity},
           {ADDRESS_HOME_STATE, kDefaultState},
           {ADDRESS_HOME_ZIP, kDefaultZip},
           {ADDRESS_HOME_COUNTRY, kDefaultCountry}})});
}

// Test that even from unfocusable fields we import.
TEST_P(FormDataImporterTest, ImportAddressProfiles_UnFocussableFields) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultProfileFormStructure();
  // Set the Address line field as unfocusable.
  form_structure->field(4)->is_focusable = false;
  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_MultilineAddress) {
  TypeValuePairs type_value_pairs = {
      {NAME_FULL, kDefaultFullName},
      // This is a multi-line field.
      {ADDRESS_HOME_STREET_ADDRESS, kDefaultStreetAddress},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, "US"},
  };

  AutofillProfile profile =
      ConstructProfileFromTypeValuePairs(type_value_pairs);

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {profile});
}

TEST_P(FormDataImporterTest,
       ImportAddressProfiles_TwoValidProfilesDifferentForms) {
  std::unique_ptr<FormStructure> default_form_structure =
      ConstructDefaultProfileFormStructure();

  AutofillProfile default_profile = ConstructDefaultProfile();
  ImportAddressProfilesAndVerifyExpectation(*default_form_structure,
                                            {default_profile});

  // Now import a second profile from a different form submission.
  std::unique_ptr<FormStructure> alternative_form_structure =
      ConstructSecondProfileFormStructure();
  AutofillProfile alternative_profile = ConstructSecondProfile();

  // Verify that both profiles have been imported.
  ImportAddressProfilesAndVerifyExpectation(
      *alternative_form_structure, {alternative_profile, default_profile});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_TwoValidProfilesSameForm) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructShippingAndBillingFormStructure();
  ImportAddressProfilesAndVerifyExpectation(
      *form_structure, {ConstructDefaultProfile(), ConstructSecondProfile()});
}

TEST_P(FormDataImporterTest,
       ImportAddressProfiles_OneValidProfileSameForm_PartsHidden) {
  FormData form_data = ConstructDefaultFormData();

  FormData hidden_second_form = form_data;
  for (FormFieldData& field : hidden_second_form.fields) {
    // Reset the values and make the field non focusable.
    field.value = u"";
    field.is_focusable = false;
  }

  // Append the fields of the second form to the first form.
  form_data.fields.insert(form_data.fields.end(),
                          hidden_second_form.fields.begin(),
                          hidden_second_form.fields.end());

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromFormData(form_data);
  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

// A maximum of two address profiles are imported per form.
// This test is flaky for an unknown reason.
// TODO(crbug.com/1297212): Understand flakiness.
TEST_P(FormDataImporterTest,
       DISABLED_ImportAddressProfiles_ThreeValidProfilesSameForm) {
  TypeValuePairs profile_type_value_pairs = GetDefaultProfileTypeValuePairs();

  TypeValuePairs second_profile_type_value_pairs =
      GetSecondProfileTypeValuePairs();

  TypeValuePairs third_profile_type_value_pairs =
      GetThirdProfileTypeValuePairs();

  // Merge the type value pairs into one and construct the corresponding form
  // structure.
  profile_type_value_pairs.insert(profile_type_value_pairs.end(),
                                  second_profile_type_value_pairs.begin(),
                                  second_profile_type_value_pairs.end());
  profile_type_value_pairs.insert(profile_type_value_pairs.end(),
                                  third_profile_type_value_pairs.begin(),
                                  third_profile_type_value_pairs.end());

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(profile_type_value_pairs);

  // Import from the form structure and verify that only the first two profiles
  // are imported.
  ImportAddressProfilesAndVerifyExpectation(
      *form_structure, {ConstructDefaultProfile(), ConstructSecondProfile()});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_SameProfileWithConflict) {
  TypeValuePairs initial_type_value_pairs{
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, kDefaultCountry},
      {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneDomesticFormatting},
  };
  AutofillProfile initial_profile =
      ConstructProfileFromTypeValuePairs(initial_type_value_pairs);

  std::unique_ptr<FormStructure> initial_form_structure =
      ConstructFormStructureFromTypeValuePairs(initial_type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*initial_form_structure,
                                            {initial_profile});

  // Create a second form structure with an additional country and a differently
  // formatted phone number
  TypeValuePairs conflicting_type_value_pairs = {
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      // The phone number is spelled differently.
      {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneAlternativeFormatting},
      // Country information is added.
      {ADDRESS_HOME_COUNTRY, "US"}};
  AutofillProfile conflicting_profile =
      ConstructProfileFromTypeValuePairs(conflicting_type_value_pairs);

  // Verify that the initial profile and the conflicting profile are not the
  // same.
  ASSERT_FALSE(initial_profile.Compare(conflicting_profile) == 0);
  std::unique_ptr<FormStructure> conflicting_form_structure =
      ConstructFormStructureFromTypeValuePairs(conflicting_type_value_pairs);

  TypeValuePairs resulting_type_value_pairs{
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      // The phone number remains in domestic format.
      {PHONE_HOME_WHOLE_NUMBER, kDefaultPhoneDomesticFormatting},
      // Country information is added.
      {ADDRESS_HOME_COUNTRY, "US"}};

  // Verify that importing the conflicting profile will result in an update of
  // the existing profile rather than creating a new one.
  ImportAddressProfilesAndVerifyExpectation(
      *conflicting_form_structure,
      {ConstructProfileFromTypeValuePairs(resulting_type_value_pairs)});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_MissingInfoInOld) {
  TypeValuePairs initial_type_value_pairs{
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, kDefaultCountry},
      {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
  };
  AutofillProfile initial_profile =
      ConstructProfileFromTypeValuePairs(initial_type_value_pairs);

  std::unique_ptr<FormStructure> initial_form_structure =
      ConstructFormStructureFromTypeValuePairs(initial_type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*initial_form_structure,
                                            {initial_profile});

  // Create a superset that includes a new email address.
  TypeValuePairs superset_type_value_pairs = initial_type_value_pairs;
  superset_type_value_pairs.emplace_back(EMAIL_ADDRESS, kDefaultMail);

  AutofillProfile superset_profile =
      ConstructProfileFromTypeValuePairs(superset_type_value_pairs);

  // Verify that the initial profile and the superset profile are not the
  // same.
  ASSERT_FALSE(initial_profile.Compare(superset_profile) == 0);

  std::unique_ptr<FormStructure> superset_form_structure =
      ConstructFormStructureFromTypeValuePairs(superset_type_value_pairs);
  // Verify that importing the superset profile will result in an update of
  // the existing profile rather than creating a new one.
  ImportAddressProfilesAndVerifyExpectation(*superset_form_structure,
                                            {superset_profile});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_MissingInfoInNew) {
  TypeValuePairs subset_type_value_pairs({
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_STATE, kDefaultState},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, kDefaultCountry},
      {PHONE_HOME_WHOLE_NUMBER, kDefaultPhone},
  });
  // Create a superset that includes a new email address.
  TypeValuePairs superset_type_value_pairs = subset_type_value_pairs;
  superset_type_value_pairs.emplace_back(EMAIL_ADDRESS, kDefaultMail);

  AutofillProfile subset_profile =
      ConstructProfileFromTypeValuePairs(subset_type_value_pairs);
  AutofillProfile superset_profile =
      ConstructProfileFromTypeValuePairs(superset_type_value_pairs);

  // Verify that the subset profile and the superset profile are not the
  // same.
  ASSERT_FALSE(subset_profile.Compare(superset_profile) == 0);

  // First import the superset profile.
  std::unique_ptr<FormStructure> superset_form_structure =
      ConstructFormStructureFromTypeValuePairs(superset_type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*superset_form_structure,
                                            {superset_profile});

  // Than import the subset profile and verify that the stored profile is still
  // the superset.
  std::unique_ptr<FormStructure> subset_form_structure =
      ConstructFormStructureFromTypeValuePairs(subset_type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(*superset_form_structure,
                                            {superset_profile});
}

TEST_P(FormDataImporterTest, ImportAddressProfiles_InsufficientAddress) {
  // This address is missing a state which is required in the US.
  TypeValuePairs type_value_pairs = {
      {NAME_FULL, kDefaultFullName},
      {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
      {ADDRESS_HOME_CITY, kDefaultCity},
      {ADDRESS_HOME_ZIP, kDefaultZip},
      {ADDRESS_HOME_COUNTRY, "US"},
  };

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  // Verify that no profile is imported.
  ImportAddressProfileAndVerifyImportOfNoProfile(*form_structure);
}

// Ensure that if a verified profile already exists, aggregated profiles cannot
// modify it in any way. This also checks the profile merging/matching algorithm
// works: if either the full name OR all the non-empty name pieces match, the
// profile is a match.
TEST_P(FormDataImporterTest,
       ImportAddressProfiles_ExistingVerifiedProfileWithConflict) {
  // Start with a verified profile.
  AutofillProfile profile(base::GenerateGUID(), kSettingsOrigin);
  test::SetProfileInfo(&profile, "Marion", "Mitchell", "Morrison",
                       "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5",
                       "Hollywood", "CA", "91601", "US", "12345678910");
  EXPECT_TRUE(profile.IsVerified());

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  personal_data_manager_->AddProfile(profile);
  run_loop.Run();

  // Simulate a form submission with conflicting info.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "Marion Mitchell",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Morrison", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "johnwayne@me.xyz", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "123 Zoo St.", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "Hollywood", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "CA", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "91601", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(profile));

  // Try the same thing, but without "Mitchell". The profiles should still match
  // because the non empty name pieces (first and last) match that stored in the
  // profile.
  test::CreateTestFormField("First name:", "first_name", "Marion", "text",
                            &field);
  form.fields[0] = field;

  FormStructure form_structure2(form);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);

  ImportAddressProfiles(/*extraction_successful=*/true, form_structure2);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results2 =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(profile));
}

TEST_P(FormDataImporterTest,
       IncorporateStructuredNameInformationInVerifiedProfile) {
  // This test is only applicable to structured names.
  if (!structured_address::StructuredNamesEnabled()) {
    return;
  }

  // Start with a verified profile.
  AutofillProfile profile(base::GenerateGUID(), kSettingsOrigin);
  test::SetProfileInfo(&profile, "Marion", "Mitchell", "Morrison",
                       "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5",
                       "Hollywood", "CA", "91601", "US", "12345678910");
  EXPECT_TRUE(profile.IsVerified());

  // Set the verification status for the first and middle name to parsed.
  profile.SetRawInfoWithVerificationStatus(
      NAME_FIRST, u"Marion", structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_MIDDLE, u"Mitchell",
      structured_address::VerificationStatus::kParsed);

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  personal_data_manager_->AddProfile(profile);
  run_loop.Run();

  // Simulate a form submission with conflicting info.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "Marion Mitchell",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Morrison", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "johnwayne@me.xyz", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "123 Zoo St.", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "Hollywood", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "CA", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "91601", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  // The form submission should result in a change of name structure.
  profile.SetRawInfoWithVerificationStatus(
      NAME_FIRST, u"Marion Mitchell",
      structured_address::VerificationStatus::kObserved);
  profile.SetRawInfoWithVerificationStatus(
      NAME_MIDDLE, u"", structured_address::VerificationStatus::kNoStatus);
  profile.SetRawInfoWithVerificationStatus(
      NAME_LAST, u"Morrison",
      structured_address::VerificationStatus::kObserved);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(profile));

  // Try the same thing, but without "Mitchell". The profiles should still match
  // because "Marion Morrison" is a variant of the known full name.
  test::CreateTestFormField("First name:", "first_name", "Marion", "text",
                            &field);
  form.fields[0] = field;

  FormStructure form_structure2(form);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);

  ImportAddressProfiles(/*extraction_successful=*/true, form_structure2);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results2 =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(profile));
}

TEST_P(FormDataImporterTest,
       IncorporateStructuredAddressInformationInVerififedProfile) {
  // This test is only applicable to structured addresses.
  if (!structured_address::StructuredAddressesEnabled()) {
    return;
  }

  // Start with a verified profile.
  AutofillProfile profile(base::GenerateGUID(), kSettingsOrigin);
  test::SetProfileInfo(&profile, "Marion", "Mitchell", "Morrison",
                       "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5",
                       "Hollywood", "CA", "91601", "US", "12345678910");
  EXPECT_TRUE(profile.IsVerified());

  // Reset the structured address to emulate a failed parsing attempt.
  profile.SetRawInfoWithVerificationStatus(
      ADDRESS_HOME_HOUSE_NUMBER, u"",
      structured_address::VerificationStatus::kNoStatus);
  profile.SetRawInfoWithVerificationStatus(
      ADDRESS_HOME_STREET_NAME, u"",
      structured_address::VerificationStatus::kNoStatus);
  profile.SetRawInfoWithVerificationStatus(
      ADDRESS_HOME_STREET_AND_DEPENDENT_STREET_NAME, u"",
      structured_address::VerificationStatus::kNoStatus);

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  personal_data_manager_->AddProfile(profile);
  run_loop.Run();

  // Simulate a form submission with conflicting info.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "Marion Mitchell",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Morrison", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "johnwayne@me.xyz", "text",
                            &field);
  form.fields.push_back(field);
  // This forms contains structured address information.
  test::CreateTestFormField("Street Name:", "street_name", "Zoo St.", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("House Number:", "house_number", "123", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "Hollywood", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "CA", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "91601", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  // The form submission should result in a change of the address structure.
  profile.SetRawInfoWithVerificationStatus(
      ADDRESS_HOME_STREET_AND_DEPENDENT_STREET_NAME, u"Zoo St.",
      structured_address::VerificationStatus::kFormatted);
  profile.SetRawInfoWithVerificationStatus(
      ADDRESS_HOME_STREET_NAME, u"Zoo St.",
      structured_address::VerificationStatus::kObserved);
  profile.SetRawInfoWithVerificationStatus(
      ADDRESS_HOME_HOUSE_NUMBER, u"123",
      structured_address::VerificationStatus::kObserved);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(profile));
}

// Tests that no profile is inferred if the country is not recognized.
TEST_P(FormDataImporterTest, ImportAddressProfiles_UnrecognizedCountry) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country", "Notacountry", "text",
                            &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/false, form_structure);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  ASSERT_EQ(0U, personal_data_manager_->GetProfiles().size());
  ASSERT_EQ(0U, personal_data_manager_->GetCreditCards().size());
}

// Tests that a profile is imported if the country can be translated using the
// page language.
TEST_P(FormDataImporterTest, ImportAddressProfiles_LocalizedCountryName) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  // Create a form with all important fields.
  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  // The country field has a localized value.
  test::CreateTestFormField("Country:", "country", "Armenien", "text", &field);
  form.fields.push_back(field);

  // Set up language state mock.
  autofill_client_->GetLanguageState()->SetSourceLanguage("");

  // Verify that the country code is not determined from the country value if
  // the page language is not set.
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/false, form_structure);

  ASSERT_EQ(0U, personal_data_manager_->GetProfiles().size());
  ASSERT_EQ(0U, personal_data_manager_->GetCreditCards().size());

  // Set the page language to match the localized country value and try again.
  autofill_client_->GetLanguageState()->SetSourceLanguage("de");

  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  // There should be one imported address profile.
  ASSERT_EQ(1U, personal_data_manager_->GetProfiles().size());
  ASSERT_EQ(0U, personal_data_manager_->GetCreditCards().size());

  // Check that the correct profile was stored.
  AutofillProfile expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&expected, "George", nullptr, "Washington",
                       "theprez@gmail.com", nullptr, "21 Laussat St", nullptr,
                       "San Francisco", "California", "94102", "AM", nullptr);
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  EXPECT_THAT(*results[0], ComparesEqual(expected));
}

// Tests that a profile is created for countries with composed names.
TEST_P(FormDataImporterTest,
       ImportAddressProfiles_CompleteComposedCountryName) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1",
                            "No. 43 Bo Aung Gyaw Street", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "Yangon", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "11181", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country", "Myanmar [Burma]", "text",
                            &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/true, form_structure);

  AutofillProfile expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&expected, "George", nullptr, "Washington",
                       "theprez@gmail.com", nullptr,
                       "No. 43 Bo Aung Gyaw Street", nullptr, "Yangon", "",
                       "11181", "MM", nullptr);
  EXPECT_THAT(personal_data_manager_->GetProfiles(),
              UnorderedElementsCompareEqual(expected));
}

// TODO(crbug.com/634131): Create profiles if part of a standalone part of a
// composed country name is present.
// Tests that a profile is created if a standalone part of a composed country
// name is present.
TEST_P(FormDataImporterTest,
       ImportAddressProfiles_IncompleteComposedCountryName) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country",
                            "Myanmar",  // Missing the [Burma] part
                            "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/false, form_structure);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  ASSERT_EQ(0U, personal_data_manager_->GetProfiles().size());
  ASSERT_EQ(0U, personal_data_manager_->GetCreditCards().size());
}

// ImportCreditCard tests.

// Tests that a valid credit card is extracted.
TEST_P(FormDataImporterTest, ImportCreditCard_Valid) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultCreditCardFormStructure();
  std::unique_ptr<CreditCard> imported_credit_card;
  base::HistogramTester histogram_tester;
  EXPECT_TRUE(ImportCreditCard(*form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample(
      "Autofill.SubmittedCardState",
      AutofillMetrics::HAS_CARD_NUMBER_AND_EXPIRATION_DATE, 1);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");  // Imported cards have no billing info.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));
}

// Tests that an invalid credit card number is not extracted.
TEST_P(FormDataImporterTest, ImportCreditCard_InvalidCardNumber) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Jim Johansen", "1000000000000000", "02",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  base::HistogramTester histogram_tester;
  EXPECT_FALSE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_FALSE(imported_credit_card);
  histogram_tester.ExpectUniqueSample("Autofill.SubmittedCardState",
                                      AutofillMetrics::HAS_EXPIRATION_DATE_ONLY,
                                      1);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  ASSERT_EQ(0U, personal_data_manager_->GetCreditCards().size());
}

// Tests that a credit card with an empty expiration can be extracted due to the
// expiration date fix flow.
TEST_P(FormDataImporterTest,
       ImportCreditCard_InvalidExpiryDate_EditableExpirationExpOn) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Smalls Biggie", "4111-1111-1111-1111", "", "");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  base::HistogramTester histogram_tester;
  EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample("Autofill.SubmittedCardState",
                                      AutofillMetrics::HAS_CARD_NUMBER_ONLY, 1);
}

// Tests that an expired credit card can be extracted due to the expiration date
// fix flow.
TEST_P(FormDataImporterTest,
       ImportCreditCard_ExpiredExpiryDate_EditableExpirationExpOn) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Smalls Biggie", "4111-1111-1111-1111", "01",
                        "2000");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  base::HistogramTester histogram_tester;
  EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample("Autofill.SubmittedCardState",
                                      AutofillMetrics::HAS_CARD_NUMBER_ONLY, 1);
}

// Tests that a valid credit card is extracted when the option text for month
// select can't be parsed but its value can.
TEST_P(FormDataImporterTest, ImportCreditCard_MonthSelectInvalidText) {
  // Add a single valid credit card form with an invalid option value.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111",
                        "Feb (2)", "2999");
  // Add option values and contents to the expiration month field.
  ASSERT_EQ(u"exp_month", form.fields[2].name);
  form.fields[2].options = {
      {.value = u"1", .content = u"Jan (1)"},
      {.value = u"2", .content = u"Feb (2)"},
      {.value = u"3", .content = u"Mar (3)"},
  };

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  base::HistogramTester histogram_tester;
  EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample(
      "Autofill.SubmittedCardState",
      AutofillMetrics::HAS_CARD_NUMBER_AND_EXPIRATION_DATE, 1);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  // See that the invalid option text was converted to the right value.
  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "02",
                          "2999", "");  // Imported cards have no billing info.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));
}

TEST_P(FormDataImporterTest, ImportCreditCard_TwoValidCards) {
  // Start with a single valid credit card form.
  std::unique_ptr<FormStructure> form_structure1 =
      ConstructDefaultCreditCardFormStructure();
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(*form_structure1, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");  // Imported cards have no billing info.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));

  // Add a second different valid credit card.
  FormData form2;
  form2.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form2, "", "5500 0000 0000 0004", "02", "2999");

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);

  std::unique_ptr<CreditCard> imported_credit_card2;
  EXPECT_TRUE(ImportCreditCard(form_structure2, false, &imported_credit_card2));
  ASSERT_TRUE(imported_credit_card2);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card2);

  WaitForOnPersonalDataChanged();

  CreditCard expected2(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected2, "", "5500000000000004", "02", "2999",
                          "");  // Imported cards have no billing info.
  // We ignore the order because multiple profiles or credit cards that
  // are added to the SQLite DB within the same second will be returned in GUID
  // (i.e., random) order.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected, expected2));
}

// This form has the expiration year as one field with MM/YY.
TEST_P(FormDataImporterTest, ImportCreditCard_Month2DigitYearCombination) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "John MMYY",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4111111111111111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Date:", "exp_date", "05/45", "text", "cc-exp",
                            5, &field);
  form.fields.push_back(field);

  SubmitFormAndExpectImportedCardWithData(form, "John MMYY", "4111111111111111",
                                          "05", "2045");
}

// This form has the expiration year as one field with MM/YYYY.
TEST_P(FormDataImporterTest, ImportCreditCard_Month4DigitYearCombination) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "John MMYYYY",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4111111111111111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Date:", "exp_date", "05/2045", "text",
                            "cc-exp", 7, &field);
  form.fields.push_back(field);

  SubmitFormAndExpectImportedCardWithData(form, "John MMYYYY",
                                          "4111111111111111", "05", "2045");
}

// This form has the expiration year as one field with M/YYYY.
TEST_P(FormDataImporterTest, ImportCreditCard_1DigitMonth4DigitYear) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "John MYYYY",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4111111111111111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Date:", "exp_date", "5/2045", "text", "cc-exp",
                            &field);
  form.fields.push_back(field);

  SubmitFormAndExpectImportedCardWithData(form, "John MYYYY",
                                          "4111111111111111", "05", "2045");
}

// This form has the expiration year as a 2-digit field.
TEST_P(FormDataImporterTest, ImportCreditCard_2DigitYear) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "John Smith",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4111111111111111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "05", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "45", "text", &field);
  field.max_length = 2;
  form.fields.push_back(field);

  SubmitFormAndExpectImportedCardWithData(form, "John Smith",
                                          "4111111111111111", "05", "2045");
}

// Tests that a credit card is extracted when the card matches a masked server
// card.
TEST_P(FormDataImporterTest,
       ImportCreditCard_DuplicateServerCards_ExtractMaskedCard) {
  // Add a masked server card.
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::MASKED_SERVER_CARD, "a123"));
  test::SetCreditCardInfo(&server_cards.back(), "John Dillinger",
                          "1111" /* Visa */, "01", "2999", "");
  server_cards.back().SetNetworkForMaskedCard(kVisaCard);
  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // Type the same data as the masked card into a form.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "John Dillinger", "4111111111111111", "01",
                        "2999");

  // The card should not be offered to be saved locally because the feature flag
  // is disabled.
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_FALSE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  ASSERT_TRUE(imported_credit_card->record_type() ==
              CreditCard::MASKED_SERVER_CARD);
}

// Tests that a credit card is extracted when it matches a full server
// card.
TEST_P(FormDataImporterTest,
       ImportCreditCard_DuplicateServerCards_ExtractFullCard) {
  // Add a full server card.
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "378282246310005" /* American Express */, "04",
                          "2999", "");  // Imported cards have no billing info.
  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // Type the same data as the unmasked card into a form.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Clyde Barrow", "378282246310005", "04", "2999");

  // The card should not be offered to be saved locally because it only matches
  // the full server card.
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_FALSE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  ASSERT_TRUE(imported_credit_card->record_type() ==
              CreditCard::RecordType::FULL_SERVER_CARD);
}

TEST_P(FormDataImporterTest, ImportCreditCard_SameCreditCardWithConflict) {
  // Start with a single valid credit card form.
  FormData form1;
  form1.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form1, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2998");

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure1, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "01",
                          "2998", "");  // Imported cards have no billing info.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));

  // Add a second different valid credit card where the year is different but
  // the credit card number matches.
  FormData form2;
  form2.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form2, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        /* different year */ "2999");

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card2;
  EXPECT_TRUE(ImportCreditCard(form_structure2, false, &imported_credit_card2));
  EXPECT_TRUE(imported_credit_card2);

  WaitForOnPersonalDataChanged();

  // Expect that the newer information is saved.  In this case the year is
  // updated to "2999".
  CreditCard expected2(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected2, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");  // Imported cards have no billing info.
  const std::vector<CreditCard*>& results2 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(expected2));
}

TEST_P(FormDataImporterTest, ImportCreditCard_ShouldReturnLocalCard) {
  // Start with a single valid credit card form.
  FormData form1;
  form1.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form1, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2998");

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure1, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "01",
                          "2998", "");  // Imported cards have no billing info.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));

  // Add a second different valid credit card where the year is different but
  // the credit card number matches.
  FormData form2;
  form2.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form2, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        /* different year */ "2999");

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card2;
  EXPECT_TRUE(ImportCreditCard(form_structure2,
                               /* should_return_local_card= */ true,
                               &imported_credit_card2));
  // The local card is returned after an update.
  EXPECT_TRUE(imported_credit_card2);

  WaitForOnPersonalDataChanged();

  // Expect that the newer information is saved.  In this case the year is
  // updated to "2999".
  CreditCard expected2(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected2, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");  // Imported cards have no billing info.
  const std::vector<CreditCard*>& results2 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(expected2));
}

TEST_P(FormDataImporterTest, ImportCreditCard_EmptyCardWithConflict) {
  // Start with a single valid credit card form.
  FormData form1;
  form1.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form1, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2998");

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes(nullptr, nullptr);

  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure1, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "01",
                          "2998", "");  // Imported cards have no billing info.
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));

  // Add a second credit card with no number.
  FormData form2;
  form2.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form2, "Biggie Smalls", /* no number */ nullptr, "01",
                        "2999");

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card2;
  EXPECT_FALSE(
      ImportCreditCard(form_structure2, false, &imported_credit_card2));
  EXPECT_FALSE(imported_credit_card2);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  // No change is expected.
  CreditCard expected2(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected2, "Biggie Smalls", "4111111111111111", "01",
                          "2998", "");
  const std::vector<CreditCard*>& results2 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(expected2));
}

TEST_P(FormDataImporterTest, ImportCreditCard_MissingInfoInNew) {
  // Start with a single valid credit card form.
  FormData form1;
  form1.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form1, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure1(form1);
  form_structure1.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure1, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  CreditCard expected(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");
  EXPECT_THAT(personal_data_manager_->GetCreditCards(),
              UnorderedElementsCompareEqual(expected));

  // Add a second different valid credit card where the name is missing but
  // the credit card number matches.
  FormData form2;
  form2.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form2, /* missing name */ nullptr,
                        "4111-1111-1111-1111", "01", "2999");

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card2;
  EXPECT_TRUE(ImportCreditCard(form_structure2, false, &imported_credit_card2));
  EXPECT_TRUE(imported_credit_card2);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  // No change is expected.
  CreditCard expected2(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected2, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");
  const std::vector<CreditCard*>& results2 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(expected2));

  // Add a third credit card where the expiration date is missing.
  FormData form3;
  form3.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form3, "Johnny McEnroe", "5555555555554444",
                        /* no month */ nullptr,
                        /* no year */ nullptr);

  FormStructure form_structure3(form3);
  form_structure3.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card3;
  EXPECT_FALSE(
      ImportCreditCard(form_structure3, false, &imported_credit_card3));
  ASSERT_FALSE(imported_credit_card3);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  // No change is expected.
  CreditCard expected3(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected3, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "");
  const std::vector<CreditCard*>& results3 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results3.size());
  EXPECT_THAT(*results3[0], ComparesEqual(expected3));
}

TEST_P(FormDataImporterTest, ImportCreditCard_MissingInfoInOld) {
  // Start with a single valid credit card stored via the preferences.
  // Note the empty name.
  CreditCard saved_credit_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&saved_credit_card, "", "4111111111111111" /* Visa */,
                          "01", "2998", "1");
  personal_data_manager_->AddCreditCard(saved_credit_card);

  WaitForOnPersonalDataChanged();

  const std::vector<CreditCard*>& results1 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results1.size());
  EXPECT_EQ(saved_credit_card, *results1[0]);

  // Add a second different valid credit card where the year is different but
  // the credit card number matches.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        /* different year */ "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
  EXPECT_TRUE(imported_credit_card);

  WaitForOnPersonalDataChanged();

  // Expect that the newer information is saved.  In this case the year is
  // added to the existing credit card.
  CreditCard expected2(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected2, "Biggie Smalls", "4111111111111111", "01",
                          "2999", "1");
  const std::vector<CreditCard*>& results2 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(expected2));
}

// We allow the user to store a credit card number with separators via the UI.
// We should not try to re-aggregate the same card with the separators stripped.
TEST_P(FormDataImporterTest, ImportCreditCard_SameCardWithSeparators) {
  // Start with a single valid credit card stored via the preferences.
  // Note the separators in the credit card number.
  CreditCard saved_credit_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&saved_credit_card, "Biggie Smalls",
                          "4111 1111 1111 1111" /* Visa */, "01", "2999", "");
  personal_data_manager_->AddCreditCard(saved_credit_card);

  WaitForOnPersonalDataChanged();

  const std::vector<CreditCard*>& results1 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results1.size());
  EXPECT_THAT(*results1[0], ComparesEqual(saved_credit_card));

  // Import the same card info, but with different separators in the number.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
  EXPECT_TRUE(imported_credit_card);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  // Expect that no new card is saved.
  const std::vector<CreditCard*>& results2 =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results2.size());
  EXPECT_THAT(*results2[0], ComparesEqual(saved_credit_card));
}

// Ensure that if a verified credit card already exists, aggregated credit cards
// cannot modify it in any way.
TEST_P(FormDataImporterTest,
       ImportCreditCard_ExistingVerifiedCardWithConflict) {
  // Start with a verified credit card.
  CreditCard credit_card(base::GenerateGUID(), kSettingsOrigin);
  test::SetCreditCardInfo(&credit_card, "Biggie Smalls",
                          "4111 1111 1111 1111" /* Visa */, "01", "2998", "");
  EXPECT_TRUE(credit_card.IsVerified());

  // Add the credit card to the database.
  personal_data_manager_->AddCreditCard(credit_card);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // Simulate a form submission with conflicting expiration year.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        /* different year */ "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_TRUE(ImportCreditCard(form_structure, false, &imported_credit_card));
  ASSERT_TRUE(imported_credit_card);

  // Since no refresh is expected, reload the data from the database to make
  // sure no changes were written out.
  ResetPersonalDataManager(USER_MODE_NORMAL);

  // Expect that the saved credit card is not modified.
  const std::vector<CreditCard*>& results =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(credit_card));
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// and reset correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_SecondImportResetsCreditCardRecordType) {
  // Start with a single valid credit card stored via the preferences.
  CreditCard saved_credit_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&saved_credit_card, "Biggie Smalls",
                          "4111 1111 1111 1111" /* Visa */, "01", "2999", "");
  personal_data_manager_->AddCreditCard(saved_credit_card);

  WaitForOnPersonalDataChanged();

  const std::vector<CreditCard*>& results =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(saved_credit_card));

  // Simulate a form submission with the same card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // LOCAL_CARD because upload was offered and the card is a local card already
  // on the device.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::LOCAL_CARD);

  // Second form is filled with a new card so
  // `FormDataImporterTest::imported_credit_card_record_type_` should be reset.
  // Simulate a form submission with a new card.
  FormData form2;
  form2.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form2, "Biggie Smalls", "4012888888881881", "01",
                        "2999");

  FormStructure form_structure2(form2);
  form_structure2.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card2;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure2, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card2,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card2);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // NEW_CARD because the imported card is not already on the device.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NEW_CARD);

  // Third form is an address form and set |credit_card_autofill_enabled| to be
  // false so that the ImportCreditCard won't be called.
  // `FormDataImporterTest::imported_credit_card_record_type_` should still be
  // reset even if ImportCreditCard is not called. Simulate a form submission
  // with no card.
  FormData form3;
  form3.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form3.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form3.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "bogus@example.com", "text",
                            &field);
  form3.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form3.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form3.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form3.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form3.fields.push_back(field);
  FormStructure form_structure3(form3);
  form_structure3.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card3;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure3, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/false,
      /*should_return_local_card=*/true, &imported_credit_card3,
      &imported_upi_id));
  // `FormDataImporterTest::imported_credit_card_record_type_` should be NO_CARD
  // because no valid card was imported from the form.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NO_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_NewCard) {
  // Simulate a form submission with a new credit card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // NEW_CARD because the imported card is not already on the device.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NEW_CARD);
}

// Ensures that `imported_credit_card_record_type_` is set correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_LocalCard) {
  // Start with a single valid credit card stored via the preferences.
  CreditCard saved_credit_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&saved_credit_card, "Biggie Smalls",
                          "4111 1111 1111 1111" /* Visa */, "01", "2999", "");
  personal_data_manager_->AddCreditCard(saved_credit_card);

  WaitForOnPersonalDataChanged();

  const std::vector<CreditCard*>& results =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(saved_credit_card));

  // Simulate a form submission with the same card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // LOCAL_CARD because upload was offered and the card is a local card already
  // on the device.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::LOCAL_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_MaskedServerCard) {
  // Add a masked server card.
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::MASKED_SERVER_CARD, "a123"));
  test::SetCreditCardInfo(&server_cards.back(), "Biggie Smalls",
                          "1111" /* Visa */, "01", "2999", "");
  server_cards.back().SetNetworkForMaskedCard(kVisaCard);
  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // Simulate a form submission with the same masked server card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // SERVER_CARD.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::SERVER_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_FullServerCard) {
  // Add a full server card.
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Biggie Smalls",
                          "378282246310005" /* American Express */, "04",
                          "2999", "1");
  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // Simulate a form submission with the same full server card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "378282246310005", "04",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // SERVER_CARD.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::SERVER_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_NoCard_InvalidCardNumber) {
  // Simulate a form submission using a credit card with an invalid card number.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1112", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_FALSE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be NO_CARD
  // because no valid card was successfully imported from the form.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NO_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_NoCard_VirtualCard) {
  // Simulate a form submission using a credit card that is known as a virtual
  // card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");
  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        "2999");
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  form_data_importer_->CacheFetchedVirtualCard(u"1111");
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;

  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_FALSE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be NO_CARD
  // because the card imported from the form was a virtual card.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NO_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(
    FormDataImporterTest,
    ImportFormData_ImportCreditCardRecordType_NewCard_ExpiredCard_WithExpDateFixFlow) {
  // Simulate a form submission with an expired credit card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  AddFullCreditCardForm(&form, "Biggie Smalls", "4111 1111 1111 1111", "01",
                        "1999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be
  // NEW_CARD because card was successfully imported from the form via the
  // expiration date fix flow.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NEW_CARD);
}

// Ensures that `FormDataImporterTest::imported_credit_card_record_type_` is set
// correctly.
TEST_P(FormDataImporterTest,
       ImportFormData_ImportCreditCardRecordType_NoCard_NoCardOnForm) {
  // Simulate a form submission with no credit card on form.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "bogus@example.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_FALSE(imported_credit_card);
  // `FormDataImporterTest::imported_credit_card_record_type_` should be NO_CARD
  // because the form doesn't have credit card section.
  ASSERT_TRUE(
      form_data_importer_->imported_credit_card_record_type_for_testing() ==
      FormDataImporter::ImportedCreditCardRecordType::NO_CARD);
}

// ImportFormData tests (both addresses and credit cards).

// Test that a form with both address and credit card sections imports the
// address and the credit card.
TEST_P(FormDataImporterTest, ImportFormData_OneAddressOneCreditCard) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  // Address section.
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country", "US", "text", &field);
  form.fields.push_back(field);

  // Credit card section.
  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  // Test that the address has been saved.
  AutofillProfile expected_address(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&expected_address, "George", nullptr, "Washington",
                       "theprez@gmail.com", nullptr, "21 Laussat St", nullptr,
                       "San Francisco", "California", "94102", "US", nullptr);
  const std::vector<AutofillProfile*>& results_addr =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results_addr.size());
  EXPECT_THAT(*results_addr[0], ComparesEqual(expected_address));

  // Test that the credit card has also been saved.
  CreditCard expected_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected_card, "Biggie Smalls", "4111111111111111",
                          "01", "2999", "");
  const std::vector<CreditCard*>& results_cards =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results_cards.size());
  EXPECT_THAT(*results_cards[0], ComparesEqual(expected_card));
}

// Test that a form with two address sections and a credit card section does not
// import the address but does import the credit card.
TEST_P(FormDataImporterTest, ImportFormData_TwoAddressesOneCreditCard) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  // Address section 1.
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);

  // Address section 2.
  test::CreateTestFormField("Address:", "address", "1600 Pennsylvania Avenue",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Name:", "name", "Barack Obama", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "Washington", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "DC", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "20500", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country", "USA", "text", &field);
  form.fields.push_back(field);

  // Credit card section.
  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillRepeatedly(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .Times(testing::AnyNumber());
  absl::optional<std::string> imported_upi_id;
  // Still returns true because the credit card import was successful.
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  run_loop.Run();

  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  // Test that both addresses have been saved.
  EXPECT_EQ(2U, personal_data_manager_->GetProfiles().size());

  // Test that the credit card has been saved.
  CreditCard expected_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected_card, "Biggie Smalls", "4111111111111111",
                          "01", "2999", "");
  const std::vector<CreditCard*>& results =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(expected_card));
}

// Test that a form with both address and credit card sections imports only the
// the credit card if addresses are disabled.
TEST_P(FormDataImporterTest, ImportFormData_AddressesDisabledOneCreditCard) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  // Address section.
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);

  // Credit card section.
  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/false,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  // Test that addresses were not saved.
  EXPECT_EQ(0U, personal_data_manager_->GetProfiles().size());

  // Test that the credit card has been saved.
  CreditCard expected_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected_card, "Biggie Smalls", "4111111111111111",
                          "01", "2999", "");
  const std::vector<CreditCard*>& results =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(expected_card));
}

// Test that a form with both address and credit card sections imports only the
// the address if credit cards are disabled.
TEST_P(FormDataImporterTest, ImportFormData_OneAddressCreditCardDisabled) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  // Address section.
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Country:", "country", "US", "text", &field);
  form.fields.push_back(field);

  // Credit card section.
  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/false,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  ASSERT_FALSE(imported_credit_card);

  WaitForOnPersonalDataChanged();

  // Test that the address has been saved.
  AutofillProfile expected_address(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&expected_address, "George", nullptr, "Washington",
                       "theprez@gmail.com", nullptr, "21 Laussat St", nullptr,
                       "San Francisco", "California", "94102", "US", nullptr);
  const std::vector<AutofillProfile*>& results_addr =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results_addr.size());
  EXPECT_THAT(*results_addr[0], ComparesEqual(expected_address));

  // Test that the credit card was not saved.
  const std::vector<CreditCard*>& results_cards =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(0U, results_cards.size());
}

// Test that a form with both address and credit card sections imports nothing
// if both addressed and credit cards are disabled.
TEST_P(FormDataImporterTest, ImportFormData_AddressCreditCardDisabled) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  // Address section.
  test::CreateTestFormField("First name:", "first_name", "George", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Washington", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Address:", "address1", "21 Laussat St", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("City:", "city", "San Francisco", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("State:", "state", "California", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Zip:", "zip", "94102", "text", &field);
  form.fields.push_back(field);

  // Credit card section.
  AddFullCreditCardForm(&form, "Biggie Smalls", "4111-1111-1111-1111", "01",
                        "2999");

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/false,
      /*credit_card_autofill_enabled=*/false,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  ASSERT_FALSE(imported_credit_card);

  // Test that addresses were not saved.
  EXPECT_EQ(0U, personal_data_manager_->GetProfiles().size());

  // Test that the credit card was not saved.
  const std::vector<CreditCard*>& results_cards =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(0U, results_cards.size());
}

TEST_P(FormDataImporterTest, DuplicateMaskedServerCard) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::MASKED_SERVER_CARD, "a123"));
  test::SetCreditCardInfo(&server_cards.back(), "John Dillinger",
                          "1881" /* Visa */, "01", "2999", "");
  server_cards.back().SetNetworkForMaskedCard(kVisaCard);

  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "378282246310005" /* American Express */, "04",
                          "2999", "");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(2U, personal_data_manager_->GetCreditCards().size());

  // A valid credit card form. A user re-enters one of their masked cards.
  // We should not offer to save locally.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "John Dillinger",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4012888888881881",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "01", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2999", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
}

// Tests that a credit card form that is hidden after receiving input still
// imports the card.
TEST_P(FormDataImporterTest, ImportFormData_HiddenCreditCardFormAfterEntered) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;

  test::CreateTestFormField("Name on card:", "name_on_card", "Biggie Smalls",
                            "text", &field);
  field.is_focusable = false;
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4111111111111111",
                            "text", &field);
  field.is_focusable = false;
  form.fields.push_back(field);
  test::CreateTestFormField("Email:", "email", "theprez@gmail.com", "text",
                            &field);
  field.is_focusable = false;
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "01", "text", &field);
  field.is_focusable = false;
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2999", "text", &field);
  field.is_focusable = false;
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  // Still returns true because the credit card import was successful.
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  ASSERT_TRUE(imported_credit_card);
  personal_data_manager_->OnAcceptedLocalCreditCardSave(*imported_credit_card);

  WaitForOnPersonalDataChanged();

  // Test that the credit card has been saved.
  CreditCard expected_card(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetCreditCardInfo(&expected_card, "Biggie Smalls", "4111111111111111",
                          "01", "2999", "");
  const std::vector<CreditCard*>& results =
      personal_data_manager_->GetCreditCards();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(expected_card));
}

// Ensures that no UPI ID value is returned when there's a credit card and no
// UPI ID.
TEST_P(FormDataImporterTest,
       ImportFormData_DontSetUpiIdWhenOnlyCreditCardExists) {
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultCreditCardFormStructure();
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      *form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/true, &imported_credit_card,
      &imported_upi_id));
  ASSERT_FALSE(imported_upi_id.has_value());
}

TEST_P(FormDataImporterTest,
       DuplicateFullServerCardWhileContainingLocalCardCopies) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::MASKED_SERVER_CARD, "a123"));
  test::SetCreditCardInfo(&server_cards.back(), "John Dillinger",
                          "1881" /* Visa */, "01", "2999", "1");
  server_cards.back().SetNetworkForMaskedCard(kVisaCard);

  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "378282246310005" /* American Express */, "04",
                          "2999", "1");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Add two local cards to the credit cards to ensure that in the case where we
  // have separate copies of a server card and a local card, we still only set
  // |imported_credit_card| to the server card details as we want the server
  // to be the source of truth. Adding two cards also helps us ensure that we
  // will update both.
  for (int i = 0; i < 2; i++) {
    CreditCard local_card = test::GetCreditCard();
    test::SetCreditCardInfo(&local_card, "Clyde Barrow",
                            "378282246310005" /* American Express */, "05",
                            "2999", "1");
    local_card.set_record_type(CreditCard::RecordType::LOCAL_CARD);
    personal_data_manager_->AddCreditCard(local_card);
  }

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(4U, personal_data_manager_->GetCreditCards().size());

  // A user re-types (or fills with) an unmasked card. Don't offer to save
  // here, either. Since it's unmasked, we know for certain that it's the same
  // card.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "378282246310005",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "04", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2999", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_TRUE(imported_credit_card);
  // Ensure that we imported the server version of the card, not the local
  // version.
  EXPECT_TRUE(imported_credit_card->record_type() ==
              CreditCard::FULL_SERVER_CARD);

  // Check that both of the local cards we have added were updated.
  int matched_local_cards = 0;
  for (const CreditCard* card : personal_data_manager_->GetCreditCards()) {
    if (card->record_type() == CreditCard::RecordType::LOCAL_CARD) {
      matched_local_cards++;
      EXPECT_EQ(card->expiration_month(), 4);
    }
  }
  EXPECT_EQ(matched_local_cards, 2);
}

TEST_P(FormDataImporterTest,
       Metrics_SubmittedServerCardExpirationStatus_FullServerCardMatch) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "4444333322221111" /* Visa */, "04", "2111", "1");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form.  Ensure that
  // an expiration date match is recorded.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "04", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2111", "text", &field);
  form.fields.push_back(field);

  base::HistogramTester histogram_tester;
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample(
      "Autofill.SubmittedServerCardExpirationStatus",
      AutofillMetrics::FULL_SERVER_CARD_EXPIRATION_DATE_MATCHED, 1);
}

// Ensure that we don't offer to save if we already have same card stored as a
// server card and user submitted an invalid expiration date month.
TEST_P(FormDataImporterTest,
       Metrics_SubmittedServerCardExpirationStatus_EmptyExpirationMonth) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "4444333322221111" /* Visa */, "04", "2111", "1");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form with an empty
  // expiration date.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2111", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_FALSE(imported_credit_card);
}

// Ensure that we don't offer to save if we already have same card stored as a
// server card and user submitted an invalid expiration date year.
TEST_P(FormDataImporterTest,
       Metrics_SubmittedServerCardExpirationStatus_EmptyExpirationYear) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "4444333322221111" /* Visa */, "04", "2111", "1");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form with an empty
  // expiration date.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "08", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_FALSE(imported_credit_card);
}

// Ensure that we still offer to save if we have different cards stored as a
// server card and user submitted an invalid expiration date year.
TEST_P(
    FormDataImporterTest,
    Metrics_SubmittedDifferentServerCardExpirationStatus_EmptyExpirationYear) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "4111111111111111" /* Visa */, "04", "2111", "1");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form with an empty
  // expiration date.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "08", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "", "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_TRUE(imported_credit_card);
}

TEST_P(FormDataImporterTest,
       Metrics_SubmittedServerCardExpirationStatus_FullServerCardMismatch) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::FULL_SERVER_CARD, "c789"));
  test::SetCreditCardInfo(&server_cards.back(), "Clyde Barrow",
                          "4444333322221111" /* Visa */, "04", "2111", "1");

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form but changes
  // the expiration date of the card.  Ensure that an expiration date mismatch
  // is recorded.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "04", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2345", "text", &field);
  form.fields.push_back(field);

  base::HistogramTester histogram_tester;
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample(
      "Autofill.SubmittedServerCardExpirationStatus",
      AutofillMetrics::FULL_SERVER_CARD_EXPIRATION_DATE_DID_NOT_MATCH, 1);
}

TEST_P(FormDataImporterTest,
       Metrics_SubmittedServerCardExpirationStatus_MaskedServerCardMatch) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::MASKED_SERVER_CARD, "a123"));
  test::SetCreditCardInfo(&server_cards.back(), "John Dillinger",
                          "1111" /* Visa */, "01", "2111", "");
  server_cards.back().SetNetworkForMaskedCard(kVisaCard);

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form.  Ensure that
  // an expiration date match is recorded.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "01", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2111", "text", &field);
  form.fields.push_back(field);

  base::HistogramTester histogram_tester;
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure,
      /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample(
      "Autofill.SubmittedServerCardExpirationStatus",
      AutofillMetrics::MASKED_SERVER_CARD_EXPIRATION_DATE_MATCHED, 1);
}

TEST_P(FormDataImporterTest,
       Metrics_SubmittedServerCardExpirationStatus_MaskedServerCardMismatch) {
  std::vector<CreditCard> server_cards;
  server_cards.push_back(CreditCard(CreditCard::MASKED_SERVER_CARD, "a123"));
  test::SetCreditCardInfo(&server_cards.back(), "John Dillinger",
                          "1111" /* Visa */, "01", "2111", "");
  server_cards.back().SetNetworkForMaskedCard(kVisaCard);

  test::SetServerCreditCards(autofill_table_, server_cards);

  // Make sure everything is set up correctly.
  personal_data_manager_->Refresh();
  WaitForOnPersonalDataChanged();
  EXPECT_EQ(1U, personal_data_manager_->GetCreditCards().size());

  // A user fills/enters the card's information on a checkout form but changes
  // the expiration date of the card.  Ensure that an expiration date mismatch
  // is recorded.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("Name on card:", "name_on_card", "Clyde Barrow",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Card Number:", "card_number", "4444333322221111",
                            "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Month:", "exp_month", "04", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Exp Year:", "exp_year", "2345", "text", &field);
  form.fields.push_back(field);

  base::HistogramTester histogram_tester;
  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  std::unique_ptr<CreditCard> imported_credit_card;
  absl::optional<std::string> imported_upi_id;
  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));
  EXPECT_TRUE(imported_credit_card);
  histogram_tester.ExpectUniqueSample(
      "Autofill.SubmittedServerCardExpirationStatus",
      AutofillMetrics::MASKED_SERVER_CARD_EXPIRATION_DATE_DID_NOT_MATCH, 1);
}

TEST_P(FormDataImporterTest, ImportUpiId) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("UPI ID:", "upi_id", "user@indianbank", "text",
                            &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);

  std::unique_ptr<CreditCard> imported_credit_card;  // Discarded.
  absl::optional<std::string> imported_upi_id;

  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/false,
      /*credit_card_autofill_enabled=*/true,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));

  ASSERT_TRUE(imported_upi_id.has_value());
  EXPECT_EQ(imported_upi_id.value(), "user@indianbank");
}

TEST_P(FormDataImporterTest, ImportUpiIdDisabled) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("UPI ID:", "upi_id", "user@indianbank", "text",
                            &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);

  std::unique_ptr<CreditCard> imported_credit_card;  // Discarded.
  absl::optional<std::string> imported_upi_id;

  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/false,
      /*credit_card_autofill_enabled=*/false,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));

  EXPECT_FALSE(imported_upi_id.has_value());
}

TEST_P(FormDataImporterTest, ImportUpiIdIgnoreNonUpiId) {
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("UPI ID:", "upi_id", "user@gmail.com", "text",
                            &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);

  std::unique_ptr<CreditCard> imported_credit_card;  // Discarded.
  absl::optional<std::string> imported_upi_id;

  EXPECT_FALSE(ImportFormDataAndProcessAddressCandidates(
      form_structure, /*profile_autofill_enabled=*/false,
      /*credit_card_autofill_enabled=*/false,
      /*should_return_local_card=*/false, &imported_credit_card,
      &imported_upi_id));

  EXPECT_FALSE(imported_upi_id.has_value());
}

TEST_P(FormDataImporterTest, SilentlyUpdateExistingProfileByIncompleteProfile) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kAutofillSilentProfileUpdateForInsufficientImport);

  AutofillProfile profile(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&profile, "Marion", "Mitchell", "Morrison",
                       "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5",
                       "Hollywood", "CA", "91601", "US", "12345678910");

  // Set the verification status for the first and middle name to parsed.
  profile.SetRawInfoWithVerificationStatus(
      NAME_FIRST, u"Marion", structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_MIDDLE, u"Mitchell",
      structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_LAST, u"Morrison", structured_address::VerificationStatus::kParsed);

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  personal_data_manager_->AddProfile(profile);
  run_loop.Run();

  // Simulate a form submission with conflicting info.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "Marion", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Middle name:", "middle_name", "", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Mitchell Morrison",
                            "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/false, form_structure);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(1, profile.Compare(*results[0]));
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FULL), u"Marion Mitchell Morrison");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FIRST), u"Marion");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_MIDDLE), u"");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST), u"Mitchell Morrison");
}

TEST_P(
    FormDataImporterTest,
    SilentlyUpdateExistingProfileByIncompleteProfile_DespiteDisallowedPrompts) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitWithFeatures(
      {features::kAutofillSilentProfileUpdateForInsufficientImport,
       features::kAutofillAddressProfileSavePrompt},
      {});

  AutofillProfile profile(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&profile, "Marion", "Mitchell", "Morrison",
                       "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5",
                       "Hollywood", "CA", "91601", "US", "12345678910");

  // Set the verification status for the first and middle name to parsed.
  profile.SetRawInfoWithVerificationStatus(
      NAME_FIRST, u"Marion", structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_MIDDLE, u"Mitchell",
      structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_LAST, u"Morrison", structured_address::VerificationStatus::kParsed);

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  personal_data_manager_->AddProfile(profile);
  run_loop.Run();

  // Simulate a form submission with conflicting info.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "Marion", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Middle name:", "middle_name", "", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Mitchell Morrison",
                            "text", &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/false, form_structure,
                        /*skip_waiting_on_pdm=*/false,
                        /*allow_save_prompts=*/false);

  // Expect that no new profile is saved and the existing profile is updated.
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_EQ(1, profile.Compare(*results[0]));
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FULL), u"Marion Mitchell Morrison");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FIRST), u"Marion");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_MIDDLE), u"");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST), u"Mitchell Morrison");
}

TEST_P(FormDataImporterTest, UnusableIncompleteProfile) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kAutofillSilentProfileUpdateForInsufficientImport);

  AutofillProfile profile(base::GenerateGUID(), test::kEmptyOrigin);
  test::SetProfileInfo(&profile, "Marion", "Mitchell", "Morrison",
                       "johnwayne@me.xyz", "Fox", "123 Zoo St.", "unit 5",
                       "Hollywood", "CA", "91601", "US", "12345678910");

  // Set the verification status for the first and middle name to parsed.
  profile.SetRawInfoWithVerificationStatus(
      NAME_FIRST, u"Marion", structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_MIDDLE, u"Mitchell",
      structured_address::VerificationStatus::kParsed);
  profile.SetRawInfoWithVerificationStatus(
      NAME_LAST, u"Morrison", structured_address::VerificationStatus::kParsed);

  base::RunLoop run_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataFinishedProfileTasks())
      .WillOnce(QuitMessageLoop(&run_loop));
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged()).Times(1);
  personal_data_manager_->AddProfile(profile);
  run_loop.Run();

  // Simulate a form submission with conflicting info.
  FormData form;
  form.url = GURL("https://wwww.foo.com");

  FormFieldData field;
  test::CreateTestFormField("First name:", "first_name", "Marion", "text",
                            &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Middle name:", "middle_name", "", "text", &field);
  form.fields.push_back(field);
  test::CreateTestFormField("Last name:", "last_name", "Mitch Morrison", "text",
                            &field);
  form.fields.push_back(field);

  FormStructure form_structure(form);
  form_structure.DetermineHeuristicTypes(nullptr, nullptr);
  ImportAddressProfiles(/*extraction_successful=*/false, form_structure,
                        /*skip_waiting_on_pdm=*/true);

  // Expect that no new profile is saved.
  const std::vector<AutofillProfile*>& results =
      personal_data_manager_->GetProfiles();
  ASSERT_EQ(1U, results.size());
  EXPECT_THAT(*results[0], ComparesEqual(profile));
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FULL), u"Marion Mitchell Morrison");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_FIRST), u"Marion");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_MIDDLE), u"Mitchell");
  EXPECT_EQ(results[0]->GetRawInfo(NAME_LAST), u"Morrison");
}

// Tests that metrics are correctly recorded when removing setting-inaccessible
// fields.
// Note that this function doesn't test the removal functionality itself. This
// is done in the AutofillProfile unit tests.
TEST_P(FormDataImporterTest, RemoveInaccessibleProfileValuesMetrics) {
  // Minimal importable profile, but with a state, which is setting-inaccessible
  // for Germany.
  TypeValuePairs type_value_pairs = {{ADDRESS_HOME_COUNTRY, "DE"},
                                     {ADDRESS_HOME_LINE1, kDefaultAddressLine1},
                                     {ADDRESS_HOME_CITY, kDefaultCity},
                                     {ADDRESS_HOME_ZIP, kDefaultZip},
                                     {ADDRESS_HOME_STATE, kDefaultState}};

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  type_value_pairs.pop_back();  // Remove state manually for verification.
  base::HistogramTester histogram_tester;
  ImportAddressProfilesAndVerifyExpectation(
      *form_structure, {ConstructProfileFromTypeValuePairs(type_value_pairs)});

  // State was removed. Expect the metrics to behave accordingly.
  const std::string metric =
      "Autofill.ProfileImport.InaccessibleFieldsRemoved.";
  histogram_tester.ExpectUniqueSample(metric + "Total", true, 1);
  histogram_tester.ExpectUniqueSample(
      metric + "ByFieldType",
      AutofillMetrics::SettingsVisibleFieldTypeForMetrics::kState, 1);
}

// Tests a 2-page multi-step import.
TEST_P(FormDataImporterTest, MultiStepImport) {
  base::test::ScopedFeatureList multistep_import_feature;
  multistep_import_feature.InitAndEnableFeature(
      features::kAutofillEnableMultiStepImports);

  std::unique_ptr<FormStructure> form_structure =
      ConstructSplitDefaultProfileFormStructure(/*part=*/1);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});

  form_structure = ConstructSplitDefaultProfileFormStructure(/*part=*/2);
  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

// Tests that imported profiles remain multi-step candidates if
// |kAutofillEnableMultiStepImportComplements| is true, which enables
// complementing the profile with additional information on further pages.
TEST_P(FormDataImporterTest, MultiStepImportComplement) {
  base::test::ScopedFeatureList multistep_import_with_complement_feature;
  multistep_import_with_complement_feature.InitAndEnableFeatureWithParameters(
      features::kAutofillEnableMultiStepImports,
      {{features::kAutofillEnableMultiStepImportComplements.name, "true"}});

  // Import the default profile without an email address.
  TypeValuePairs type_value_pairs = GetDefaultProfileTypeValuePairs();
  EXPECT_EQ(type_value_pairs[2].first, EMAIL_ADDRESS);
  type_value_pairs.erase(type_value_pairs.begin() + 2);

  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromTypeValuePairs(type_value_pairs);
  ImportAddressProfilesAndVerifyExpectation(
      *form_structure, {ConstructProfileFromTypeValuePairs(type_value_pairs)});

  // Import the email address in a separate form. Without multi-step imports,
  // this information cannot be associated to a profile. The resulting profile
  // is the default one.
  // The autocomplete attribute is set manually, because for small forms (number
  // of fields < kMinRequiredFieldsForHeuristics), no heuristics are used.
  FormData form =
      ConstructFormDateFromTypeValuePairs({{EMAIL_ADDRESS, kDefaultMail}});
  const char* autocomplete = "email";
  form.fields[0].autocomplete_attribute = autocomplete;
  form.fields[0].parsed_autocomplete =
      ParseAutocompleteAttribute(autocomplete, form.fields[0].max_length);
  form_structure = ConstructFormStructureFromFormData(form);
  ImportAddressProfileAndVerifyImportOfDefaultProfile(*form_structure);
}

// Tests that multi-step candidate profiles from different origins are not
// merged.
TEST_P(FormDataImporterTest, MultiStepImportDifferentOrigin) {
  base::test::ScopedFeatureList multistep_import_feature;
  multistep_import_feature.InitAndEnableFeature(
      features::kAutofillEnableMultiStepImports);

  FormData form = ConstructSplitDefaultFormData(/*part=*/1);
  form.url = GURL("https://wwww.foo.com");
  std::unique_ptr<FormStructure> form_structure =
      ConstructFormStructureFromFormData(form);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});

  form = ConstructSplitDefaultFormData(/*part=*/2);
  form.url = GURL("https://wwww.bar.com");
  form_structure = ConstructFormStructureFromFormData(form);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});
}

// Tests that multi-step candidates profiles are invalidated after some TTL.
TEST_P(FormDataImporterTest, MultiStepImportTTL) {
  base::test::ScopedFeatureList multistep_import_feature_set_ttl;
  multistep_import_feature_set_ttl.InitAndEnableFeatureWithParameters(
      features::kAutofillEnableMultiStepImports,
      {{features::kAutofillMultiStepImportCandidateTTL.name, "30m"}});
  TestAutofillClock test_clock;

  std::unique_ptr<FormStructure> form_structure =
      ConstructSplitDefaultProfileFormStructure(/*part=*/1);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});

  test_clock.Advance(base::Minutes(31));

  form_structure = ConstructSplitDefaultProfileFormStructure(/*part=*/2);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});
}

// Tests that multi-step candidates profiles are cleared if the browsing history
// is deleted.
TEST_P(FormDataImporterTest, MultiStepImportDeleteOnBrowsingHistoryCleared) {
  base::test::ScopedFeatureList multistep_import_feature;
  multistep_import_feature.InitAndEnableFeature(
      features::kAutofillEnableMultiStepImports);

  std::unique_ptr<FormStructure> form_structure =
      ConstructSplitDefaultProfileFormStructure(/*part=*/1);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});

  personal_data_manager_->OnURLsDeleted(
      /*history_service=*/nullptr,
      history::DeletionInfo::ForUrls(
          {history::URLRow(form_structure->source_url())},
          /*favicon_urls=*/{}));

  form_structure = ConstructSplitDefaultProfileFormStructure(/*part=*/2);
  ImportAddressProfilesAndVerifyExpectation(*form_structure, {});
}

// Tests that the FormAssociator is correctly integrated in FormDataImporter and
// that multiple address form in the same form are associated with each other.
// The functionality itself is tested in form_data_importer_utils_unittest.cc.
TEST_P(FormDataImporterTest, FormAssociator) {
  base::test::ScopedFeatureList form_association_feature;
  form_association_feature.InitAndEnableFeature(
      features::kAutofillAssociateForms);

  std::unique_ptr<FormStructure> form_structure =
      ConstructShippingAndBillingFormStructure();
  FormSignature form_signature = form_structure->form_signature();
  // Don't use `ImportAddressProfileAndVerifyImportOfDefaultProfile()`, as this
  // function assumes we know it's an address form already. Form associations
  // are tracked in `ImportFormData()` instead.
  EXPECT_TRUE(ImportFormDataAndProcessAddressCandidates(
      *form_structure, /*profile_autofill_enabled=*/true,
      /*credit_card_autofill_enabled=*/false,
      /*should_return_local_card=*/false, nullptr, nullptr));

  absl::optional<FormStructure::FormAssociations> associations =
      form_data_importer_->GetFormAssociations(form_signature);
  // Expect the same form signature for the two most recent address form, as
  // `form_structure` consists of two sections.
  EXPECT_TRUE(associations);
  EXPECT_EQ(associations->last_address_form_submitted, form_signature);
  EXPECT_EQ(associations->second_last_address_form_submitted, form_signature);
  EXPECT_FALSE(associations->last_credit_card_form_submitted);
}

// Runs the suite with the feature `kAutofillEnableSupportForApartmentNumbers`
// and `kAutofillConsiderVariationCountryCodeForPhoneNumbers` enabled and
// disabled.
// TODO(crbug.com/1295721): Remove
// `kAutofillConsiderVariationCountryCodeForPhoneNumbers` when launched.
INSTANTIATE_TEST_SUITE_P(,
                         FormDataImporterTest,
                         testing::Combine(testing::Bool(), testing::Bool()));

class FormDataImporterNonParameterizedTest : public FormDataImporterTestBase,
                                             public testing::Test {
 private:
  void SetUp() override { SetUpHelper(); }
  void TearDown() override { TearDownHelper(); }
};

TEST_F(FormDataImporterNonParameterizedTest,
       ProcessCreditCardImportCandidate_EmptyCreditCard) {
  std::unique_ptr<CreditCard> imported_credit_card;
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultCreditCardFormStructure();

  // `form_data_importer_`'s `imported_credit_card_record_type_` is set to
  // LOCAL_CARD because we need to make sure we do not return early in the
  // NEW_CARD case, and LOCAL_CARD with upstream enabled but empty
  // |imported_credit_card| is the most likely scenario for a crash.
  form_data_importer_->set_imported_credit_card_record_type_for_testing(
      FormDataImporter::ImportedCreditCardRecordType::LOCAL_CARD);

  // We need a sync service so that
  // LocalCardMigrationManager::ShouldOfferLocalCardMigration() does not crash.
  syncer::TestSyncService sync_service;
  personal_data_manager_->OnSyncServiceInitialized(&sync_service);

  EXPECT_FALSE(form_data_importer_->ProcessCreditCardImportCandidate(
      *form_structure, std::move(imported_credit_card),
      /*detected_upi_id=*/"",
      /*credit_card_autofill_enabled=*/true,
      /*is_credit_card_upstream_enabled=*/true));
  personal_data_manager_->OnSyncServiceInitialized(nullptr);
}

#if !BUILDFLAG(IS_IOS)
TEST_F(FormDataImporterNonParameterizedTest,
       ProcessCreditCardImportCandidate_VirtualCardEligible) {
  CreditCard imported_credit_card = test::GetMaskedServerCard();
  imported_credit_card.SetNetworkForMaskedCard(kAmericanExpressCard);
  imported_credit_card.set_instrument_id(1111);
  imported_credit_card.set_virtual_card_enrollment_state(
      CreditCard::VirtualCardEnrollmentState::UNENROLLED_AND_ELIGIBLE);
  std::unique_ptr<FormStructure> form_structure =
      ConstructDefaultCreditCardFormStructure();

  form_data_importer_->set_imported_credit_card_record_type_for_testing(
      FormDataImporter::ImportedCreditCardRecordType::SERVER_CARD);
  form_data_importer_->SetFetchedCardInstrumentId(2222);

  // We need a sync service so that
  // LocalCardMigrationManager::ShouldOfferLocalCardMigration() does not
  // crash.
  syncer::TestSyncService sync_service;
  personal_data_manager_->OnSyncServiceInitialized(&sync_service);

  EXPECT_CALL(*virtual_card_enrollment_manager_,
              InitVirtualCardEnroll(_, VirtualCardEnrollmentSource::kDownstream,
                                    _, _, _, _))
      .Times(0);
  EXPECT_FALSE(form_data_importer_->ProcessCreditCardImportCandidate(
      *form_structure, std::make_unique<CreditCard>(imported_credit_card),
      /*detected_upi_id=*/"",
      /*credit_card_autofill_enabled=*/true,
      /*is_credit_card_upstream_enabled=*/true));

  form_data_importer_->SetFetchedCardInstrumentId(1111);
  EXPECT_CALL(*virtual_card_enrollment_manager_,
              InitVirtualCardEnroll(_, VirtualCardEnrollmentSource::kDownstream,
                                    _, _, _, _))
      .Times(1);
  EXPECT_TRUE(form_data_importer_->ProcessCreditCardImportCandidate(
      *form_structure, std::make_unique<CreditCard>(imported_credit_card),
      /*detected_upi_id=*/"",
      /*credit_card_autofill_enabled=*/true,
      /*is_credit_card_upstream_enabled=*/true));

  personal_data_manager_->OnSyncServiceInitialized(nullptr);
}
#endif

TEST_F(FormDataImporterNonParameterizedTest,
       ShouldOfferUploadCardOrLocalCardSave) {
  // Should not offer save for null cards.
  std::unique_ptr<CreditCard> imported_credit_card;
  EXPECT_FALSE(form_data_importer_->ShouldOfferUploadCardOrLocalCardSave(
      imported_credit_card.get(),
      /*is_credit_card_upload_enabled=*/false));

  imported_credit_card = std::make_unique<CreditCard>(test::GetCreditCard());

  // Should not offer save for local cards if upstream is not enabled.
  form_data_importer_->set_imported_credit_card_record_type_for_testing(
      FormDataImporter::ImportedCreditCardRecordType::LOCAL_CARD);
  EXPECT_FALSE(form_data_importer_->ShouldOfferUploadCardOrLocalCardSave(
      imported_credit_card.get(), /*is_credit_card_upload_enabled=*/false));

  // Should offer save for local cards if upstream is enabled.
  EXPECT_TRUE(form_data_importer_->ShouldOfferUploadCardOrLocalCardSave(
      imported_credit_card.get(), /*is_credit_card_upload_enabled=*/true));

  // Should not offer save for server cards.
  form_data_importer_->set_imported_credit_card_record_type_for_testing(
      FormDataImporter::ImportedCreditCardRecordType::SERVER_CARD);
  EXPECT_FALSE(form_data_importer_->ShouldOfferUploadCardOrLocalCardSave(
      imported_credit_card.get(), /*is_credit_card_upload_enabled=*/true));

  // Should always offer save for new cards; upload save if it is enabled, local
  // save otherwise.
  form_data_importer_->set_imported_credit_card_record_type_for_testing(
      FormDataImporter::ImportedCreditCardRecordType::NEW_CARD);
  EXPECT_TRUE(form_data_importer_->ShouldOfferUploadCardOrLocalCardSave(
      imported_credit_card.get(), /*is_credit_card_upload_enabled=*/true));
  EXPECT_TRUE(form_data_importer_->ShouldOfferUploadCardOrLocalCardSave(
      imported_credit_card.get(), /*is_credit_card_upload_enabled=*/false));
}

}  // namespace autofill
