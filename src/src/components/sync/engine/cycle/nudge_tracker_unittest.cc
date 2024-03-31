// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/engine/cycle/nudge_tracker.h"

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "base/run_loop.h"
#include "components/sync/protocol/data_type_progress_marker.pb.h"
#include "components/sync/test/mock_invalidation.h"
#include "components/sync/test/mock_invalidation_tracker.h"
#include "components/sync/test/model_type_test_util.h"
#include "components/sync/test/trackable_mock_invalidation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

const size_t kHintBufferSize = 5;

testing::AssertionResult ModelTypeSetEquals(ModelTypeSet a, ModelTypeSet b) {
  if (a == b) {
    return testing::AssertionSuccess();
  } else {
    return testing::AssertionFailure()
           << "Left side " << ModelTypeSetToDebugString(a)
           << ", does not match rigth side: " << ModelTypeSetToDebugString(b);
  }
}

}  // namespace

class NudgeTrackerTest : public ::testing::Test {
 public:
  NudgeTrackerTest() {
    // Override this limit so tests know when it is surpassed.
    nudge_tracker_.SetHintBufferSize(kHintBufferSize);
    SetInvalidationsInSync();
  }

  bool InvalidationsOutOfSync() const {
    // We don't currently track invalidations out of sync on a per-type basis.
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    return gu_trigger.invalidations_out_of_sync();
  }

  int ProtoLocallyModifiedCount(ModelType type) const {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(type, &gu_trigger);
    return gu_trigger.local_modification_nudges();
  }

  int ProtoRefreshRequestedCount(ModelType type) const {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(type, &gu_trigger);
    return gu_trigger.datatype_refresh_nudges();
  }

  void SetInvalidationsInSync() {
    nudge_tracker_.OnInvalidationsEnabled();
    nudge_tracker_.RecordSuccessfulSyncCycle({});
  }

  std::unique_ptr<SyncInvalidation> BuildInvalidation(
      int64_t version,
      const std::string& payload) {
    return MockInvalidation::Build(version, payload);
  }

  static std::unique_ptr<SyncInvalidation> BuildUnknownVersionInvalidation() {
    return MockInvalidation::BuildUnknownVersion();
  }

  bool IsTypeThrottled(ModelType type) {
    return nudge_tracker_.GetTypeBlockingMode(type) ==
           WaitInterval::BlockingMode::kThrottled;
  }

  bool IsTypeBackedOff(ModelType type) {
    return nudge_tracker_.GetTypeBlockingMode(type) ==
           WaitInterval::BlockingMode::kExponentialBackoff;
  }

 protected:
  NudgeTracker nudge_tracker_;
};

// Exercise an empty NudgeTracker.
// Use with valgrind to detect uninitialized members.
TEST_F(NudgeTrackerTest, EmptyNudgeTracker) {
  // Now we're at the normal, "idle" state.
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
  EXPECT_EQ(sync_pb::SyncEnums::UNKNOWN_ORIGIN, nudge_tracker_.GetOrigin());

  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);

  EXPECT_EQ(sync_pb::SyncEnums::UNKNOWN_ORIGIN, nudge_tracker_.GetOrigin());
}

// Verify that nudges override each other based on a priority order.
// RETRY < all variants of GU_TRIGGER
TEST_F(NudgeTrackerTest, OriginPriorities) {
  // Start with a retry request.
  const base::TimeTicks t0 = base::TimeTicks() + base::Microseconds(1234);
  const base::TimeTicks t1 = t0 + base::Seconds(10);
  nudge_tracker_.SetNextRetryTime(t0);
  nudge_tracker_.SetSyncCycleStartTime(t1);
  EXPECT_EQ(sync_pb::SyncEnums::RETRY, nudge_tracker_.GetOrigin());

  // Track a local nudge.
  nudge_tracker_.RecordLocalChange(BOOKMARKS);
  EXPECT_EQ(sync_pb::SyncEnums::GU_TRIGGER, nudge_tracker_.GetOrigin());

  // A refresh request will override it.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(TYPED_URLS));
  EXPECT_EQ(sync_pb::SyncEnums::GU_TRIGGER, nudge_tracker_.GetOrigin());

  // Another local nudge will not be enough to change it.
  nudge_tracker_.RecordLocalChange(BOOKMARKS);
  EXPECT_EQ(sync_pb::SyncEnums::GU_TRIGGER, nudge_tracker_.GetOrigin());

  // An invalidation will override the refresh request source.
  nudge_tracker_.RecordRemoteInvalidation(PREFERENCES,
                                          BuildInvalidation(1, "hint"));
  EXPECT_EQ(sync_pb::SyncEnums::GU_TRIGGER, nudge_tracker_.GetOrigin());

  // Neither local nudges nor refresh requests will override it.
  nudge_tracker_.RecordLocalChange(BOOKMARKS);
  EXPECT_EQ(sync_pb::SyncEnums::GU_TRIGGER, nudge_tracker_.GetOrigin());
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(TYPED_URLS));
  EXPECT_EQ(sync_pb::SyncEnums::GU_TRIGGER, nudge_tracker_.GetOrigin());
}

