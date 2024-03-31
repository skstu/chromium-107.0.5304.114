// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/ios/password_manager_ios_util.h"

#include "base/strings/sys_string_conversions.h"
#include "base/values.h"
#import "components/autofill/ios/browser/autofill_util.h"
#include "components/security_state/ios/security_state_utils.h"
#import "ios/web/public/web_state.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "url/origin.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace password_manager {

bool WebStateContentIsSecureHtml(const web::WebState* web_state) {
  if (!web_state) {
    return false;
  }

  if (!web_state->ContentIsHTML()) {
    return false;
  }

  const GURL last_committed_url = web_state->GetLastCommittedURL();

  if (!network::IsUrlPotentiallyTrustworthy(last_committed_url)) {
    return false;
  }

  // If scheme is not cryptographic, the origin must be either localhost or a
  // file.
  if (!security_state::IsSchemeCryptographic(last_committed_url)) {
    return security_state::IsOriginLocalhostOrFile(last_committed_url);
  }

  // If scheme is cryptographic, valid SSL certificate is required.
  security_state::SecurityLevel security_level =
      security_state::GetSecurityLevelForWebState(web_state);
  return security_state::IsSslCertificateValid(security_level);
}

bool JsonStringToFormData(NSString* json_string,
                          autofill::FormData* form_data,
                          GURL page_url) {
  std::unique_ptr<base::Value> formValue = autofill::ParseJson(json_string);
  if (!formValue)
    return false;

  return autofill::ExtractFormData(
      *formValue, false, std::u16string(), page_url,
      page_url.DeprecatedGetOriginAsURL(), form_data);
}

bool IsCrossOriginIframe(web::WebState* web_state, web::WebFrame* web_frame) {
  return !web_frame->IsMainFrame() &&
         !url::Origin::Create(web_state->GetLastCommittedURL())
              .IsSameOriginWith(web_frame->GetSecurityOrigin());
}

}  // namespace password_manager
