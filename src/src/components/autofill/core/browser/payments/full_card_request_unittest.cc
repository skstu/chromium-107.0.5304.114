// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/payments/full_card_request.h"

#include "base/command_line.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/payments/payments_client.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_autofill_driver.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/variations/scoped_variations_ids_provider.h"
#include "net/url_request/url_request_test_util.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {
namespace payments {

using testing::_;
using testing::NiceMock;

// The consumer of the full card request API.
class MockResultDelegate : public FullCardRequest::ResultDelegate,
                           public base::SupportsWeakPtr<MockResultDelegate> {
 public:
  MOCK_METHOD(void,
              OnFullCardRequestSucceeded,
              (const payments::FullCardRequest&,
               const CreditCard&,
               const std::u16string&),
              (override));
  MOCK_METHOD(void,
              OnFullCardRequestFailed,
              (payments::FullCardRequest::FailureType),
              (override));
};

// The delegate responsible for displaying the unmask prompt UI.
class MockUIDelegate : public FullCardRequest::UIDelegate,
                       public base::SupportsWeakPtr<MockUIDelegate> {
 public:
  MOCK_METHOD(void,
              ShowUnmaskPrompt,
              (const CreditCard&,
               AutofillClient::UnmaskCardReason,
               base::WeakPtr<CardUnmaskDelegate>),
              (override));
  MOCK_METHOD(void,
              OnUnmaskVerificationResult,
              (AutofillClient::PaymentsRpcResult),
              (override));
#if BUILDFLAG(IS_ANDROID)
  MOCK_METHOD(bool, ShouldOfferFidoAuth, (), (const override));
  MOCK_METHOD(bool,
              UserOptedInToFidoFromSettingsPageOnMobile,
              (),
              (const override));
#endif
};

// The personal data manager.
class MockPersonalDataManager : public TestPersonalDataManager {
 public:
  MockPersonalDataManager() {}
  ~MockPersonalDataManager() override {}
  MOCK_METHOD(bool, IsSyncFeatureEnabled, (), (const override));
  MOCK_METHOD(void,
              UpdateCreditCard,
              (const CreditCard& credit_card),
              (override));
  MOCK_METHOD(void,
              UpdateServerCreditCard,
              (const CreditCard& credit_card),
              (override));
};

// TODO(crbug.com/881835): Simplify this test setup.
// The test fixture for full card request.
class FullCardRequestTest : public testing::Test {
 public:
  FullCardRequestTest()
      : test_shared_loader_factory_(
            base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
                &test_url_loader_factory_)) {
    payments_client_ = std::make_unique<PaymentsClient>(
        test_shared_loader_factory_, autofill_client_.GetIdentityManager(),
        &personal_data_);
    request_ = std::make_unique<FullCardRequest>(
        &autofill_client_, payments_client_.get(), &personal_data_);
    personal_data_.SetAccountInfoForPayments(
        autofill_client_.GetIdentityManager()->GetPrimaryAccountInfo(
            signin::ConsentLevel::kSync));
    // Silence the warning from PaymentsClient about matching sync and Payments
    // server types.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        "sync-url", "https://google.com");
  }

  FullCardRequestTest(const FullCardRequestTest&) = delete;
  FullCardRequestTest& operator=(const FullCardRequestTest&) = delete;

  ~FullCardRequestTest() override = default;

  MockPersonalDataManager* personal_data() { return &personal_data_; }

  FullCardRequest* request() { return request_.get(); }

  CardUnmaskDelegate* card_unmask_delegate() {
    return static_cast<CardUnmaskDelegate*>(request_.get());
  }

  MockResultDelegate* result_delegate() { return &result_delegate_; }

  MockUIDelegate* ui_delegate() { return &ui_delegate_; }

  void OnDidGetRealPan(AutofillClient::PaymentsRpcResult result,
                       const std::string& real_pan,
                       bool is_virtual_card = false) {
    payments::PaymentsClient::UnmaskResponseDetails response;
    response.card_type = is_virtual_card
                             ? AutofillClient::PaymentsRpcCardType::kVirtualCard
                             : AutofillClient::PaymentsRpcCardType::kServerCard;
    request_->OnDidGetRealPan(result, response.with_real_pan(real_pan));
  }