// Verifies the management of invalidation hints and GU trigger fields.
TEST_F(NudgeTrackerTest, HintCoalescing) {
  // Easy case: record one hint.
  {
    nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                            BuildInvalidation(1, "bm_hint_1"));

    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    ASSERT_EQ(1, gu_trigger.notification_hint_size());
    EXPECT_EQ("bm_hint_1", gu_trigger.notification_hint(0));
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
  }

  // Record a second hint for the same type.
  {
    nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                            BuildInvalidation(2, "bm_hint_2"));

    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    ASSERT_EQ(2, gu_trigger.notification_hint_size());

    // Expect the most hint recent is last in the list.
    EXPECT_EQ("bm_hint_1", gu_trigger.notification_hint(0));
    EXPECT_EQ("bm_hint_2", gu_trigger.notification_hint(1));
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
  }

  // Record a hint for a different type.
  {
    nudge_tracker_.RecordRemoteInvalidation(PASSWORDS,
                                            BuildInvalidation(1, "pw_hint_1"));

    // Re-verify the bookmarks to make sure they're unaffected.
    sync_pb::GetUpdateTriggers bm_gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &bm_gu_trigger);
    ASSERT_EQ(2, bm_gu_trigger.notification_hint_size());
    EXPECT_EQ("bm_hint_1", bm_gu_trigger.notification_hint(0));
    EXPECT_EQ("bm_hint_2",
              bm_gu_trigger.notification_hint(1));  // most recent last.
    EXPECT_FALSE(bm_gu_trigger.client_dropped_hints());

    // Verify the new type, too.
    sync_pb::GetUpdateTriggers pw_gu_trigger;
    nudge_tracker_.FillProtoMessage(PASSWORDS, &pw_gu_trigger);
    ASSERT_EQ(1, pw_gu_trigger.notification_hint_size());
    EXPECT_EQ("pw_hint_1", pw_gu_trigger.notification_hint(0));
    EXPECT_FALSE(pw_gu_trigger.client_dropped_hints());
  }
}

// Test the dropping of invalidation hints.  Receives invalidations one by one.
TEST_F(NudgeTrackerTest, DropHintsLocally_OneAtATime) {
  for (size_t i = 0; i < kHintBufferSize; ++i) {
    nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                            BuildInvalidation(i, "hint"));
  }
  {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    EXPECT_EQ(kHintBufferSize,
              static_cast<size_t>(gu_trigger.notification_hint_size()));
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
  }

  // Force an overflow.
  nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                          BuildInvalidation(1000, "new_hint"));

  {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    EXPECT_TRUE(gu_trigger.client_dropped_hints());
    ASSERT_EQ(kHintBufferSize,
              static_cast<size_t>(gu_trigger.notification_hint_size()));

    // Verify the newest hint was not dropped and is the last in the list.
    EXPECT_EQ("new_hint", gu_trigger.notification_hint(kHintBufferSize - 1));

    // Verify the oldest hint, too.
    EXPECT_EQ("hint", gu_trigger.notification_hint(0));
  }
}

// Tests the receipt of 'unknown version' invalidations.
TEST_F(NudgeTrackerTest, DropHintsAtServer_Alone) {
  // Record the unknown version invalidation.
  nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                          BuildUnknownVersionInvalidation());
  {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    EXPECT_TRUE(gu_trigger.server_dropped_hints());
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
    ASSERT_EQ(0, gu_trigger.notification_hint_size());
  }

  // Clear status then verify.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
    EXPECT_FALSE(gu_trigger.server_dropped_hints());
    ASSERT_EQ(0, gu_trigger.notification_hint_size());
  }
}

// Tests the receipt of 'unknown version' invalidations.  This test also
// includes a known version invalidation to mix things up a bit.
TEST_F(NudgeTrackerTest, DropHintsAtServer_WithOtherInvalidations) {
  // Record the two invalidations, one with unknown version, the other known.
  nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                          BuildUnknownVersionInvalidation());
  nudge_tracker_.RecordRemoteInvalidation(BOOKMARKS,
                                          BuildInvalidation(10, "hint"));

  {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    EXPECT_TRUE(gu_trigger.server_dropped_hints());
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
    ASSERT_EQ(1, gu_trigger.notification_hint_size());
    EXPECT_EQ("hint", gu_trigger.notification_hint(0));
  }

  // Clear status then verify.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  {
    sync_pb::GetUpdateTriggers gu_trigger;
    nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
    EXPECT_FALSE(gu_trigger.client_dropped_hints());
    EXPECT_FALSE(gu_trigger.server_dropped_hints());
    ASSERT_EQ(0, gu_trigger.notification_hint_size());
  }
}

// Checks the behaviour of the invalidations-out-of-sync flag.
TEST_F(NudgeTrackerTest, EnableDisableInvalidations) {
  // Start with invalidations offline.
  nudge_tracker_.OnInvalidationsDisabled();
  EXPECT_TRUE(InvalidationsOutOfSync());
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Simply enabling invalidations does not bring us back into sync.
  nudge_tracker_.OnInvalidationsEnabled();
  EXPECT_TRUE(InvalidationsOutOfSync());
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // We must successfully complete a sync cycle while invalidations are enabled
  // to be sure that we're in sync.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(InvalidationsOutOfSync());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // If the invalidator malfunctions, we go become unsynced again.
  nudge_tracker_.OnInvalidationsDisabled();
  EXPECT_TRUE(InvalidationsOutOfSync());
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // A sync cycle while invalidations are disabled won't reset the flag.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(InvalidationsOutOfSync());
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Nor will the re-enabling of invalidations be sufficient, even now that
  // we've had a successful sync cycle.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(InvalidationsOutOfSync());
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
}

