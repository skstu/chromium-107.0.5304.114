// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/breadcrumbs/core/application_breadcrumbs_logger.h"

#include "base/files/scoped_temp_dir.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/metrics/user_metrics_action.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "components/breadcrumbs/core/breadcrumb_manager.h"
#include "components/breadcrumbs/core/breadcrumb_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace breadcrumbs {

namespace {
// The particular UserActions used here are not important, but real UserAction
// names are used to prevent a presubmit warning.
const char kUserAction1Name[] = "MobileMenuNewTab";
const char kUserAction2Name[] = "OverscrollActionCloseTab";
// An "InProductHelp.*" user action.
const char kInProductHelpUserActionName[] = "InProductHelp.Dismissed";
}  // namespace

// Test fixture for testing ApplicationBreadcrumbsLogger class.
class ApplicationBreadcrumbsLoggerTest : public PlatformTest {
 protected:
  ApplicationBreadcrumbsLoggerTest() {
    base::SetRecordActionTaskRunner(
        task_environment_.GetMainThreadTaskRunner());
    CHECK(temp_dir_.CreateUniqueTempDir());
    logger_ = std::make_unique<ApplicationBreadcrumbsLogger>(
        temp_dir_.GetPath(),
        /*is_metrics_enabled_callback=*/base::BindRepeating(
            [] { return true; }));
  }

  // This must be created before `task_environment_`, to ensure that any tasks
  // that depend on the directory existing (e.g., those posted by `logger_`)
  // have finished.
  base::ScopedTempDir temp_dir_;

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<ApplicationBreadcrumbsLogger> logger_;
};

// Tests that a recorded UserAction is logged by the
// ApplicationBreadcrumbsLogger.
TEST_F(ApplicationBreadcrumbsLoggerTest, UserAction) {
  ASSERT_EQ(1U, logger_->GetEventsForTesting().size());  // startup event

  base::RecordAction(base::UserMetricsAction(kUserAction1Name));
  base::RecordAction(base::UserMetricsAction(kUserAction2Name));

  std::list<std::string> events = logger_->GetEventsForTesting();
  ASSERT_EQ(3ul, events.size());
  events.pop_front();
  EXPECT_NE(std::string::npos, events.front().find(kUserAction1Name));
  events.pop_front();
  EXPECT_NE(std::string::npos, events.front().find(kUserAction2Name));
}

// Tests that not_user_triggered User Action does not show up in breadcrumbs.
TEST_F(ApplicationBreadcrumbsLoggerTest, LogNotUserTriggeredAction) {
  ASSERT_EQ(1U, logger_->GetEventsForTesting().size());  // startup event

  base::RecordAction(base::UserMetricsAction("ActiveTabChanged"));

  EXPECT_EQ(1U, logger_->GetEventsForTesting().size());
}

// Tests that "InProductHelp" UserActions are not logged by
// ApplicationBreadcrumbsLogger as they are very noisy.
TEST_F(ApplicationBreadcrumbsLoggerTest, SkipInProductHelpUserActions) {
  ASSERT_EQ(1U, logger_->GetEventsForTesting().size());  // startup event

  base::RecordAction(base::UserMetricsAction(kInProductHelpUserActionName));

  const std::list<std::string>& events = logger_->GetEventsForTesting();
  ASSERT_EQ(1ul, events.size());
}

// Tests that memory pressure events are logged by ApplicationBreadcrumbsLogger.
// Test is flaky (https://crbug.com/1305253)
TEST_F(ApplicationBreadcrumbsLoggerTest, MemoryPressure) {
  ASSERT_EQ(1U, logger_->GetEventsForTesting().size());  // startup event

  base::MemoryPressureListener::SimulatePressureNotification(
      base::MemoryPressureListener::MemoryPressureLevel::
          MEMORY_PRESSURE_LEVEL_MODERATE);
  base::MemoryPressureListener::SimulatePressureNotification(
      base::MemoryPressureListener::MemoryPressureLevel::
          MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  std::list<std::string> events = logger_->GetEventsForTesting();
  ASSERT_EQ(3ul, events.size());
  // Pop startup.
  events.pop_front();
  EXPECT_NE(std::string::npos, events.front().find("Moderate"));
  // Ensure UserAction events are labeled as such.
  EXPECT_NE(std::string::npos, events.front().find("Memory Pressure: "));
  events.pop_front();
  EXPECT_NE(std::string::npos, events.front().find("Critical"));
}

}  // namespace breadcrumbs