  void OnDidGetRealPanWithDcvv(AutofillClient::PaymentsRpcResult result,
                               const std::string& real_pan,
                               const std::string& dcvv,
                               bool is_virtual_card = false) {
    payments::PaymentsClient::UnmaskResponseDetails response;
    response.card_type = is_virtual_card
                             ? AutofillClient::PaymentsRpcCardType::kVirtualCard
                             : AutofillClient::PaymentsRpcCardType::kServerCard;
    request_->OnDidGetRealPan(result,
                              response.with_real_pan(real_pan).with_dcvv(dcvv));
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  variations::ScopedVariationsIdsProvider scoped_variations_ids_provider_{
      variations::VariationsIdsProvider::Mode::kUseSignedInState};
  NiceMock<MockPersonalDataManager> personal_data_;
  MockResultDelegate result_delegate_;
  MockUIDelegate ui_delegate_;
  TestAutofillClient autofill_client_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> test_shared_loader_factory_;
  std::unique_ptr<PaymentsClient> payments_client_;
  std::unique_ptr<FullCardRequest> request_;
};

// Matches the |arg| credit card to the given |record_type| and |card_number|.
MATCHER_P2(CardMatches, record_type, card_number, "") {
  return arg.record_type() == record_type &&
         arg.GetRawInfo(CREDIT_CARD_NUMBER) == base::ASCIIToUTF16(card_number);
}

// Matches the |arg| credit card to the given |record_type|, card |number|,
// expiration |month|, and expiration |year|.
MATCHER_P4(CardMatches, record_type, number, month, year, "") {
  return arg.record_type() == record_type &&
         arg.GetRawInfo(CREDIT_CARD_NUMBER) == base::ASCIIToUTF16(number) &&
         arg.GetRawInfo(CREDIT_CARD_EXP_MONTH) == base::ASCIIToUTF16(month) &&
         arg.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR) ==
             base::ASCIIToUTF16(year);
}

// Verify getting the full PAN and the CVC for a masked server card.
TEST_F(FullCardRequestTest, GetFullCardPanAndCvcForMaskedServerCardViaCvc) {
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::FULL_SERVER_CARD, "4111"),
                  testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the full PAN and the dCVV for a masked server card when cloud
// tokenization is enabled.
TEST_F(FullCardRequestTest, GetFullCardPanAndDcvvForMaskedServerCardViaDcvv) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillAlwaysReturnCloudTokenizedCard);
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::FULL_SERVER_CARD, "4111"),
                  testing::Eq(u"321")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPanWithDcvv(AutofillClient::PaymentsRpcResult::kSuccess, "4111",
                          "321");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the full PAN for a masked server card when cloud
// tokenization is enabled but no dCVV is returned.
TEST_F(FullCardRequestTest, GetFullCardPanForMaskedServerCardWithoutDcvv) {
  scoped_feature_list_.InitAndEnableFeature(
      features::kAutofillAlwaysReturnCloudTokenizedCard);
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::FULL_SERVER_CARD, "4111"),
                  testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the full PAN for a masked server card.
TEST_F(FullCardRequestTest, GetFullCardPanAndCvcForMaskedServerCardViaFido) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestSucceeded(
          testing::Ref(*request()),
          CardMatches(CreditCard::FULL_SERVER_CARD, "4111"), testing::Eq(u"")));

  request()->GetFullCardViaFIDO(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(),
      base::Value(base::Value::Type::DICTIONARY));
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
}