// Tests that locally modified types are correctly written out to the
// GetUpdateTriggers proto.
TEST_F(NudgeTrackerTest, WriteLocallyModifiedTypesToProto) {
  // Should not be locally modified by default.
  EXPECT_EQ(0, ProtoLocallyModifiedCount(PREFERENCES));

  // Record a local bookmark change.  Verify it was registered correctly.
  nudge_tracker_.RecordLocalChange(PREFERENCES);
  EXPECT_EQ(1, ProtoLocallyModifiedCount(PREFERENCES));

  // Record a successful sync cycle.  Verify the count is cleared.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_EQ(0, ProtoLocallyModifiedCount(PREFERENCES));
}

// Tests that refresh requested types are correctly written out to the
// GetUpdateTriggers proto.
TEST_F(NudgeTrackerTest, WriteRefreshRequestedTypesToProto) {
  // There should be no refresh requested by default.
  EXPECT_EQ(0, ProtoRefreshRequestedCount(SESSIONS));

  // Record a local refresh request.  Verify it was registered correctly.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(SESSIONS));
  EXPECT_EQ(1, ProtoRefreshRequestedCount(SESSIONS));

  // Record a successful sync cycle.  Verify the count is cleared.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_EQ(0, ProtoRefreshRequestedCount(SESSIONS));
}

// Basic tests for the IsSyncRequired() flag.
TEST_F(NudgeTrackerTest, IsSyncRequired) {
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // Initial sync request.
  nudge_tracker_.RecordInitialSyncRequired(BOOKMARKS);
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  // Note: The initial sync happens as part of a configuration cycle, not a
  // normal cycle, so here we need to use RecordInitialSyncDone() rather than
  // RecordSuccessfulSyncCycle().
  // A finished initial sync for a different data type doesn't affect us.
  nudge_tracker_.RecordInitialSyncDone({EXTENSIONS});
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  nudge_tracker_.RecordInitialSyncDone({BOOKMARKS});
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // Sync request for resolve conflict.
  nudge_tracker_.RecordCommitConflict(BOOKMARKS);
  // Now a sync is required for BOOKMARKS.
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired({BOOKMARKS}));
  // But not for SESSIONS.
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired({SESSIONS}));
  // A successful cycle for SESSIONS doesn't change anything.
  nudge_tracker_.RecordSuccessfulSyncCycle({SESSIONS});
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  // A successful cycle for all types resolves things.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // Local changes.
  nudge_tracker_.RecordLocalChange(SESSIONS);
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // Refresh requests.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(SESSIONS));
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // Invalidations.
  nudge_tracker_.RecordRemoteInvalidation(PREFERENCES,
                                          BuildInvalidation(1, "hint"));
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // Invalidation is added to GetUpdates trigger message and processed, so
  // after RecordSuccessfulSyncCycle() it'll be deleted.
  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(PREFERENCES, &gu_trigger);
  ASSERT_EQ(1, gu_trigger.notification_hint_size());

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
}

// Basic tests for the IsGetUpdatesRequired() flag.
TEST_F(NudgeTrackerTest, IsGetUpdatesRequired) {
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Initial sync request.
  // TODO(crbug.com/926184): This is probably wrong; a missing initial sync
  // should not cause IsGetUpdatesRequired(): The former happens during config
  // cycles, but the latter refers to normal cycles.
  nudge_tracker_.RecordInitialSyncRequired(BOOKMARKS);
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
  nudge_tracker_.RecordInitialSyncDone(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Local changes.
  nudge_tracker_.RecordLocalChange(SESSIONS);
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Refresh requests.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(SESSIONS));
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Invalidations.
  nudge_tracker_.RecordRemoteInvalidation(PREFERENCES,
                                          BuildInvalidation(1, "hint"));
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Invalidation is added to GetUpdates trigger message and processed, so
  // after RecordSuccessfulSyncCycle() it'll be deleted.
  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(PREFERENCES, &gu_trigger);
  ASSERT_EQ(1, gu_trigger.notification_hint_size());

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
}

// Test IsSyncRequired() responds correctly to data type throttling and backoff.
TEST_F(NudgeTrackerTest, IsSyncRequired_Throttling_Backoff) {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta throttle_length = base::Minutes(0);

  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // A local change to sessions enables the flag.
  nudge_tracker_.RecordLocalChange(SESSIONS);
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // But the throttling of sessions unsets it.
  nudge_tracker_.SetTypesThrottledUntil(ModelTypeSet(SESSIONS), throttle_length,
                                        now);
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // A refresh request for bookmarks means we have reason to sync again.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(BOOKMARKS));
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // But the backoff of bookmarks unsets it.
  nudge_tracker_.SetTypeBackedOff(BOOKMARKS, throttle_length, now);
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_TRUE(IsTypeBackedOff(BOOKMARKS));
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // A refresh request for preferences means we have reason to sync again.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(PREFERENCES));
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // A successful sync cycle means we took care of preferences.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));

  // But we still haven't dealt with sessions and bookmarks. We'll need to
  // remember that sessions and bookmarks are out of sync and re-enable the flag
  // when their throttling and backoff interval expires.
  nudge_tracker_.UpdateTypeThrottlingAndBackoffState();
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(SESSIONS));
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(BOOKMARKS));
  EXPECT_TRUE(nudge_tracker_.IsSyncRequired(ModelTypeSet::All()));
}

