// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/payments/payments_requests/opt_change_request.h"

#include <string>

#include "base/json/json_writer.h"

namespace autofill::payments {

namespace {
const char kOptChangeRequestPath[] =
    "payments/apis/chromepaymentsservice/updateautofilluserpreference";
}  // namespace

OptChangeRequest::OptChangeRequest(
    const PaymentsClient::OptChangeRequestDetails& request_details,
    base::OnceCallback<void(AutofillClient::PaymentsRpcResult,
                            PaymentsClient::OptChangeResponseDetails&)>
        callback,
    const bool full_sync_enabled)
    : request_details_(request_details),
      callback_(std::move(callback)),
      full_sync_enabled_(full_sync_enabled) {}

OptChangeRequest::~OptChangeRequest() = default;

std::string OptChangeRequest::GetRequestUrlPath() {
  return kOptChangeRequestPath;
}

std::string OptChangeRequest::GetRequestContentType() {
  return "application/json";
}

std::string OptChangeRequest::GetRequestContent() {
  base::Value request_dict(base::Value::Type::DICTIONARY);
  base::Value context(base::Value::Type::DICTIONARY);
  context.SetKey("language_code", base::Value(request_details_.app_locale));
  context.SetKey("billable_service",
                 base::Value(kUnmaskCardBillableServiceNumber));
  request_dict.SetKey("context", std::move(context));

  base::Value chrome_user_context(base::Value::Type::DICTIONARY);
  chrome_user_context.SetKey("full_sync_enabled",
                             base::Value(full_sync_enabled_));
  request_dict.SetKey("chrome_user_context", std::move(chrome_user_context));

  std::string reason;
  switch (request_details_.reason) {
    case PaymentsClient::OptChangeRequestDetails::ENABLE_FIDO_AUTH:
      reason = "ENABLE_FIDO_AUTH";
      break;
    case PaymentsClient::OptChangeRequestDetails::DISABLE_FIDO_AUTH:
      reason = "DISABLE_FIDO_AUTH";
      break;
    case PaymentsClient::OptChangeRequestDetails::ADD_CARD_FOR_FIDO_AUTH:
      reason = "ADD_CARD_FOR_FIDO_AUTH";
      break;
    default:
      NOTREACHED();
      break;
  }
  request_dict.SetKey("reason", base::Value(reason));

  if (request_details_.fido_authenticator_response.has_value()) {
    base::Value fido_authentication_info(base::Value::Type::DICTIONARY);

    fido_authentication_info.SetKey(
        "fido_authenticator_response",
        std::move(request_details_.fido_authenticator_response.value()));

    if (!request_details_.card_authorization_token.empty()) {
      fido_authentication_info.SetKey(
          "card_authorization_token",
          base::Value(request_details_.card_authorization_token));
    }

    request_dict.SetKey("fido_authentication_info",
                        std::move(fido_authentication_info));
  }

  std::string request_content;
  base::JSONWriter::Write(request_dict, &request_content);
  VLOG(3) << "updateautofilluserpreference request body: " << request_content;
  return request_content;
}

void OptChangeRequest::ParseResponse(const base::Value& response) {
  const auto* fido_authentication_info = response.FindKeyOfType(
      "fido_authentication_info", base::Value::Type::DICTIONARY);
  if (!fido_authentication_info)
    return;

  const auto* user_status =
      fido_authentication_info->FindStringKey("user_status");
  if (user_status && *user_status != "UNKNOWN_USER_STATUS")
    response_details_.user_is_opted_in = (*user_status == "FIDO_AUTH_ENABLED");

  const auto* fido_creation_options = fido_authentication_info->FindKeyOfType(
      "fido_creation_options", base::Value::Type::DICTIONARY);
  if (fido_creation_options)
    response_details_.fido_creation_options = fido_creation_options->Clone();

  const auto* fido_request_options = fido_authentication_info->FindKeyOfType(
      "fido_request_options", base::Value::Type::DICTIONARY);
  if (fido_request_options)
    response_details_.fido_request_options = fido_request_options->Clone();
}

bool OptChangeRequest::IsResponseComplete() {
  return response_details_.user_is_opted_in.has_value();
}

void OptChangeRequest::RespondToDelegate(
    AutofillClient::PaymentsRpcResult result) {
  std::move(callback_).Run(result, response_details_);
}

}  // namespace autofill::payments