// Verify getting the CVC for a local card.
TEST_F(FullCardRequestTest, GetFullCardPanAndCvcForLocalCard) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestSucceeded(testing::Ref(*request()),
                                 CardMatches(CreditCard::LOCAL_CARD, "4111"),
                                 testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  CreditCard card;
  test::SetCreditCardInfo(&card, nullptr, "4111", "12", "2050", "1");
  request()->GetFullCard(card, AutofillClient::UnmaskCardReason::kAutofill,
                         result_delegate()->AsWeakPtr(),
                         ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the CVC for an unmasked server card.
TEST_F(FullCardRequestTest, GetFullCardPanAndCvcForFullServerCard) {
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::FULL_SERVER_CARD, "4111"),
                  testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  CreditCard full_server_card(CreditCard::FULL_SERVER_CARD, "server_id");
  test::SetCreditCardInfo(&full_server_card, nullptr, "4111", "12", "2050",
                          "1");
  request()->GetFullCard(
      full_server_card, AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the CVC for an unmasked server card with expiration date in
// the past.
TEST_F(FullCardRequestTest, GetFullCardPanAndCvcForExpiredFullServerCard) {
  EXPECT_CALL(*result_delegate(), OnFullCardRequestSucceeded(
                                      testing::Ref(*request()),
                                      CardMatches(CreditCard::FULL_SERVER_CARD,
                                                  "4111", "12", "2051"),
                                      testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*personal_data(), UpdateServerCreditCard(_)).Times(0);
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  base::Time::Exploded today;
  AutofillClock::Now().LocalExplode(&today);
  CreditCard full_server_card(CreditCard::FULL_SERVER_CARD, "server_id");
  test::SetCreditCardInfo(&full_server_card, nullptr, "4111", "12",
                          base::StringPrintf("%d", today.year - 1).c_str(),
                          "1");
  request()->GetFullCard(
      full_server_card, AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  details.exp_year = u"2051";
  details.exp_month = u"12";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the CVC for a masked server card with expiration date in the past.
TEST_F(FullCardRequestTest, GetFullCardPanAndCvcForExpiredMaskedServerCard) {
  EXPECT_CALL(*result_delegate(), OnFullCardRequestSucceeded(
                                      testing::Ref(*request()),
                                      CardMatches(CreditCard::FULL_SERVER_CARD,
                                                  "4111", "12", "2051"),
                                      testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*personal_data(), UpdateServerCreditCard(_)).Times(0);
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  base::Time::Exploded today;
  AutofillClock::Now().LocalExplode(&today);
  CreditCard full_server_card(CreditCard::MASKED_SERVER_CARD, "server_id");
  test::SetCreditCardInfo(&full_server_card, nullptr, "4111", "12",
                          base::StringPrintf("%d", today.year - 1).c_str(),
                          "1");
  request()->GetFullCard(
      full_server_card, AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  details.exp_year = u"2051";
  details.exp_month = u"12";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting the full PAN, the expiration and the dCVV for a virtual card.
TEST_F(FullCardRequestTest, GetFullCardPanAndExpirationAndDcvvForVirtualCard) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestSucceeded(testing::Ref(*request()),
                                 CardMatches(CreditCard::VIRTUAL_CARD, "4111"),
                                 testing::Eq(u"321")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr(),
      GURL("https://www.example.com"));
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  payments::PaymentsClient::UnmaskResponseDetails response;
  response.real_pan = "4111";
  response.dcvv = "321";
  response.expiration_month = "12";
  response.expiration_year = test::NextYear();
  response.card_type = AutofillClient::PaymentsRpcCardType::kVirtualCard;
  request()->OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess,
                             response);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Only one request at a time should be allowed.
TEST_F(FullCardRequestTest, OneRequestAtATime) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestFailed(FullCardRequest::FailureType::GENERIC_FAILURE));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(_)).Times(0);

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id_1"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id_2"),
      AutofillClient::UnmaskCardReason::kPaymentRequest,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
}

// After the first request completes, it's OK to start the second request.
TEST_F(FullCardRequestTest, SecondRequestOkAfterFirstFinished) {
  EXPECT_CALL(*result_delegate(), OnFullCardRequestFailed(_)).Times(0);
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestSucceeded(testing::Ref(*request()),
                                 CardMatches(CreditCard::LOCAL_CARD, "4111"),
                                 testing::Eq(u"123")))
      .Times(2);
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _)).Times(2);
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess))
      .Times(2);

  CreditCard card;
  test::SetCreditCardInfo(&card, nullptr, "4111", "12", "2050", "1");
  request()->GetFullCard(card, AutofillClient::UnmaskCardReason::kAutofill,
                         result_delegate()->AsWeakPtr(),
                         ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  card_unmask_delegate()->OnUnmaskPromptClosed();

  request()->GetFullCard(card, AutofillClient::UnmaskCardReason::kAutofill,
                         result_delegate()->AsWeakPtr(),
                         ui_delegate()->AsWeakPtr());
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the user cancels the CVC prompt,
// FullCardRequest::Delegate::OnFullCardRequestFailed() should be invoked.
TEST_F(FullCardRequestTest, ClosePromptWithoutUserInput) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestFailed(FullCardRequest::FailureType::PROMPT_CLOSED));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(_)).Times(0);

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the server provides an empty PAN with PERMANENT_FAILURE error,
// FullCardRequest::Delegate::OnFullCardRequestFailed() should be invoked.
TEST_F(FullCardRequestTest, PermanentFailure) {
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestFailed(
                  FullCardRequest::FailureType::VERIFICATION_DECLINED));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(),
              OnUnmaskVerificationResult(
                  AutofillClient::PaymentsRpcResult::kPermanentFailure));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kPermanentFailure, "");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the server provides an empty PAN with VCN_RETRIEVAL_TRY_AGAIN_FAILURE