// Test IsGetUpdatesRequired() responds correctly to data type throttling and
// backoff.
TEST_F(NudgeTrackerTest, IsGetUpdatesRequired_Throttling_Backoff) {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta throttle_length = base::Minutes(0);

  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // A refresh request to sessions enables the flag.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(SESSIONS));
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // But the throttling of sessions unsets it.
  nudge_tracker_.SetTypesThrottledUntil(ModelTypeSet(SESSIONS), throttle_length,
                                        now);
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // A refresh request for bookmarks means we have reason to sync again.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(BOOKMARKS));
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // But the backoff of bookmarks unsets it.
  nudge_tracker_.SetTypeBackedOff(BOOKMARKS, throttle_length, now);
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_TRUE(IsTypeBackedOff(BOOKMARKS));
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // A refresh request for preferences means we have reason to sync again.
  nudge_tracker_.RecordLocalRefreshRequest(ModelTypeSet(PREFERENCES));
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // A successful sync cycle means we took care of preferences.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // But we still haven't dealt with sessions and bookmarks. We'll need to
  // remember that sessions and bookmarks are out of sync and re-enable the flag
  // when their throttling and backoff interval expires.
  nudge_tracker_.UpdateTypeThrottlingAndBackoffState();
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(SESSIONS));
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(BOOKMARKS));
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));
}

// Tests blocking-related getter functions when no types are blocked.
TEST_F(NudgeTrackerTest, NoTypesBlocked) {
  EXPECT_FALSE(nudge_tracker_.IsAnyTypeBlocked());
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(SESSIONS));
  EXPECT_TRUE(nudge_tracker_.GetBlockedTypes().Empty());
}

// Tests throttling-related getter functions when some types are throttled.
TEST_F(NudgeTrackerTest, ThrottleAndUnthrottle) {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta throttle_length = base::Minutes(0);

  nudge_tracker_.SetTypesThrottledUntil(ModelTypeSet(SESSIONS, PREFERENCES),
                                        throttle_length, now);

  EXPECT_TRUE(nudge_tracker_.IsAnyTypeBlocked());
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_TRUE(IsTypeThrottled(PREFERENCES));
  EXPECT_FALSE(nudge_tracker_.GetBlockedTypes().Empty());
  EXPECT_EQ(throttle_length, nudge_tracker_.GetTimeUntilNextUnblock());

  nudge_tracker_.UpdateTypeThrottlingAndBackoffState();

  EXPECT_FALSE(nudge_tracker_.IsAnyTypeBlocked());
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(SESSIONS));
  EXPECT_TRUE(nudge_tracker_.GetBlockedTypes().Empty());
}

// Tests backoff-related getter functions when some types are backed off.
TEST_F(NudgeTrackerTest, BackoffAndUnbackoff) {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta backoff_length = base::Minutes(0);

  nudge_tracker_.SetTypeBackedOff(SESSIONS, backoff_length, now);
  nudge_tracker_.SetTypeBackedOff(PREFERENCES, backoff_length, now);

  EXPECT_TRUE(nudge_tracker_.IsAnyTypeBlocked());
  EXPECT_TRUE(IsTypeBackedOff(SESSIONS));
  EXPECT_TRUE(IsTypeBackedOff(PREFERENCES));
  EXPECT_FALSE(nudge_tracker_.GetBlockedTypes().Empty());
  EXPECT_EQ(backoff_length, nudge_tracker_.GetTimeUntilNextUnblock());

  nudge_tracker_.UpdateTypeThrottlingAndBackoffState();

  EXPECT_FALSE(nudge_tracker_.IsAnyTypeBlocked());
  EXPECT_FALSE(nudge_tracker_.IsTypeBlocked(SESSIONS));
  EXPECT_TRUE(nudge_tracker_.GetBlockedTypes().Empty());
}

TEST_F(NudgeTrackerTest, OverlappingThrottleIntervals) {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta throttle1_length = base::Minutes(0);
  const base::TimeDelta throttle2_length = base::Minutes(20);

  // Setup the longer of two intervals.
  nudge_tracker_.SetTypesThrottledUntil(ModelTypeSet(SESSIONS, PREFERENCES),
                                        throttle2_length, now);
  EXPECT_TRUE(ModelTypeSetEquals(ModelTypeSet(SESSIONS, PREFERENCES),
                                 nudge_tracker_.GetBlockedTypes()));
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_TRUE(IsTypeThrottled(PREFERENCES));
  EXPECT_GE(throttle2_length, nudge_tracker_.GetTimeUntilNextUnblock());

  // Setup the shorter interval.
  nudge_tracker_.SetTypesThrottledUntil(ModelTypeSet(SESSIONS, BOOKMARKS),
                                        throttle1_length, now);
  EXPECT_TRUE(ModelTypeSetEquals(ModelTypeSet(SESSIONS, PREFERENCES, BOOKMARKS),
                                 nudge_tracker_.GetBlockedTypes()));
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_TRUE(IsTypeThrottled(PREFERENCES));
  EXPECT_TRUE(IsTypeThrottled(BOOKMARKS));
  EXPECT_GE(throttle1_length, nudge_tracker_.GetTimeUntilNextUnblock());

  // Expire the first interval.
  nudge_tracker_.UpdateTypeThrottlingAndBackoffState();

  // SESSIONS appeared in both intervals.  We expect it will be throttled for
  // the longer of the two, so it's still throttled at time t1.
  EXPECT_TRUE(ModelTypeSetEquals(ModelTypeSet(SESSIONS, PREFERENCES),
                                 nudge_tracker_.GetBlockedTypes()));
  EXPECT_TRUE(IsTypeThrottled(SESSIONS));
  EXPECT_TRUE(IsTypeThrottled(PREFERENCES));
  EXPECT_FALSE(IsTypeThrottled(BOOKMARKS));
  EXPECT_GE(throttle2_length - throttle1_length,
            nudge_tracker_.GetTimeUntilNextUnblock());
}

