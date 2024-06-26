// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SUPERVISED_USER_SUPERVISED_USER_INTERSTITIAL_H_
#define CHROME_BROWSER_SUPERVISED_USER_SUPERVISED_USER_INTERSTITIAL_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/supervised_user/supervised_user_error_page/supervised_user_error_page.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

class Profile;

// This class is used by SupervisedUserNavigationObserver to handle requests
// from supervised user error page. The error page is shown when a page is
// blocked because it is on a denylist (in "allow everything" mode), not on any
// allowlist (in "allow only specified sites" mode), or doesn't pass safe
// search.
class SupervisedUserInterstitial {
 public:
  SupervisedUserInterstitial(const SupervisedUserInterstitial&) = delete;
  SupervisedUserInterstitial& operator=(const SupervisedUserInterstitial&) =
      delete;

  ~SupervisedUserInterstitial();

  static std::unique_ptr<SupervisedUserInterstitial> Create(
      content::WebContents* web_contents,
      const GURL& url,
      supervised_user_error_page::FilteringBehaviorReason reason,
      int frame_id,
      int64_t interstitial_navigation_id);

  static std::string GetHTMLContents(
      Profile* profile,
      supervised_user_error_page::FilteringBehaviorReason reason,
      bool already_sent_request,
      bool is_main_frame);

  void GoBack();
  void RequestUrlAccessRemote(base::OnceCallback<void(bool)> callback);
  void RequestUrlAccessLocal(base::OnceCallback<void(bool)> callback);
  void ShowFeedback();

  // Getter methods.
  content::WebContents* web_contents() { return web_contents_; }
  int frame_id() const { return frame_id_; }
  int64_t interstitial_navigation_id() const {
    return interstitial_navigation_id_;
  }
  const GURL& url() const { return url_; }

 private:
  SupervisedUserInterstitial(
      content::WebContents* web_contents,
      const GURL& url,
      supervised_user_error_page::FilteringBehaviorReason reason,
      int frame_id,
      int64_t interstitial_navigation_id);

  // Tries to go back.
  void AttemptMoveAwayFromCurrentFrameURL();

  void OnInterstitialDone();

  // Owns SupervisedUserNavigationObserver which owns us.
  raw_ptr<content::WebContents> web_contents_;

  raw_ptr<Profile> profile_;

  // The last committed url for this frame.
  GURL url_;
  supervised_user_error_page::FilteringBehaviorReason reason_;

  // The uniquely identifying global id for the frame.
  int frame_id_;

  // The Navigation ID of the navigation that last triggered the interstitial.
  int64_t interstitial_navigation_id_;
};

#endif  // CHROME_BROWSER_SUPERVISED_USER_SUPERVISED_USER_INTERSTITIAL_H_
