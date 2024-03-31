// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/leak_detection_delegate_helper.h"

#include "base/barrier_closure.h"
#include "base/callback.h"
#include "base/containers/flat_set.h"
#include "base/ranges/algorithm.h"
#include "components/password_manager/core/browser/leak_detection/encryption_utils.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_scripts_fetcher.h"
#include "components/password_manager/core/browser/password_store_interface.h"
#include "components/password_manager/core/browser/psl_matching_helper.h"

namespace password_manager {

LeakDetectionDelegateHelper::LeakDetectionDelegateHelper(
    scoped_refptr<PasswordStoreInterface> profile_store,
    scoped_refptr<PasswordStoreInterface> account_store,
    PasswordScriptsFetcher* scripts_fetcher,
    LeakTypeReply callback)
    : profile_store_(std::move(profile_store)),
      account_store_(std::move(account_store)),
      scripts_fetcher_(scripts_fetcher),
      callback_(std::move(callback)) {
  DCHECK(profile_store_);
  // |account_store_| and |scripts_fetcher_| may be null.
}

LeakDetectionDelegateHelper::~LeakDetectionDelegateHelper() = default;

void LeakDetectionDelegateHelper::ProcessLeakedPassword(
    GURL url,
    std::u16string username,
    std::u16string password) {
  url_ = std::move(url);
  username_ = std::move(username);
  password_ = std::move(password);

  int wait_counter = 1 + (account_store_ ? 1 : 0) + (scripts_fetcher_ ? 1 : 0);
  barrier_closure_ = base::BarrierClosure(
      wait_counter, base::BindOnce(&LeakDetectionDelegateHelper::ProcessResults,
                                   base::Unretained(this)));

  profile_store_->GetAutofillableLogins(weak_ptr_factory_.GetWeakPtr());

  if (account_store_) {
    account_store_->GetAutofillableLogins(weak_ptr_factory_.GetWeakPtr());
  }

  if (scripts_fetcher_) {
    scripts_fetcher_->FetchScriptAvailability(
        url::Origin::Create(url_),
        base::BindOnce(
            &LeakDetectionDelegateHelper::ScriptAvailabilityDetermined,
            weak_ptr_factory_.GetWeakPtr()));
  }
}

void LeakDetectionDelegateHelper::OnGetPasswordStoreResults(
    std::vector<std::unique_ptr<PasswordForm>> results) {
  // Store the results.
  base::ranges::move(results, std::back_inserter(partial_results_));

  barrier_closure_.Run();
}

void LeakDetectionDelegateHelper::ScriptAvailabilityDetermined(
    bool script_is_available) {
  script_is_available_ = script_is_available;

  barrier_closure_.Run();
}

void LeakDetectionDelegateHelper::ProcessResults() {
  std::u16string canonicalized_username = CanonicalizeUsername(username_);
  std::vector<GURL> all_urls_with_leaked_credentials;
  for (const auto& form : partial_results_) {
    if (CanonicalizeUsername(form->username_value) == canonicalized_username &&
        form->password_value == password_) {
      PasswordStoreInterface& store =
          form->IsUsingAccountStore() ? *account_store_ : *profile_store_;
      PasswordForm form_to_update = *form.get();
      form_to_update.password_issues.insert_or_assign(
          InsecureType::kLeaked,
          InsecurityMetadata(base::Time::Now(), IsMuted(false)));
      store.UpdateLogin(form_to_update);
      all_urls_with_leaked_credentials.push_back(form->url);
    }
  }

  // Returns true if the urls are identical or one is a PSL match of the other.
  auto are_urls_equivalent = [&](const GURL& url1, const GURL& url2) -> bool {
    return url1 == url2 || IsPublicSuffixDomainMatch(url1.spec(), url2.spec());
  };

  IsSaved is_saved(base::ranges::any_of(
      partial_results_, [this, are_urls_equivalent](const auto& form) {
        return are_urls_equivalent(form->url, url_) &&
               form->username_value == username_ &&
               form->password_value == password_;
      }));

  // Check if the password is reused on a different origin, or on the same
  // origin with a different username.
  IsReused is_reused(base::ranges::any_of(
      partial_results_, [this, are_urls_equivalent](const auto& form) {
        return form->password_value == password_ &&
               (!are_urls_equivalent(form->url, url_) ||
                form->username_value != username_);
      }));

  HasChangeScript has_change_script(script_is_available_);

  std::move(callback_).Run(is_saved, is_reused, has_change_script,
                           std::move(url_), std::move(username_),
                           std::move(all_urls_with_leaked_credentials));
}

}  // namespace password_manager