TEST_F(NudgeTrackerTest, OverlappingBackoffIntervals) {
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeDelta backoff1_length = base::Minutes(0);
  const base::TimeDelta backoff2_length = base::Minutes(20);

  // Setup the longer of two intervals.
  nudge_tracker_.SetTypeBackedOff(SESSIONS, backoff2_length, now);
  nudge_tracker_.SetTypeBackedOff(PREFERENCES, backoff2_length, now);
  EXPECT_TRUE(ModelTypeSetEquals(ModelTypeSet(SESSIONS, PREFERENCES),
                                 nudge_tracker_.GetBlockedTypes()));
  EXPECT_TRUE(IsTypeBackedOff(SESSIONS));
  EXPECT_TRUE(IsTypeBackedOff(PREFERENCES));
  EXPECT_GE(backoff2_length, nudge_tracker_.GetTimeUntilNextUnblock());

  // Setup the shorter interval.
  nudge_tracker_.SetTypeBackedOff(SESSIONS, backoff1_length, now);
  nudge_tracker_.SetTypeBackedOff(BOOKMARKS, backoff1_length, now);
  EXPECT_TRUE(ModelTypeSetEquals(ModelTypeSet(SESSIONS, PREFERENCES, BOOKMARKS),
                                 nudge_tracker_.GetBlockedTypes()));
  EXPECT_TRUE(IsTypeBackedOff(SESSIONS));
  EXPECT_TRUE(IsTypeBackedOff(PREFERENCES));
  EXPECT_TRUE(IsTypeBackedOff(BOOKMARKS));
  EXPECT_GE(backoff1_length, nudge_tracker_.GetTimeUntilNextUnblock());

  // Expire the first interval.
  nudge_tracker_.UpdateTypeThrottlingAndBackoffState();

  // SESSIONS appeared in both intervals.  We expect it will be backed off for
  // the longer of the two, so it's still backed off at time t1.
  EXPECT_TRUE(ModelTypeSetEquals(ModelTypeSet(SESSIONS, PREFERENCES),
                                 nudge_tracker_.GetBlockedTypes()));
  EXPECT_TRUE(IsTypeBackedOff(SESSIONS));
  EXPECT_TRUE(IsTypeBackedOff(PREFERENCES));
  EXPECT_FALSE(IsTypeBackedOff(BOOKMARKS));
  EXPECT_GE(backoff2_length - backoff1_length,
            nudge_tracker_.GetTimeUntilNextUnblock());
}

TEST_F(NudgeTrackerTest, Retry) {
  const base::TimeTicks t0 = base::TimeTicks::FromInternalValue(12345);
  const base::TimeTicks t3 = t0 + base::Seconds(3);
  const base::TimeTicks t4 = t0 + base::Seconds(4);

  // Set retry for t3.
  nudge_tracker_.SetNextRetryTime(t3);

  // Not due yet at t0.
  nudge_tracker_.SetSyncCycleStartTime(t0);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // Successful sync cycle at t0 changes nothing.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());
  EXPECT_FALSE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // At t4, the retry becomes due.
  nudge_tracker_.SetSyncCycleStartTime(t4);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());
  EXPECT_TRUE(nudge_tracker_.IsGetUpdatesRequired(ModelTypeSet::All()));

  // A sync cycle unsets the flag.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());

  // It's still unset at the start of the next sync cycle.
  nudge_tracker_.SetSyncCycleStartTime(t4);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());
}

// Test a mid-cycle update when IsRetryRequired() was true before the cycle
// began.
TEST_F(NudgeTrackerTest, IsRetryRequired_MidCycleUpdate1) {
  const base::TimeTicks t0 = base::TimeTicks::FromInternalValue(12345);
  const base::TimeTicks t1 = t0 + base::Seconds(1);
  const base::TimeTicks t2 = t0 + base::Seconds(2);
  const base::TimeTicks t5 = t0 + base::Seconds(5);
  const base::TimeTicks t6 = t0 + base::Seconds(6);

  nudge_tracker_.SetNextRetryTime(t0);
  nudge_tracker_.SetSyncCycleStartTime(t1);

  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // Pretend that we were updated mid-cycle.  SetSyncCycleStartTime is
  // called only at the start of the sync cycle, so don't call it here.
  // The update should have no effect on IsRetryRequired().
  nudge_tracker_.SetNextRetryTime(t5);

  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // Verify that the successful sync cycle clears the flag.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());

  // Verify expecations around the new retry time.
  nudge_tracker_.SetSyncCycleStartTime(t2);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());

  nudge_tracker_.SetSyncCycleStartTime(t6);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());
}

