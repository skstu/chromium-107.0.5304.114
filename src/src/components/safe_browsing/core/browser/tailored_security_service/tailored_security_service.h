// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAFE_BROWSING_CORE_BROWSER_TAILORED_SECURITY_SERVICE_TAILORED_SECURITY_SERVICE_H_
#define COMPONENTS_SAFE_BROWSING_CORE_BROWSER_TAILORED_SECURITY_SERVICE_TAILORED_SECURITY_SERVICE_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace base {
class Value;
}

namespace signin {
class IdentityManager;
}

namespace network {
class SharedURLLoaderFactory;
}

namespace safe_browsing {

class TailoredSecurityServiceObserver;

// Provides an API for querying Google servers for a user's tailored security
// account Opt-In.
class TailoredSecurityService : public KeyedService {
 public:
  // Handles all the work of making an API request. This class encapsulates
  // the entire state of the request. When an instance is destroyed, all
  // aspects of the request are cancelled.
  class Request {
   public:
    virtual ~Request();

    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;

    // Returns true if the request is "pending" (i.e., it has been started, but
    // is not yet completed).
    virtual bool IsPending() const = 0;

    // Returns the response code received from the server, which will only be
    // valid if the request succeeded.
    virtual int GetResponseCode() const = 0;

    // Returns the contents of the response body received from the server.
    virtual const std::string& GetResponseBody() const = 0;

    virtual void SetPostData(const std::string& post_data) = 0;

    // Tells the request to begin.
    virtual void Start() = 0;

    virtual void Shutdown() = 0;

   protected:
    Request();
  };

  using QueryTailoredSecurityBitCallback =
      base::OnceCallback<void(bool is_enabled, base::Time previous_update)>;

  using CompletionCallback = base::OnceCallback<void(Request*, bool success)>;

  TailoredSecurityService(signin::IdentityManager* identity_manager,
                          PrefService* prefs);
  ~TailoredSecurityService() override;

  void AddObserver(TailoredSecurityServiceObserver* observer);
  void RemoveObserver(TailoredSecurityServiceObserver* observer);

  // Called to increment/decrement |active_query_request_|. When
  // |active_query_request_| goes from zero to nonzero, we begin querying the
  // tailored security setting. When it goes from nonzero to zero, we stop
  // querying the tailored security setting. Virtual for tests.
  virtual void AddQueryRequest();
  virtual void RemoveQueryRequest();

  // Queries whether TailoredSecurity is enabled on the server.
  void QueryTailoredSecurityBit();

  // Starts the request to send to the backend to retrieve the bit.
  void StartRequest(QueryTailoredSecurityBitCallback callback);

  // Sets the state of tailored security bit to |is_enabled| for testing.
  void SetTailoredSecurityBitForTesting(
      bool is_enabled,
      QueryTailoredSecurityBitCallback callback,
      const net::NetworkTrafficAnnotationTag& traffic_annotation);

  // KeyedService implementation:
  void Shutdown() override;

 protected:
  // This function is pulled out for testing purposes. Caller takes ownership of
  // the new Request.
  virtual std::unique_ptr<Request> CreateRequest(
      const GURL& url,
      CompletionCallback callback,
      const net::NetworkTrafficAnnotationTag& traffic_annotation);

  // Used for tests.
  size_t GetNumberOfPendingTailoredSecurityServiceRequests();

  // Extracts a JSON-encoded HTTP response into a dictionary.
  static base::Value ReadResponse(Request* request);

  // Called by `request` when a tailored security service query has completed.
  // Unpacks the response and calls `callback`, which is the original callback
  // that was passed to QueryTailoredSecurityBit().
  void QueryTailoredSecurityBitCompletionCallback(
      QueryTailoredSecurityBitCallback callback,
      Request* request,
      bool success);

  // Called with whether the tailored security setting `is_enabled` and the
  // timestamp of the most recent update (excluding the current update in
  // progress).
  void OnTailoredSecurityBitRetrieved(bool is_enabled,
                                      base::Time previous_update);

  // After `kAccountTailoredSecurityUpdateTimestamp` is updated, we check the
  // true value of the account tailored security preference and run this
  // callback.
  virtual void MaybeNotifySyncUser(bool is_enabled, base::Time previous_update);

  // Needs to be overridden by subclass to show sync notification. Sends a
  // trigger to tell system to show sync notification which is a visual message
  // prompt which informs user of their sync status between Account-level
  // Enhanced Safe Browsing and Chrome-level Enhanced Safe Browsing.
  virtual void ShowSyncNotification(bool is_enabled) = 0;

  PrefService* prefs() { return prefs_; }

  raw_ptr<signin::IdentityManager> identity_manager() {
    return identity_manager_;
  }

  virtual scoped_refptr<network::SharedURLLoaderFactory>
  GetURLLoaderFactory() = 0;

 private:
  // Callback when the `kAccountTailoredSecurityUpdateTimestamp` is updated
  void TailoredSecurityTimestampUpdateCallback();

  // Stores pointer to IdentityManager instance. It must outlive the
  // TailoredSecurityService and can be null during tests.
  raw_ptr<signin::IdentityManager> identity_manager_;

  // Pending TailoredSecurity queries to be canceled if not complete by
  // profile shutdown.
  std::map<Request*, std::unique_ptr<Request>>
      pending_tailored_security_requests_;

  // Observers.
  base::ObserverList<TailoredSecurityServiceObserver, true>::Unchecked
      observer_list_;

  // The number of active query requests. When this goes from non-zero to zero,
  // we stop `timer_`. When it goes from zero to non-zero, we start it.
  size_t active_query_request_ = 0;

  // Timer to periodically check tailored security bit.
  base::OneShotTimer timer_;

  bool is_tailored_security_enabled_ = false;
  base::Time last_updated_;

  bool is_shut_down_ = false;

  // The preferences for the given profile.
  raw_ptr<PrefService> prefs_;

  // This is used to observe when sync users update their Tailored Security
  // setting.
  PrefChangeRegistrar pref_registrar_;

  // Callback run when we should notify a sync user about a state change.
  base::RepeatingCallback<void(bool)> notify_sync_user_callback_;

  base::WeakPtrFactory<TailoredSecurityService> weak_ptr_factory_{this};
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_CORE_BROWSER_TAILORED_SECURITY_SERVICE_TAILORED_SECURITY_SERVICE_H_
