// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAFE_BROWSING_CONTENT_BROWSER_THREAT_DETAILS_H_
#define COMPONENTS_SAFE_BROWSING_CONTENT_BROWSER_THREAT_DETAILS_H_

// A class that encapsulates the detailed threat reports sent when
// users opt-in to do so from the safe browsing warning page.

// An instance of this class is generated when a safe browsing warning page
// is shown (SafeBrowsingBlockingPage).

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "components/safe_browsing/content/common/safe_browsing.mojom.h"
#include "components/safe_browsing/core/common/proto/csd.pb.h"
#include "components/security_interstitials/core/unsafe_resource.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/weak_document_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace history {
class HistoryService;
}  // namespace history

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace safe_browsing {

class BaseUIManager;
class ReferrerChainProvider;

// Maps a URL to its Resource.
class ThreatDetailsCacheCollector;
class ThreatDetailsRedirectsCollector;
class ThreatDetailsFactory;

using ResourceMap = std::unordered_map<
    std::string,
    std::unique_ptr<ClientSafeBrowsingReportRequest::Resource>>;

// Maps a key of an HTML element to its corresponding HTMLElement proto message.
// HTML Element keys have the form "<frame_id>-<node_id>", where |frame_id| is
// the FrameTreeNode ID of the frame containing the element, and
// |node_id| is a sequential ID for the element generated by the renderer.
using ElementMap =
    std::unordered_map<std::string, std::unique_ptr<HTMLElement>>;

// Maps the key of an iframe element to the FrameTreeNode ID of the frame that
// rendered the contents of the iframe.
using KeyToFrameTreeIdMap = std::unordered_map<std::string, int>;

// Maps a FrameTreeNode ID of a frame to a set of child IDs. The child IDs are
// the Element IDs of the top-level HTML Elements in this frame.
using FrameTreeIdToChildIdsMap =
    std::unordered_map<int, std::unordered_set<int>>;

// Callback used to notify a caller that ThreatDetails has finished creating and
// sending a report.
using ThreatDetailsDoneCallback =
    base::OnceCallback<void(content::WebContents*)>;

class ThreatDetails {
 public:
  typedef security_interstitials::UnsafeResource UnsafeResource;

  ThreatDetails(const ThreatDetails&) = delete;
  ThreatDetails& operator=(const ThreatDetails&) = delete;

  virtual ~ThreatDetails();

  // Constructs a new ThreatDetails instance, using the factory.
  static std::unique_ptr<ThreatDetails> NewThreatDetails(
      BaseUIManager* ui_manager,
      content::WebContents* web_contents,
      const UnsafeResource& resource,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      history::HistoryService* history_service,
      ReferrerChainProvider* referrer_chain_provider,
      bool trim_to_ad_tags,
      ThreatDetailsDoneCallback done_callback);

  // Makes the passed |factory| the factory used to instantiate
  // SafeBrowsingBlockingPage objects. Useful for tests.
  static void RegisterFactory(ThreatDetailsFactory* factory) {
    factory_ = factory;
  }

  // The SafeBrowsingBlockingPage calls this from the IO thread when
  // the user is leaving the blocking page and has opted-in to sending
  // the report. We start the redirection urls collection from history service
  // in UI thread; then do cache collection back in IO thread. We also record
  // if the user did proceed with the warning page, and how many times user
  // visited this page before. When we are done, we send the report.
  virtual void FinishCollection(bool did_proceed, int num_visits);

  void OnCacheCollectionReady();

  // Overridden during tests
  virtual void OnRedirectionCollectionReady();

  base::WeakPtr<ThreatDetails> GetWeakPtr();

 protected:
  friend class ThreatDetailsFactoryImpl;
  friend class TestThreatDetailsFactory;
  friend class ThreatDetailsTest;

  ThreatDetails(
      BaseUIManager* ui_manager,
      content::WebContents* web_contents,
      const UnsafeResource& resource,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      history::HistoryService* history_service,
      ReferrerChainProvider* referrer_chain_provider,
      bool trim_to_ad_tags,
      ThreatDetailsDoneCallback done_callback);

  // Default constructor for testing only.
  ThreatDetails();

  virtual void AddDOMDetails(const int frame_tree_node_id,
                             std::vector<mojom::ThreatDOMDetailsNodePtr> params,
                             const KeyToFrameTreeIdMap& child_frame_tree_map);

  // The report protocol buffer.
  std::unique_ptr<ClientSafeBrowsingReportRequest> report_;

  // Used to get a pointer to the HTTP cache.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // Starts the collection of the report.
  void StartCollection();

 private:
  // Whether the url is "public" so we can add it to the report.
  bool IsReportableUrl(const GURL& url) const;

  // Finds an existing Resource for the given url, or creates a new one if not
  // found, and adds it to |resources_|. Returns the found/created resource.
  ClientSafeBrowsingReportRequest::Resource* FindOrCreateResource(
      const GURL& url);

  // Finds an existing HTMLElement for a given key, or creates a new one if not
  // found and adds it to |elements_|. Returns the found/created element.
  HTMLElement* FindOrCreateElement(const std::string& element_key);

  // Adds a Resource to resources_ with the given parent-child
  // relationship. |parent| and |tagname| can be empty, |children| can be NULL.
  // Returns the Resource that was affected, or null if no work was done.
  ClientSafeBrowsingReportRequest::Resource* AddUrl(
      const GURL& url,
      const GURL& parent,
      const std::string& tagname,
      const std::vector<GURL>* children);

  void RequestThreatDOMDetails(content::RenderFrameHost* frame);

  void OnReceivedThreatDOMDetails(
      mojo::Remote<mojom::ThreatReporter> threat_reporter,
      content::WeakDocumentPtr sender,
      std::vector<mojom::ThreatDOMDetailsNodePtr> params);

  void AddRedirectUrlList(const std::vector<GURL>& urls);

  // Adds an HTML Element to the DOM structure. |frame_tree_node_id| is the
  // unique ID of the frame the element came from. |element_node_id| is a unique
  // ID of the element within the frame. |tag_name| is the tag of the element.
  // |parent_element_node_id| is the unique ID of the parent element within the
  // frame. |attributes| contains the names and values of the element's
  // attributes. |inner_html| is set if the element contains inline JavaScript.
  // |resource| is set if this element is a resource.
  void AddDomElement(const int frame_tree_node_id,
                     const int element_node_id,
                     const std::string& tag_name,
                     const int parent_element_node_id,
                     const std::vector<mojom::AttributeNameValuePtr> attributes,
                     const std::string& inner_html,
                     const ClientSafeBrowsingReportRequest::Resource* resource);

  // Populates the referrer chain data in |report_|. This may be skipped if the
  // referrer chain provider isn't available, or the type of report doesn't
  // include the referrer chain.
  void MaybeFillReferrerChain();

  // Called when the report is complete. Runs |done_callback_|.
  void AllDone();

  // `this` is owned by TriggerManager which prevents this from outliving
  // the WebContents.
  raw_ptr<content::WebContents> web_contents_ = nullptr;

  scoped_refptr<BaseUIManager> ui_manager_;

  raw_ptr<content::BrowserContext> browser_context_;

  const UnsafeResource resource_;

  raw_ptr<ReferrerChainProvider> referrer_chain_provider_;

  // For every Url we collect we create a Resource message. We keep
  // them in a map so we can avoid duplicates.
  ResourceMap resources_;

  // Store all HTML elements collected, keep them in a map for easy lookup.
  ElementMap elements_;

  // For each iframe element encountered we map the key of the iframe to the
  // FrameTreeNode ID of the frame containing the contents of that iframe.
  // We populate this map when receiving results from ThreatDomDetails, and use
  // it in a second pass (after FinishCollection) to attach children to iframe
  // elements.
  // Should only be accessed on the IO thread.
  KeyToFrameTreeIdMap iframe_key_to_frame_tree_id_map_;

  // When getting a set of elements from a frame, we store the frame's
  // FrameTreeNode ID and a collection of all top-level elements in that frame.
  // It is populated as we receive sets of nodes from different renderers.
  // It is used together with |iframe_key_to_frame_tree_id_map_| in a second
  // pass to insert child elements under their parent iframe elements.
  FrameTreeIdToChildIdsMap frame_tree_id_to_children_map_;

  // Result from the cache extractor.
  bool cache_result_;

  // Whether user did proceed with the safe browsing blocking page or
  // not.
  bool did_proceed_;

  // How many times this user has visited this page before.
  int num_visits_;

  // Whether this report should be trimmed down to only ad tags, not the entire
  // page contents. Used for sampling ads.
  bool trim_to_ad_tags_;

  // A vector containing the IDs of the DOM Elements to trim to. If an element
  // ID is in this list, then its siblings and its children should be included
  // in the report. Only populated if this report will be trimmed.
  std::set<int> trimmed_dom_element_ids_;

  // The factory used to instantiate SafeBrowsingBlockingPage objects.
  // Useful for tests, so they can provide their own implementation of
  // SafeBrowsingBlockingPage.
  static ThreatDetailsFactory* factory_;

  // Used to collect details from the HTTP Cache.
  std::unique_ptr<ThreatDetailsCacheCollector> cache_collector_;

  // Used to collect redirect urls from the history service
  std::unique_ptr<ThreatDetailsRedirectsCollector> redirects_collector_;

  // Callback to run when the report is finished.
  ThreatDetailsDoneCallback done_callback_;

  // Whether this ThreatDetails has begun finalizing the report and is expected
  // to invoke |done_callback_| when it finishes.
  bool all_done_expected_;

  // Whether the |done_callback_| has been invoked.
  bool is_all_done_;

  // Used for references to |this| bound in callbacks.
  base::WeakPtrFactory<ThreatDetails> weak_factory_{this};

  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, HistoryServiceUrls);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, HttpsResourceSanitization);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, HTTPCacheNoEntries);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, HTTPCache);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, ThreatDOMDetails_AmbiguousDOM);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest,
                           ThreatDOMDetails_EmptyReportNotSent);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, ThreatDOMDetails_MultipleFrames);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, ThreatDOMDetails_TrimToAdTags);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, ThreatDOMDetails);
  FRIEND_TEST_ALL_PREFIXES(ThreatDetailsTest, CanCancelDuringCollection);
};

// Factory for creating ThreatDetails.  Useful for tests.
class ThreatDetailsFactory {
 public:
  virtual ~ThreatDetailsFactory() {}

  virtual std::unique_ptr<ThreatDetails> CreateThreatDetails(
      BaseUIManager* ui_manager,
      content::WebContents* web_contents,
      const security_interstitials::UnsafeResource& unsafe_resource,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      history::HistoryService* history_service,
      ReferrerChainProvider* referrer_chain_provider,
      bool trim_to_ad_tags,
      ThreatDetailsDoneCallback done_callback) = 0;
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_CONTENT_BROWSER_THREAT_DETAILS_H_