// Test a mid-cycle update when IsRetryRequired() was false before the cycle
// began.
TEST_F(NudgeTrackerTest, IsRetryRequired_MidCycleUpdate2) {
  const base::TimeTicks t0 = base::TimeTicks::FromInternalValue(12345);
  const base::TimeTicks t1 = t0 + base::Seconds(1);
  const base::TimeTicks t3 = t0 + base::Seconds(3);
  const base::TimeTicks t5 = t0 + base::Seconds(5);
  const base::TimeTicks t6 = t0 + base::Seconds(6);

  // Schedule a future retry, and a nudge unrelated to it.
  nudge_tracker_.RecordLocalChange(BOOKMARKS);
  nudge_tracker_.SetNextRetryTime(t1);
  nudge_tracker_.SetSyncCycleStartTime(t0);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());

  // Pretend this happened in mid-cycle.  This should have no effect on
  // IsRetryRequired().
  nudge_tracker_.SetNextRetryTime(t5);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());

  // The cycle succeeded.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  // The time t3 is greater than the GU retry time scheduled at the beginning of
  // the test, but later than the retry time that overwrote it during the
  // pretend 'sync cycle'.
  nudge_tracker_.SetSyncCycleStartTime(t3);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());

  // Finally, the retry established during the sync cycle becomes due.
  nudge_tracker_.SetSyncCycleStartTime(t6);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());
}

// Simulate the case where a sync cycle fails.
TEST_F(NudgeTrackerTest, IsRetryRequired_FailedCycle) {
  const base::TimeTicks t0 = base::TimeTicks::FromInternalValue(12345);
  const base::TimeTicks t1 = t0 + base::Seconds(1);
  const base::TimeTicks t2 = t0 + base::Seconds(2);

  nudge_tracker_.SetNextRetryTime(t0);
  nudge_tracker_.SetSyncCycleStartTime(t1);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // The nudge tracker receives no notifications for a failed sync cycle.
  // Pretend one happened here.
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // Think of this as the retry cycle.
  nudge_tracker_.SetSyncCycleStartTime(t2);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // The second cycle is a success.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());
}

// Simulate a partially failed sync cycle.  The callback to update the GU retry
// was invoked, but the sync cycle did not complete successfully.
TEST_F(NudgeTrackerTest, IsRetryRequired_FailedCycleIncludesUpdate) {
  const base::TimeTicks t0 = base::TimeTicks::FromInternalValue(12345);
  const base::TimeTicks t1 = t0 + base::Seconds(1);
  const base::TimeTicks t3 = t0 + base::Seconds(3);
  const base::TimeTicks t4 = t0 + base::Seconds(4);
  const base::TimeTicks t5 = t0 + base::Seconds(5);
  const base::TimeTicks t6 = t0 + base::Seconds(6);

  nudge_tracker_.SetNextRetryTime(t0);
  nudge_tracker_.SetSyncCycleStartTime(t1);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // The cycle is in progress.  A new GU Retry time is received.
  // The flag is not because this cycle is still in progress.
  nudge_tracker_.SetNextRetryTime(t5);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // The nudge tracker receives no notifications for a failed sync cycle.
  // Pretend the cycle failed here.

  // The next sync cycle starts.  The new GU time has not taken effect by this
  // time, but the NudgeTracker hasn't forgotten that we have not yet serviced
  // the retry from the previous cycle.
  nudge_tracker_.SetSyncCycleStartTime(t3);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());

  // It succeeds.  The retry time is not updated, so it should remain at t5.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  // Another sync cycle.  This one is still before the scheduled retry.  It does
  // not change the scheduled retry time.
  nudge_tracker_.SetSyncCycleStartTime(t4);
  EXPECT_FALSE(nudge_tracker_.IsRetryRequired());
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  // The retry scheduled way back during the first cycle of this test finally
  // becomes due.  Perform a successful sync cycle to service it.
  nudge_tracker_.SetSyncCycleStartTime(t6);
  EXPECT_TRUE(nudge_tracker_.IsRetryRequired());
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
}

// Test the default nudge delays for various types.
TEST_F(NudgeTrackerTest, NudgeDelayTest) {
  // Most data types have a medium delay.
  EXPECT_EQ(nudge_tracker_.RecordLocalChange(TYPED_URLS),
            nudge_tracker_.RecordLocalChange(PASSWORDS));
  EXPECT_EQ(nudge_tracker_.RecordLocalChange(TYPED_URLS),
            nudge_tracker_.RecordLocalChange(EXTENSIONS));

  // Bookmarks and preferences sometimes have automatic changes (not directly
  // caused by a user actions), so they have bigger delays.
  EXPECT_GT(nudge_tracker_.RecordLocalChange(BOOKMARKS),
            nudge_tracker_.RecordLocalChange(TYPED_URLS));
  EXPECT_EQ(nudge_tracker_.RecordLocalChange(BOOKMARKS),
            nudge_tracker_.RecordLocalChange(PREFERENCES));

  // Sessions has an even bigger delay.
  EXPECT_GT(nudge_tracker_.RecordLocalChange(SESSIONS),
            nudge_tracker_.RecordLocalChange(BOOKMARKS));

  // Autofill and UserEvents are "accompany types" that rely on nudges from
  // other types. They have the longest delay of all, which really only acts as
  // a last-resort fallback.
  EXPECT_GT(nudge_tracker_.RecordLocalChange(AUTOFILL),
            nudge_tracker_.RecordLocalChange(SESSIONS));
  EXPECT_GT(nudge_tracker_.RecordLocalChange(AUTOFILL), base::Hours(1));
  EXPECT_EQ(nudge_tracker_.RecordLocalChange(AUTOFILL),
            nudge_tracker_.RecordLocalChange(USER_EVENTS));
}

