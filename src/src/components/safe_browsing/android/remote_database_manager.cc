// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/android/remote_database_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/timer/elapsed_timer.h"
#include "components/safe_browsing/android/safe_browsing_api_handler_bridge.h"
#include "components/safe_browsing/core/browser/db/v4_get_hash_protocol_manager.h"
#include "components/safe_browsing/core/browser/db/v4_protocol_manager_util.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

using content::BrowserThread;

namespace safe_browsing {

namespace {

// Android field trial for controlling types_to_check.
const char kAndroidFieldExperiment[] = "SafeBrowsingAndroid";
const char kAndroidTypesToCheckParam[] = "types_to_check";

}  // namespace

//
// RemoteSafeBrowsingDatabaseManager::ClientRequest methods
//
class RemoteSafeBrowsingDatabaseManager::ClientRequest {
 public:
  ClientRequest(Client* client,
                RemoteSafeBrowsingDatabaseManager* db_manager,
                const GURL& url);

  static void OnRequestDoneWeak(const base::WeakPtr<ClientRequest>& req,
                                SBThreatType matched_threat_type,
                                const ThreatMetadata& metadata);
  void OnRequestDone(SBThreatType matched_threat_type,
                     const ThreatMetadata& metadata);

  // Accessors
  Client* client() const { return client_; }
  const GURL& url() const { return url_; }
  base::WeakPtr<ClientRequest> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  raw_ptr<Client> client_;
  raw_ptr<RemoteSafeBrowsingDatabaseManager> db_manager_;
  GURL url_;
  base::ElapsedTimer timer_;
  base::WeakPtrFactory<ClientRequest> weak_factory_{this};
};

RemoteSafeBrowsingDatabaseManager::ClientRequest::ClientRequest(
    Client* client,
    RemoteSafeBrowsingDatabaseManager* db_manager,
    const GURL& url)
    : client_(client), db_manager_(db_manager), url_(url) {}

// Static
void RemoteSafeBrowsingDatabaseManager::ClientRequest::OnRequestDoneWeak(
    const base::WeakPtr<ClientRequest>& req,
    SBThreatType matched_threat_type,
    const ThreatMetadata& metadata) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!req)
    return;  // Previously canceled
  req->OnRequestDone(matched_threat_type, metadata);
}

void RemoteSafeBrowsingDatabaseManager::ClientRequest::OnRequestDone(
    SBThreatType matched_threat_type,
    const ThreatMetadata& metadata) {
  DVLOG(1) << "OnRequestDone took " << timer_.Elapsed().InMilliseconds()
           << " ms for client " << client_ << " and URL " << url_;
  client_->OnCheckBrowseUrlResult(url_, matched_threat_type, metadata);
  UMA_HISTOGRAM_TIMES("SB2.RemoteCall.Elapsed", timer_.Elapsed());
  // CancelCheck() will delete *this.
  db_manager_->CancelCheck(client_);
}

//
// RemoteSafeBrowsingDatabaseManager methods
//