// error, FullCardRequest::Delegate::OnFullCardRequestFailed() should be
// invoked.
TEST_F(FullCardRequestTest, VcnRetrievalTryAgainFailure) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestFailed(FullCardRequest::FailureType::
                                  VIRTUAL_CARD_RETRIEVAL_TRANSIENT_FAILURE));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(
      *ui_delegate(),
      OnUnmaskVerificationResult(
          AutofillClient::PaymentsRpcResult::kVcnRetrievalTryAgainFailure));

  CreditCard card;
  card.set_record_type(CreditCard::VIRTUAL_CARD);
  request()->GetFullCard(card, AutofillClient::UnmaskCardReason::kAutofill,
                         result_delegate()->AsWeakPtr(),
                         ui_delegate()->AsWeakPtr(),
                         GURL("https://www.example.com"));
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(
      AutofillClient::PaymentsRpcResult::kVcnRetrievalTryAgainFailure, "",
      /*is_virtual_card=*/true);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the server provides an empty PAN with VCN_RETRIEVAL_PERMANENT_FAILURE
// error, FullCardRequest::Delegate::OnFullCardRequestFailed() should be
// invoked.
TEST_F(FullCardRequestTest, VcnRetrievalPermanentFailure) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestFailed(FullCardRequest::FailureType::
                                  VIRTUAL_CARD_RETRIEVAL_PERMANENT_FAILURE));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(
      *ui_delegate(),
      OnUnmaskVerificationResult(
          AutofillClient::PaymentsRpcResult::kVcnRetrievalPermanentFailure));

  CreditCard card;
  card.set_record_type(CreditCard::VIRTUAL_CARD);
  request()->GetFullCard(card, AutofillClient::UnmaskCardReason::kAutofill,
                         result_delegate()->AsWeakPtr(),
                         ui_delegate()->AsWeakPtr(),
                         GURL("https://www.example.com"));
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(
      AutofillClient::PaymentsRpcResult::kVcnRetrievalPermanentFailure, "",
      /*is_virtual_card=*/true);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the server provides an empty PAN with NETWORK_ERROR error,
// FullCardRequest::Delegate::OnFullCardRequestFailed() should be invoked.
TEST_F(FullCardRequestTest, NetworkError) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestFailed(FullCardRequest::FailureType::GENERIC_FAILURE));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(),
              OnUnmaskVerificationResult(
                  AutofillClient::PaymentsRpcResult::kNetworkError));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kNetworkError, "");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the server provides an empty PAN with TRY_AGAIN_FAILURE, the user can