// Test that custom nudge delays are used over the defaults.
TEST_F(NudgeTrackerTest, CustomDelayTest) {
  // Set some custom delays.
  nudge_tracker_.SetLocalChangeDelayIgnoringMinForTest(BOOKMARKS,
                                                       base::Seconds(10));
  nudge_tracker_.SetLocalChangeDelayIgnoringMinForTest(SESSIONS,
                                                       base::Seconds(2));

  // Only those with custom delays should be affected, not another type.
  EXPECT_NE(nudge_tracker_.RecordLocalChange(BOOKMARKS),
            nudge_tracker_.RecordLocalChange(PREFERENCES));

  EXPECT_EQ(base::Seconds(10), nudge_tracker_.RecordLocalChange(BOOKMARKS));
  EXPECT_EQ(base::Seconds(2), nudge_tracker_.RecordLocalChange(SESSIONS));
}

TEST_F(NudgeTrackerTest, DoNotUpdateDelayIfTooSmall) {
  base::TimeDelta initial_delay = nudge_tracker_.RecordLocalChange(BOOKMARKS);
  // The tracker should enforce a minimum threshold that prevents setting a
  // delay too small.
  nudge_tracker_.UpdateLocalChangeDelay(BOOKMARKS, base::Microseconds(100));
  EXPECT_EQ(initial_delay, nudge_tracker_.RecordLocalChange(BOOKMARKS));
}

class NudgeTrackerAckTrackingTest : public NudgeTrackerTest {
 public:
  NudgeTrackerAckTrackingTest() = default;

  bool IsInvalidationUnacknowledged(int tracking_id) {
    return tracker_.IsUnacked(tracking_id);
  }

  bool IsInvalidationAcknowledged(int tracking_id) {
    return tracker_.IsAcknowledged(tracking_id);
  }

  bool IsInvalidationDropped(int tracking_id) {
    return tracker_.IsDropped(tracking_id);
  }

  int SendInvalidation(ModelType type, int version, const std::string& hint) {
    // Build and register the invalidation.
    std::unique_ptr<TrackableMockInvalidation> inv =
        tracker_.IssueInvalidation(version, hint);
    int id = inv->GetTrackingId();

    // Send it to the NudgeTracker.
    nudge_tracker_.RecordRemoteInvalidation(type, std::move(inv));

    // Return its ID to the test framework for use in assertions.
    return id;
  }

  int SendUnknownVersionInvalidation(ModelType type) {
    // Build and register the invalidation.
    std::unique_ptr<TrackableMockInvalidation> inv =
        tracker_.IssueUnknownVersionInvalidation();
    int id = inv->GetTrackingId();

    // Send it to the NudgeTracker.
    nudge_tracker_.RecordRemoteInvalidation(type, std::move(inv));

    // Return its ID to the test framework for use in assertions.
    return id;
  }

  bool AllInvalidationsAccountedFor() const {
    return tracker_.AllInvalidationsAccountedFor();
  }

 private:
  MockInvalidationTracker tracker_;
};

// Test the acknowledgement of a single invalidation.
TEST_F(NudgeTrackerAckTrackingTest, SimpleAcknowledgement) {
  int inv_id = SendInvalidation(BOOKMARKS, 10, "hint");

  EXPECT_TRUE(IsInvalidationUnacknowledged(inv_id));

  // Invalidations are acknowledged if they were used in
  // GetUpdates proto message. To check the acknowledged invalidation,
  // force invalidation to be used in proto message.
  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(IsInvalidationAcknowledged(inv_id));

  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

// Test the acknowledgement of many invalidations.
TEST_F(NudgeTrackerAckTrackingTest, ManyAcknowledgements) {
  int inv1_id = SendInvalidation(BOOKMARKS, 10, "hint");
  int inv2_id = SendInvalidation(BOOKMARKS, 14, "hint2");
  int inv3_id = SendInvalidation(PREFERENCES, 8, "hint3");

  EXPECT_TRUE(IsInvalidationUnacknowledged(inv1_id));
  EXPECT_TRUE(IsInvalidationUnacknowledged(inv2_id));
  EXPECT_TRUE(IsInvalidationUnacknowledged(inv3_id));

  sync_pb::GetUpdateTriggers bm_gu_trigger;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &bm_gu_trigger);
  sync_pb::GetUpdateTriggers pf_gu_trigger;
  nudge_tracker_.FillProtoMessage(PREFERENCES, &pf_gu_trigger);

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv2_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv3_id));

  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

