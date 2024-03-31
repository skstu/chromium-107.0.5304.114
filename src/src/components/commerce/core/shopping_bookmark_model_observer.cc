// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/commerce/core/shopping_bookmark_model_observer.h"

#include <memory>
#include <vector>

#include "base/callback.h"
#include "base/strings/string_number_conversions.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/commerce/core/price_tracking_utils.h"
#include "components/commerce/core/shopping_service.h"
#include "components/commerce/core/subscriptions/commerce_subscription.h"
#include "components/commerce/core/subscriptions/subscriptions_manager.h"
#include "components/power_bookmarks/core/power_bookmark_utils.h"
#include "components/power_bookmarks/core/proto/power_bookmark_meta.pb.h"
#include "components/power_bookmarks/core/proto/shopping_specifics.pb.h"

namespace commerce {

ShoppingBookmarkModelObserver::ShoppingBookmarkModelObserver(
    bookmarks::BookmarkModel* model,
    ShoppingService* shopping_service,
    SubscriptionsManager* subscriptions_manager)
    : shopping_service_(shopping_service),
      subscriptions_manager_(subscriptions_manager) {
  scoped_observation_.Observe(model);
}

ShoppingBookmarkModelObserver::~ShoppingBookmarkModelObserver() = default;

void ShoppingBookmarkModelObserver::BookmarkModelChanged() {}

void ShoppingBookmarkModelObserver::OnWillChangeBookmarkNode(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* node) {
  // Since the node is about to change, map its current known URL.
  node_to_url_map_[node->id()] = node->url();
}

void ShoppingBookmarkModelObserver::BookmarkNodeChanged(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* node) {
  if (node_to_url_map_[node->id()] != node->url()) {
    // If the URL did change, clear the power bookmark shopping meta and
    // unsubscribe if needed.
    std::unique_ptr<power_bookmarks::PowerBookmarkMeta> meta =
        power_bookmarks::GetNodePowerBookmarkMeta(model, node);

    if (meta && meta->has_shopping_specifics()) {
      power_bookmarks::ShoppingSpecifics* specifics =
          meta->mutable_shopping_specifics();

      uint64_t cluster_id = specifics->product_cluster_id();
      meta->clear_shopping_specifics();
      power_bookmarks::SetNodePowerBookmarkMeta(model, node, std::move(meta));

      // Unsubscribe using the cluster ID
      if (shopping_service_) {
        std::vector<const bookmarks::BookmarkNode*> bookmarks_with_cluster =
            GetBookmarksWithClusterId(model, cluster_id);

        // If there are no other bookmarks with the node's cluster ID,
        // unsubscribe.
        if (bookmarks_with_cluster.empty())
          SetPriceTrackingStateForBookmark(shopping_service_, model, node,
                                           false,
                                           base::BindOnce([](bool success) {}));
      }
    }
  }

  node_to_url_map_.erase(node->id());
}

void ShoppingBookmarkModelObserver::BookmarkNodeRemoved(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* parent,
    size_t old_index,
    const bookmarks::BookmarkNode* node,
    const std::set<GURL>& removed_urls) {
  // If the number of bookmarks with the node's cluster ID is now 0, unsubscribe
  // from the product.
  std::unique_ptr<power_bookmarks::PowerBookmarkMeta> meta =
      power_bookmarks::GetNodePowerBookmarkMeta(model, node);

  if (!meta || !meta->has_shopping_specifics())
    return;

  power_bookmarks::ShoppingSpecifics* specifics =
      meta->mutable_shopping_specifics();

  std::vector<const bookmarks::BookmarkNode*> bookmarks_with_cluster =
      GetBookmarksWithClusterId(model, specifics->product_cluster_id());

  // If there are other bookmarks with the node's cluster ID, do nothing.
  if (!bookmarks_with_cluster.empty())
    return;

  SetPriceTrackingStateForBookmark(shopping_service_, model, node, false,
                                   base::BindOnce([](bool success) {}));
}

void ShoppingBookmarkModelObserver::BookmarkMetaInfoChanged(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* node) {
  std::unique_ptr<power_bookmarks::PowerBookmarkMeta> meta =
      power_bookmarks::GetNodePowerBookmarkMeta(model, node);

  // If the changed bookmark is a shopping item, we check its tracking status
  // with local subscriptions and if inconsistent, we need to sync local
  // subscriptions with the server. This is mainly used to keep local
  // subscriptions up to date when users operate on multiple devices.
  if (meta && meta->has_shopping_specifics() && subscriptions_manager_) {
    power_bookmarks::ShoppingSpecifics* specifics =
        meta->mutable_shopping_specifics();
    uint64_t cluster_id = specifics->product_cluster_id();

    CommerceSubscription sub(
        SubscriptionType::kPriceTrack, IdentifierType::kProductClusterId,
        base::NumberToString(cluster_id), ManagementType::kUserManaged);

    subscriptions_manager_->VerifyIfSubscriptionExists(
        std::move(sub), specifics->is_price_tracked());
  }
}

}  // namespace commerce
