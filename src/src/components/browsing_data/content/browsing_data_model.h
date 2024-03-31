// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_BROWSING_DATA_CONTENT_BROWSING_DATA_MODEL_H_
#define COMPONENTS_BROWSING_DATA_CONTENT_BROWSING_DATA_MODEL_H_

#include <map>

#include "base/callback_forward.h"
#include "base/containers/enum_set.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class StoragePartition;
}

// Provides a model interface into a collection of Browsing Data for use in the
// UI. Exposes a uniform view into browsing data based on the concept of
// "primary hosts", which denote which host the data should be closely
// associated with in UI surfaces.
// TODO(crbug.com/1271155): Implementation in progress, should not be used.
class BrowsingDataModel {
 public:
  // Storage types which are represented by the model. Some types have
  // incomplete implementations, and are marked as such.
  // TODO(crbug.com/1271155): Complete implementations for all browsing data.
  enum class StorageType {
    kTrustTokens,                // Only issuance information considered.
    kPartitionedQuotaStorage,    // Not fetched from disk or deleted.
    kUnpartitionedQuotaStorage,  // Not fetched from disk or deleted.

    kFirstType = kTrustTokens,
    kLastType = kUnpartitionedQuotaStorage,
  };
  using StorageTypeSet = base::
      EnumSet<StorageType, StorageType::kFirstType, StorageType::kLastType>;

  // The information which uniquely identifies this browsing data. The set of
  // data an entry represents can be pulled from the relevant storage backends
  // using this information.
  typedef absl::variant<url::Origin,       // Single origin, e.g. Trust Tokens
                        blink::StorageKey  // Partitioned JS storage
                        // TODO(crbug.com/1271155): Additional backend keys.
                        >
      DataKey;

  // Information about the data pointed at by a DataKey.
  struct DataDetails {
    ~DataDetails();
    bool operator==(const DataDetails& other) const;

    // An EnumSet of storage types for this data.
    StorageTypeSet storage_types;

    // The on-disk size of this storage.
    uint64_t storage_size = 0;

    // The number of cookies included in this storage. This is only included to
    // support legacy UI surfaces.
    // TODO(crbug.com/1359998): Remove this when UI no longer requires it.
    uint64_t cookie_count = 0;
  };

  // A view of a single "unit" of browsing data. Considered a "view" as it holds
  // references to data contained within the model.
  struct BrowsingDataEntryView {
    ~BrowsingDataEntryView();
    BrowsingDataEntryView(const BrowsingDataEntryView& other) = delete;

    // The primary host for this browsing data. This is the host which this
    // information will be most strongly associated with in UX surfaces.
    const std::string& primary_host;

    // The unique identifier for the data represented by this entry.
    const DataKey& data_key;

    // Information about the data represented by this entry.
    const DataDetails& data_details;

   private:
    friend class BrowsingDataModel;

    BrowsingDataEntryView(const std::string& primary_host,
                          const DataKey& data_key,
                          const DataDetails& data_details);
  };

  // The model provides a single interface for retrieving browsing data, in the
  // form of an Input iterator (read-only, increment only, no random access)
  // over BrowsingDataEntryViews.
  // Iterators are invalidated whenever the model is updated.
  using DataKeyEntries = std::map<DataKey, DataDetails>;
  using BrowsingDataEntries = std::map<std::string, DataKeyEntries>;
  struct Iterator {
    ~Iterator();
    Iterator(const Iterator& iterator);
    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;

    // Input iterator functionality:
    BrowsingDataEntryView operator*() const;
    Iterator& operator++();

   private:
    friend class BrowsingDataModel;

    Iterator(BrowsingDataEntries::const_iterator outer_iterator,
             BrowsingDataEntries::const_iterator end_outer_iterator);

    BrowsingDataEntries::const_iterator outer_iterator_;
    DataKeyEntries::const_iterator inner_iterator_;
    const BrowsingDataEntries::const_iterator outer_end_iterator_;
  };

  Iterator begin() const;
  Iterator end() const;

  ~BrowsingDataModel();

  // Consults supported storage backends to create and populate a Model based
  // on the current state of `browser_context`.
  static void BuildFromDisk(
      content::BrowserContext* browser_context,
      base::OnceCallback<void(std::unique_ptr<BrowsingDataModel>)>
          complete_callback);

  // Creates and returns an empty model, for population via AddBrowsingData().
  static std::unique_ptr<BrowsingDataModel> BuildEmpty(
      content::BrowserContext* browser_context);

  // Directly add browsing data to the Model. The appropriate BrowsingDataEntry
  // will be created or modified. Typically this should only be used when the
  // model was created using BuildEmpty().
  void AddBrowsingData(const DataKey& data_key,
                       StorageType storage_type,
                       uint64_t storage_size,
                       // TODO(crbug.com/1359998): Deprecate cookie count.
                       uint64_t cookie_count = 0);

  // Removes all browsing data associated with `primary_host`, reaches out to
  // all supported storage backends to remove the data, and updates the model.
  // Deletion at more granularity than `primary_host` is purposefully not
  // supported by this model. UI that wishes to support such deletion should
  // consider whether it is really required, and if so, implement it separately.
  // The in-memory representation of the model is updated immediately, while
  // actual deletion from disk occurs async, completion reported by `completed`.
  // Invalidates any iterators.
  void RemoveBrowsingData(const std::string& primary_host,
                          base::OnceClosure completed);

 private:
  friend class BrowsingDataModelTest;

  // Private as one of the static BuildX functions should be used instead.
  explicit BrowsingDataModel(
      content::StoragePartition* storage_partition
      // TODO(crbug.com/1271155): Inject other dependencies.
  );

  // Pulls information from disk and populate the model.
  void PopulateFromDisk(base::OnceClosure finished_callback);

  // Backing data structure for this model. Is a map from primary hosts to a
  // list of tuples (stored as a map) of <DataKey, DataDetails>. Building the
  // model required updating existing entries as data becomes available, so
  // fast lookup is required. Similarly, keying the outer map on primary host
  // supports removal by primary host performantly.
  BrowsingDataEntries browsing_data_entries_;

  // Non-owning pointers to storage backends. All derivable from a browser
  // context, but broken out to allow easier injection in tests.
  // TODO(crbug.com/1271155): More backends to come, they should all be broken
  // out from the browser context at the appropriate level.
  raw_ptr<content::StoragePartition> storage_partition_;
};

#endif  // COMPONENTS_BROWSING_DATA_CONTENT_BROWSING_DATA_MODEL_H_