// Test dropping when the buffer overflows and subsequent drop recovery.
TEST_F(NudgeTrackerAckTrackingTest, OverflowAndRecover) {
  std::vector<int> invalidation_ids;

  int inv10_id = SendInvalidation(BOOKMARKS, 10, "hint");
  for (size_t i = 1; i < kHintBufferSize; ++i) {
    invalidation_ids.push_back(SendInvalidation(BOOKMARKS, i + 10, "hint"));
  }

  for (int id : invalidation_ids)
    EXPECT_TRUE(IsInvalidationUnacknowledged(id));

  // This invalidation, though arriving the most recently, has the oldest
  // version number so it should be dropped first.
  int inv5_id = SendInvalidation(BOOKMARKS, 5, "old_hint");
  EXPECT_TRUE(IsInvalidationDropped(inv5_id));

  // This invalidation has a larger version number, so it will force a
  // previously delivered invalidation to be dropped.
  int inv100_id = SendInvalidation(BOOKMARKS, 100, "new_hint");
  EXPECT_TRUE(IsInvalidationDropped(inv10_id));

  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);

  // This should recover from the drop and bring us back into sync.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  for (int id : invalidation_ids)
    EXPECT_TRUE(IsInvalidationAcknowledged(id));

  EXPECT_TRUE(IsInvalidationAcknowledged(inv100_id));

  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

// Test receipt of an unknown version invalidation from the server.
TEST_F(NudgeTrackerAckTrackingTest, UnknownVersionFromServer_Simple) {
  int inv_id = SendUnknownVersionInvalidation(BOOKMARKS);
  EXPECT_TRUE(IsInvalidationUnacknowledged(inv_id));
  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(IsInvalidationAcknowledged(inv_id));
  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

// Test receipt of multiple unknown version invalidations from the server.
TEST_F(NudgeTrackerAckTrackingTest, UnknownVersionFromServer_Complex) {
  int inv1_id = SendUnknownVersionInvalidation(BOOKMARKS);
  int inv2_id = SendInvalidation(BOOKMARKS, 10, "hint");
  int inv3_id = SendUnknownVersionInvalidation(BOOKMARKS);
  int inv4_id = SendUnknownVersionInvalidation(BOOKMARKS);
  int inv5_id = SendInvalidation(BOOKMARKS, 20, "hint2");

  // These invalidations have been overridden, so they got acked early.
  EXPECT_TRUE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv3_id));

  // These invalidations are still waiting to be used.
  EXPECT_TRUE(IsInvalidationUnacknowledged(inv2_id));
  EXPECT_TRUE(IsInvalidationUnacknowledged(inv4_id));
  EXPECT_TRUE(IsInvalidationUnacknowledged(inv5_id));

  sync_pb::GetUpdateTriggers gu_trigger;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger);

  // Finish the sync cycle and expect all remaining invalidations to be acked.
  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv2_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv3_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv4_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv5_id));

  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

TEST_F(NudgeTrackerAckTrackingTest, AckInvalidationsAddedDuringSyncCycle) {
  // Invalidations that are not used in FillProtoMessage() persist until
  // next RecordSuccessfulSyncCycle().
  int inv1_id = SendInvalidation(BOOKMARKS, 10, "hint");
  int inv2_id = SendInvalidation(BOOKMARKS, 14, "hint2");

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  EXPECT_FALSE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_FALSE(IsInvalidationAcknowledged(inv2_id));

  // Fill proto message with the invalidations inv1_id and inv2_id.
  sync_pb::GetUpdateTriggers gu_trigger_1;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger_1);
  ASSERT_EQ(2, gu_trigger_1.notification_hint_size());

  int inv3_id = SendInvalidation(BOOKMARKS, 100, "hint3");

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  EXPECT_TRUE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_TRUE(IsInvalidationAcknowledged(inv2_id));
  EXPECT_FALSE(IsInvalidationAcknowledged(inv3_id));

  // Be sure that invalidations are not used twice in proto messages.
  // Invalidations are expected to be deleted in RecordSuccessfulSyncCycle
  // after being processed in proto message.
  sync_pb::GetUpdateTriggers gu_trigger_2;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger_2);
  ASSERT_EQ(1, gu_trigger_2.notification_hint_size());

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

// Test invalidations that are used in several proto messages.
TEST_F(NudgeTrackerAckTrackingTest, MultipleGetUpdates) {
  int inv1_id = SendInvalidation(BOOKMARKS, 1, "hint1");
  int inv2_id = SendInvalidation(BOOKMARKS, 2, "hint2");

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());

  EXPECT_FALSE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_FALSE(IsInvalidationAcknowledged(inv2_id));

  sync_pb::GetUpdateTriggers gu_trigger_1;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger_1);
  ASSERT_EQ(2, gu_trigger_1.notification_hint_size());

  int inv3_id = SendInvalidation(BOOKMARKS, 100, "hint3");

  EXPECT_FALSE(IsInvalidationAcknowledged(inv1_id));
  EXPECT_FALSE(IsInvalidationAcknowledged(inv2_id));
  EXPECT_FALSE(IsInvalidationAcknowledged(inv3_id));
  // As they are not acknowledged yet, inv1_id, inv2_id and inv3_id
  // should be included in next proto message.
  sync_pb::GetUpdateTriggers gu_trigger_2;
  nudge_tracker_.FillProtoMessage(BOOKMARKS, &gu_trigger_2);
  ASSERT_EQ(3, gu_trigger_2.notification_hint_size());

  nudge_tracker_.RecordSuccessfulSyncCycle(ModelTypeSet::All());
  EXPECT_TRUE(AllInvalidationsAccountedFor());
}

}  // namespace syncer