// TODO(nparker): Add more tests for this class
RemoteSafeBrowsingDatabaseManager::RemoteSafeBrowsingDatabaseManager()
    : SafeBrowsingDatabaseManager(content::GetUIThreadTaskRunner({}),
                                  content::GetIOThreadTaskRunner({})) {
  // Avoid memory allocations growing the underlying vector. Although this
  // usually wastes a bit of memory, it will still be less than the default
  // vector allocation strategy.
  request_destinations_to_check_.reserve(
      static_cast<int>(network::mojom::RequestDestination::kMaxValue) + 1);
  // Decide which request destinations to check. These three are the minimum.
  request_destinations_to_check_.insert(
      network::mojom::RequestDestination::kDocument);
  request_destinations_to_check_.insert(
      network::mojom::RequestDestination::kIframe);
  request_destinations_to_check_.insert(
      network::mojom::RequestDestination::kFrame);
  request_destinations_to_check_.insert(
      network::mojom::RequestDestination::kFencedframe);

  // The param is expected to be a comma-separated list of ints
  // corresponding to the enum types.  We're keeping this finch
  // control around so we can add back types if they later become dangerous.
  const std::string ints_str = variations::GetVariationParamValue(
      kAndroidFieldExperiment, kAndroidTypesToCheckParam);
  if (ints_str.empty()) {
    // By default, we check all types except a few.
    static_assert(
        network::mojom::RequestDestination::kMaxValue ==
            network::mojom::RequestDestination::kFencedframe,
        "Decide if new request destination should be skipped on mobile.");
    for (int t_int = 0;
         t_int <=
         static_cast<int>(network::mojom::RequestDestination::kMaxValue);
         t_int++) {
      network::mojom::RequestDestination t =
          static_cast<network::mojom::RequestDestination>(t_int);
      switch (t) {
        case network::mojom::RequestDestination::kStyle:
        case network::mojom::RequestDestination::kImage:
        case network::mojom::RequestDestination::kFont:
          break;
        default:
          request_destinations_to_check_.insert(t);
      }
    }
  } else {
    // Use the finch param.
    for (const std::string& val_str : base::SplitString(
             ints_str, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
      int i;
      if (base::StringToInt(val_str, &i) && i >= 0 &&
          i <=
              static_cast<int>(network::mojom::RequestDestination::kMaxValue)) {
        request_destinations_to_check_.insert(
            static_cast<network::mojom::RequestDestination>(i));
      }
    }
  }
}

RemoteSafeBrowsingDatabaseManager::~RemoteSafeBrowsingDatabaseManager() {
  DCHECK(!enabled_);
}

void RemoteSafeBrowsingDatabaseManager::CancelCheck(Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(enabled_);
  for (auto itr = current_requests_.begin(); itr != current_requests_.end();
       ++itr) {
    if ((*itr)->client() == client) {
      DVLOG(2) << "Canceling check for URL " << (*itr)->url();
      delete *itr;
      current_requests_.erase(itr);
      return;
    }
  }
}

bool RemoteSafeBrowsingDatabaseManager::CanCheckRequestDestination(
    network::mojom::RequestDestination request_destination) const {
  return request_destinations_to_check_.count(request_destination) > 0;
}

bool RemoteSafeBrowsingDatabaseManager::CanCheckUrl(const GURL& url) const {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIs(url::kFtpScheme) ||
         url.SchemeIsWSOrWSS();
}

bool RemoteSafeBrowsingDatabaseManager::ChecksAreAlwaysAsync() const {
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckBrowseUrl(
    const GURL& url,
    const SBThreatTypeSet& threat_types,
    Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(!threat_types.empty());
  DCHECK(SBThreatTypeSetIsValidForCheckBrowseUrl(threat_types));
  if (!enabled_)
    return true;

  bool can_check_url = CanCheckUrl(url);
  UMA_HISTOGRAM_BOOLEAN("SB2.RemoteCall.CanCheckUrl", can_check_url);
  if (!can_check_url)
    return true;  // Safe, continue right away.

  std::unique_ptr<ClientRequest> req(new ClientRequest(client, this, url));

  DVLOG(1) << "Checking for client " << client << " and URL " << url;
  auto callback =
      std::make_unique<SafeBrowsingApiHandlerBridge::ResponseCallback>(
          base::BindOnce(&ClientRequest::OnRequestDoneWeak, req->GetWeakPtr()));
  SafeBrowsingApiHandlerBridge::GetInstance().StartURLCheck(std::move(callback),
                                                            url, threat_types);

  current_requests_.push_back(req.release());

  // Defer the resource load.
  return false;
}

bool RemoteSafeBrowsingDatabaseManager::CheckDownloadUrl(
    const std::vector<GURL>& url_chain,
    Client* client) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckExtensionIDs(
    const std::set<std::string>& extension_ids,
    Client* client) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckResourceUrl(const GURL& url,
                                                         Client* client) {
  NOTREACHED();
  return true;
}

AsyncMatch
RemoteSafeBrowsingDatabaseManager::CheckUrlForHighConfidenceAllowlist(
    const GURL& url,
    Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!enabled_ || !CanCheckUrl(url))
    return AsyncMatch::NO_MATCH;

  // TODO(crbug.com/1014202): Make this call async.
  bool is_match = SafeBrowsingApiHandlerBridge::GetInstance()
                      .StartHighConfidenceAllowlistCheck(url);
  return is_match ? AsyncMatch::MATCH : AsyncMatch::NO_MATCH;
}

bool RemoteSafeBrowsingDatabaseManager::CheckUrlForAccuracyTips(
    const GURL& url,
    Client* client) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::CheckUrlForSubresourceFilter(
    const GURL& url,
    Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!enabled_ || !CanCheckUrl(url))
    return true;

  std::unique_ptr<ClientRequest> req(new ClientRequest(client, this, url));

  DVLOG(1) << "Checking for client " << client << " and URL " << url;
  auto callback =
      std::make_unique<SafeBrowsingApiHandlerBridge::ResponseCallback>(
          base::BindOnce(&ClientRequest::OnRequestDoneWeak, req->GetWeakPtr()));
  SafeBrowsingApiHandlerBridge::GetInstance().StartURLCheck(
      std::move(callback), url,
      CreateSBThreatTypeSet(
          {SB_THREAT_TYPE_SUBRESOURCE_FILTER, SB_THREAT_TYPE_URL_PHISHING}));

  current_requests_.push_back(req.release());

  // Defer the resource load.
  return false;
}

