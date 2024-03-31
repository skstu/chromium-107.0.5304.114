// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_manager_metrics_util.h"

#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "components/ukm/test_ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_source.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace password_manager::metrics_util {

namespace {

constexpr ukm::SourceId kTestSourceId = 0x1234;

using UkmEntry = ukm::builders::PasswordManager_LeakWarningDialog;

// Create a LeakDialogMetricsRecorder for a test source id.
// Tests in this unit test are somewhat perfunctory due to the limited
// functionality of the class. On top of this, the unit tests for
// CredentialLeakDialogControllerImpl also test metrics recording.
LeakDialogMetricsRecorder CreateMetricsRecorder(LeakDialogType dialog_type) {
  return LeakDialogMetricsRecorder(kTestSourceId, dialog_type);
}

}  // namespace

TEST(PasswordManagerMetricsUtilLeakDialogMetricsRecorder,
     AutomaticPasswordChangeClicked) {
  base::test::TaskEnvironment task_environment_;
  base::HistogramTester histogram_tester;
  ukm::TestAutoSetUkmRecorder test_ukm_recorder;

  LeakDialogMetricsRecorder recorder(
      CreateMetricsRecorder(LeakDialogType::kChangeAutomatically));
  recorder.SetSamplingRateForTesting(1.0);
  recorder.LogLeakDialogTypeAndDismissalReason(
      LeakDialogDismissalReason::kClickedChangePasswordAutomatically);

  // Check that UMA logging is correct.
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.LeakDetection.DialogDismissalReason",
      LeakDialogDismissalReason::kClickedChangePasswordAutomatically, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.LeakDetection.DialogDismissalReason.ChangeAutomatically",
      LeakDialogDismissalReason::kClickedChangePasswordAutomatically, 1);

  // Check that UKM logging is correct.
  const auto& entries =
      test_ukm_recorder.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(1u, entries.size());
  for (const auto* entry : entries) {
    EXPECT_EQ(kTestSourceId, entry->source_id);
    test_ukm_recorder.ExpectEntryMetric(
        entry, UkmEntry::kPasswordLeakDetectionDialogTypeName,
        static_cast<int64_t>(LeakDialogType::kChangeAutomatically));
    test_ukm_recorder.ExpectEntryMetric(
        entry, UkmEntry::kPasswordLeakDetectionDialogDismissalReasonName,
        static_cast<int64_t>(
            LeakDialogDismissalReason::kClickedChangePasswordAutomatically));
  }
}

TEST(PasswordManagerMetricsUtilLeakDialogMetricsRecorder, CheckupIgnored) {
  base::test::TaskEnvironment task_environment_;
  base::HistogramTester histogram_tester;
  ukm::TestAutoSetUkmRecorder test_ukm_recorder;

  LeakDialogMetricsRecorder recorder(
      CreateMetricsRecorder(LeakDialogType::kCheckup));
  recorder.SetSamplingRateForTesting(1.0);
  recorder.LogLeakDialogTypeAndDismissalReason(
      LeakDialogDismissalReason::kNoDirectInteraction);

  // Check that UMA logging is correct.
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.LeakDetection.DialogDismissalReason",
      LeakDialogDismissalReason::kNoDirectInteraction, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.LeakDetection.DialogDismissalReason.Checkup",
      LeakDialogDismissalReason::kNoDirectInteraction, 1);

  // Check that UKM logging is correct.
  const auto& entries =
      test_ukm_recorder.GetEntriesByName(UkmEntry::kEntryName);
  EXPECT_EQ(1u, entries.size());
  for (const auto* entry : entries) {
    EXPECT_EQ(kTestSourceId, entry->source_id);
    test_ukm_recorder.ExpectEntryMetric(
        entry, UkmEntry::kPasswordLeakDetectionDialogTypeName,
        static_cast<int64_t>(LeakDialogType::kCheckup));
    test_ukm_recorder.ExpectEntryMetric(
        entry, UkmEntry::kPasswordLeakDetectionDialogDismissalReasonName,
        static_cast<int64_t>(LeakDialogDismissalReason::kNoDirectInteraction));
  }
}

TEST(PasswordManagerMetricsUtil, LogNewlySavedPasswordMetrics) {
  base::HistogramTester histogram_tester;

  constexpr bool kIsGeneratedPassword = true;
  constexpr bool kIsUsernameEmpty = true;
  LogNewlySavedPasswordMetrics(
      /*is_generated_password=*/true, /*is_username_empty=*/true,
      PasswordAccountStorageUsageLevel::kNotUsingAccountStorage);

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.NewlySavedPasswordIsGenerated", kIsGeneratedPassword, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.NewlySavedPasswordIsGenerated.NotUsingAccountStorage",
      kIsGeneratedPassword, 1);
  histogram_tester.ExpectTotalCount(
      "PasswordManager.NewlySavedPasswordIsGenerated.UsingAccountStorage", 0);
  histogram_tester.ExpectTotalCount(
      "PasswordManager.NewlySavedPasswordIsGenerated.Syncing", 0);

  histogram_tester.ExpectUniqueSample(
      "PasswordManager.NewlySavedPasswordHasEmptyUsername.Overall",
      kIsUsernameEmpty, 1);
  histogram_tester.ExpectUniqueSample(
      "PasswordManager.NewlySavedPasswordHasEmptyUsername.AutoGenerated",
      kIsUsernameEmpty, 1);
  histogram_tester.ExpectTotalCount(
      "PasswordManager.NewlySavedPasswordHasEmptyUsername.UserCreated", 0);
}

}  // namespace password_manager::metrics_util