// manually cancel out of the dialog.
TEST_F(FullCardRequestTest, TryAgainFailureGiveUp) {
  EXPECT_CALL(
      *result_delegate(),
      OnFullCardRequestFailed(FullCardRequest::FailureType::PROMPT_CLOSED));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(),
              OnUnmaskVerificationResult(
                  AutofillClient::PaymentsRpcResult::kTryAgainFailure));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kTryAgainFailure, "");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// If the server provides an empty PAN with TRY_AGAIN_FAILURE, the user can
// correct their mistake and resubmit.
TEST_F(FullCardRequestTest, TryAgainFailureRetry) {
  EXPECT_CALL(*result_delegate(), OnFullCardRequestFailed(_)).Times(0);
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::FULL_SERVER_CARD, "4111"),
                  testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(),
              OnUnmaskVerificationResult(
                  AutofillClient::PaymentsRpcResult::kTryAgainFailure));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"789";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kTryAgainFailure, "");
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify updating expiration date for a masked server card.
TEST_F(FullCardRequestTest, UpdateExpDateForMaskedServerCard) {
  EXPECT_CALL(*result_delegate(), OnFullCardRequestSucceeded(
                                      testing::Ref(*request()),
                                      CardMatches(CreditCard::FULL_SERVER_CARD,
                                                  "4111", "12", "2050"),
                                      testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  details.exp_month = u"12";
  details.exp_year = u"2050";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify updating expiration date for an unmasked server card.
TEST_F(FullCardRequestTest, UpdateExpDateForFullServerCard) {
  EXPECT_CALL(*result_delegate(), OnFullCardRequestSucceeded(
                                      testing::Ref(*request()),
                                      CardMatches(CreditCard::FULL_SERVER_CARD,
                                                  "4111", "12", "2050"),
                                      testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  CreditCard full_server_card(CreditCard::FULL_SERVER_CARD, "server_id");
  test::SetCreditCardInfo(&full_server_card, nullptr, "4111", "10", "2000",
                          "1");
  request()->GetFullCard(
      full_server_card, AutofillClient::UnmaskCardReason::kAutofill,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  details.exp_month = u"12";
  details.exp_year = u"2050";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify updating expiration date for a local card.
TEST_F(FullCardRequestTest, UpdateExpDateForLocalCard) {
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::LOCAL_CARD, "4111", "12", "2051"),
                  testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*personal_data(),
              UpdateCreditCard(
                  CardMatches(CreditCard::LOCAL_CARD, "4111", "12", "2051")));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  base::Time::Exploded today;
  AutofillClock::Now().LocalExplode(&today);
  CreditCard card;
  test::SetCreditCardInfo(&card, nullptr, "4111", "10",
                          base::StringPrintf("%d", today.year - 1).c_str(),
                          "1");
  request()->GetFullCard(card, AutofillClient::UnmaskCardReason::kAutofill,
                         result_delegate()->AsWeakPtr(),
                         ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  details.exp_month = u"12";
  details.exp_year = u"2051";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

// Verify getting full PAN and CVC for PaymentRequest.
TEST_F(FullCardRequestTest, UnmaskForPaymentRequest) {
  EXPECT_CALL(*result_delegate(),
              OnFullCardRequestSucceeded(
                  testing::Ref(*request()),
                  CardMatches(CreditCard::FULL_SERVER_CARD, "4111"),
                  testing::Eq(u"123")));
  EXPECT_CALL(*ui_delegate(), ShowUnmaskPrompt(_, _, _));
  EXPECT_CALL(*ui_delegate(), OnUnmaskVerificationResult(
                                  AutofillClient::PaymentsRpcResult::kSuccess));

  request()->GetFullCard(
      CreditCard(CreditCard::MASKED_SERVER_CARD, "server_id"),
      AutofillClient::UnmaskCardReason::kPaymentRequest,
      result_delegate()->AsWeakPtr(), ui_delegate()->AsWeakPtr());
  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess, "4111");
  card_unmask_delegate()->OnUnmaskPromptClosed();
}

}  // namespace payments
}  // namespace autofill