AsyncMatch RemoteSafeBrowsingDatabaseManager::CheckCsdAllowlistUrl(
    const GURL& url,
    Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // If this URL's scheme isn't supported, call is safe.
  if (!CanCheckUrl(url)) {
    return AsyncMatch::MATCH;
  }

  // TODO(crbug.com/995926): Make this call async.
  bool is_match =
      SafeBrowsingApiHandlerBridge::GetInstance().StartCSDAllowlistCheck(url);
  return is_match ? AsyncMatch::MATCH : AsyncMatch::NO_MATCH;
}

bool RemoteSafeBrowsingDatabaseManager::MatchDownloadAllowlistUrl(
    const GURL& url) {
  NOTREACHED();
  return true;
}

bool RemoteSafeBrowsingDatabaseManager::MatchMalwareIP(
    const std::string& ip_address) {
  NOTREACHED();
  return false;
}

safe_browsing::ThreatSource RemoteSafeBrowsingDatabaseManager::GetThreatSource()
    const {
  return safe_browsing::ThreatSource::REMOTE;
}

bool RemoteSafeBrowsingDatabaseManager::IsDownloadProtectionEnabled() const {
  return false;
}

void RemoteSafeBrowsingDatabaseManager::StartOnIOThread(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const V4ProtocolConfig& config) {
  VLOG(1) << "RemoteSafeBrowsingDatabaseManager starting";
  SafeBrowsingDatabaseManager::StartOnIOThread(url_loader_factory, config);

  enabled_ = true;
}

void RemoteSafeBrowsingDatabaseManager::StopOnIOThread(bool shutdown) {
  // |shutdown| is not used.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DVLOG(1) << "RemoteSafeBrowsingDatabaseManager stopping";

  // Call back and delete any remaining clients. OnRequestDone() modifies
  // |current_requests_|, so we make a copy first.
  std::vector<ClientRequest*> to_callback(current_requests_);
  for (auto* req : to_callback) {
    DVLOG(1) << "Stopping: Invoking unfinished req for URL " << req->url();
    req->OnRequestDone(SB_THREAT_TYPE_SAFE, ThreatMetadata());
  }
  enabled_ = false;

  SafeBrowsingDatabaseManager::StopOnIOThread(shutdown);
}

}  // namespace safe_browsing
