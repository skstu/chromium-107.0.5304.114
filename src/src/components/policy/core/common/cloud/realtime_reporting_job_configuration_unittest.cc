// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/cloud/realtime_reporting_job_configuration.h"

#include <set>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/core/common/cloud/cloud_policy_util.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/cloud/dm_auth.h"
#include "components/policy/core/common/cloud/mock_cloud_policy_client.h"
#include "components/policy/core/common/cloud/mock_device_management_service.h"
#include "components/version_info/version_info.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chromeos/system/fake_statistics_provider.h"
#endif

namespace em = enterprise_management;

using testing::_;
using testing::StrictMock;

namespace policy {

std::vector<std::string> ids = {"id1", "id2", "id3"};

constexpr char kAppPackage[] = "appPackage";
constexpr char kEventType[] = "eventType";
constexpr char kAppInstallEvent[] = "androidAppInstallEvent";
constexpr char kEventId[] = "eventId";
constexpr char kStatusCode[] = "status";

constexpr char kDummyToken[] = "dm_token";
constexpr char kPackage[] = "unitTestPackage";

class MockCallbackObserver {
 public:
  MockCallbackObserver() = default;

  MOCK_METHOD4(OnURLLoadComplete,
               void(DeviceManagementService::Job* job,
                    DeviceManagementStatus code,
                    int response_code,
                    absl::optional<base::Value::Dict>));
};

class RealtimeReportingJobConfigurationTest : public testing::Test {
 public:
  RealtimeReportingJobConfigurationTest()
#if BUILDFLAG(IS_CHROMEOS_ASH)
      : client_(&service_),
        fake_serial_number_(&fake_statistics_provider_)
#else
      : client_(&service_)
#endif
  {
  }

  void SetUp() override {
    client_.SetDMToken(kDummyToken);
    configuration_ = std::make_unique<RealtimeReportingJobConfiguration>(
        &client_, service_.configuration()->GetRealtimeReportingServerUrl(),
        /*include_device_info=*/true, /*add_connector_url_params=*/false,
        base::BindOnce(&MockCallbackObserver::OnURLLoadComplete,
                       base::Unretained(&callback_observer_)));
    base::Value::Dict context;
    context.SetByDottedPath("browser.userAgent", "dummyAgent");
    base::Value::List events;
    for (size_t i = 0; i < ids.size(); ++i) {
      base::Value event = CreateEvent(ids[i], i);
      events.Append(std::move(event));
    }

    base::Value::Dict report = RealtimeReportingJobConfiguration::BuildReport(
        std::move(events), std::move(context));
    configuration_->AddReport(std::move(report));
  }

 protected:
  static base::Value CreateEvent(std::string& event_id, int type) {
    base::Value event(base::Value::Type::DICTIONARY);
    event.SetStringKey(kAppPackage, kPackage);
    event.SetIntKey(kEventType, type);
    base::Value wrapper(base::Value::Type::DICTIONARY);
    wrapper.SetKey(kAppInstallEvent, std::move(event));
    wrapper.SetStringKey(kEventId, event_id);
    return wrapper;
  }

  static base::Value::Dict CreateResponse(
      const std::set<std::string>& success_ids,
      const std::set<std::string>& failed_ids,
      const std::set<std::string>& permanent_failed_ids) {
    base::Value::Dict response;
    if (success_ids.size()) {
      base::Value* list =
          response.Set(RealtimeReportingJobConfiguration::kUploadedEventsKey,
                       base::Value(base::Value::Type::LIST));
      for (const auto& id : success_ids) {
        list->Append(id);
      }
    }
    if (failed_ids.size()) {
      base::Value* list =
          response.Set(RealtimeReportingJobConfiguration::kFailedUploadsKey,
                       base::Value(base::Value::Type::LIST));
      for (const auto& id : failed_ids) {
        base::Value failure(base::Value::Type::DICTIONARY);
        failure.SetStringKey(kEventId, id);
        failure.SetIntKey(kStatusCode, 8 /* RESOURCE_EXHAUSTED */);
        list->Append(std::move(failure));
      }
    }
    if (permanent_failed_ids.size()) {
      base::Value* list = response.Set(
          RealtimeReportingJobConfiguration::kPermanentFailedUploadsKey,
          base::Value(base::Value::Type::LIST));
      for (const auto& id : permanent_failed_ids) {
        base::Value failure(base::Value::Type::DICTIONARY);
        failure.SetStringKey(kEventId, id);
        failure.SetIntKey(kStatusCode, 9 /* FAILED_PRECONDITION */);
        list->Append(std::move(failure));
      }
    }

    return response;
  }

  static std::string CreateResponseString(const base::Value::Dict& response) {
    std::string response_string;
    base::JSONWriter::Write(response, &response_string);
    return response_string;
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  StrictMock<MockJobCreationHandler> job_creation_handler_;
  FakeDeviceManagementService service_{&job_creation_handler_};
  MockCloudPolicyClient client_;
  StrictMock<MockCallbackObserver> callback_observer_;
  DeviceManagementService::Job job_;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  chromeos::system::ScopedFakeStatisticsProvider fake_statistics_provider_;
  class ScopedFakeSerialNumber {
   public:
    explicit ScopedFakeSerialNumber(
        chromeos::system::ScopedFakeStatisticsProvider*
            fake_statistics_provider) {
      // The fake serial number must be set before |configuration_| is
      // constructed below.
      fake_statistics_provider->SetMachineStatistic(
          chromeos::system::kSerialNumberKeyForTest, "fake_serial_number");
    }
  };
  ScopedFakeSerialNumber fake_serial_number_;
#endif
  std::unique_ptr<RealtimeReportingJobConfiguration> configuration_;
};

TEST_F(RealtimeReportingJobConfigurationTest, ValidatePayload) {
  absl::optional<base::Value> payload =
      base::JSONReader::Read(configuration_->GetPayload());
  EXPECT_TRUE(payload.has_value());
  EXPECT_EQ(kDummyToken, *payload->FindStringPath(
                             ReportingJobConfigurationBase::
                                 DeviceDictionaryBuilder::GetDMTokenPath()));
  EXPECT_EQ(
      client_.client_id(),
      *payload->FindStringPath(ReportingJobConfigurationBase::
                                   DeviceDictionaryBuilder::GetClientIdPath()));
  EXPECT_EQ(GetOSUsername(),
            *payload->FindStringPath(
                ReportingJobConfigurationBase::BrowserDictionaryBuilder::
                    GetMachineUserPath()));
  EXPECT_EQ(version_info::GetVersionNumber(),
            *payload->FindStringPath(
                ReportingJobConfigurationBase::BrowserDictionaryBuilder::
                    GetChromeVersionPath()));
  EXPECT_EQ(GetOSPlatform(),
            *payload->FindStringPath(
                ReportingJobConfigurationBase::DeviceDictionaryBuilder::
                    GetOSPlatformPath()));
  EXPECT_EQ(GetOSVersion(),
            *payload->FindStringPath(
                ReportingJobConfigurationBase::DeviceDictionaryBuilder::
                    GetOSVersionPath()));
  EXPECT_FALSE(GetDeviceName().empty());
  EXPECT_EQ(GetDeviceName(), *payload->FindStringPath(
                                 ReportingJobConfigurationBase::
                                     DeviceDictionaryBuilder::GetNamePath()));

  base::Value* events =
      payload->FindListKey(RealtimeReportingJobConfiguration::kEventListKey);
  EXPECT_EQ(ids.size(), events->GetListDeprecated().size());
  int i = -1;
  for (const auto& event : events->GetListDeprecated()) {
    auto* id = event.FindStringKey(kEventId);
    EXPECT_EQ(ids[++i], *id);
    auto type = event.FindKey(kAppInstallEvent)->FindIntKey(kEventType);
    EXPECT_EQ(i, *type);
  }
}

TEST_F(RealtimeReportingJobConfigurationTest, OnURLLoadComplete_Success) {
  base::Value::Dict response = CreateResponse({ids[0], ids[1], ids[2]}, {}, {});
  EXPECT_CALL(callback_observer_,
              OnURLLoadComplete(&job_, DM_STATUS_SUCCESS,
                                DeviceManagementService::kSuccess,
                                testing::Eq(testing::ByRef(response))));
  configuration_->OnURLLoadComplete(&job_, net::OK,
                                    DeviceManagementService::kSuccess,
                                    CreateResponseString(response));
}

TEST_F(RealtimeReportingJobConfigurationTest, OnURLLoadComplete_NetError) {
  EXPECT_CALL(callback_observer_,
              OnURLLoadComplete(&job_, DM_STATUS_REQUEST_FAILED, _,
                                testing::Eq(absl::nullopt)));
  configuration_->OnURLLoadComplete(&job_, net::ERR_CONNECTION_RESET,
                                    0 /* ignored */, "");
}

TEST_F(RealtimeReportingJobConfigurationTest,
       OnURLLoadComplete_InvalidRequest) {
  EXPECT_CALL(callback_observer_,
              OnURLLoadComplete(&job_, DM_STATUS_REQUEST_INVALID,
                                DeviceManagementService::kInvalidArgument,
                                testing::Eq(absl::nullopt)));
  configuration_->OnURLLoadComplete(
      &job_, net::OK, DeviceManagementService::kInvalidArgument, "");
}

TEST_F(RealtimeReportingJobConfigurationTest,
       OnURLLoadComplete_InvalidDMToken) {
  EXPECT_CALL(
      callback_observer_,
      OnURLLoadComplete(&job_, DM_STATUS_SERVICE_MANAGEMENT_TOKEN_INVALID,
                        DeviceManagementService::kInvalidAuthCookieOrDMToken,
                        testing::Eq(absl::nullopt)));
  configuration_->OnURLLoadComplete(
      &job_, net::OK, DeviceManagementService::kInvalidAuthCookieOrDMToken, "");
}

TEST_F(RealtimeReportingJobConfigurationTest, OnURLLoadComplete_NotSupported) {
  EXPECT_CALL(
      callback_observer_,
      OnURLLoadComplete(&job_, DM_STATUS_SERVICE_MANAGEMENT_NOT_SUPPORTED,
                        DeviceManagementService::kDeviceManagementNotAllowed,
                        testing::Eq(absl::nullopt)));
  configuration_->OnURLLoadComplete(
      &job_, net::OK, DeviceManagementService::kDeviceManagementNotAllowed, "");
}

TEST_F(RealtimeReportingJobConfigurationTest, OnURLLoadComplete_TempError) {
  EXPECT_CALL(callback_observer_,
              OnURLLoadComplete(&job_, DM_STATUS_TEMPORARY_UNAVAILABLE,
                                DeviceManagementService::kServiceUnavailable,
                                testing::Eq(absl::nullopt)));
  configuration_->OnURLLoadComplete(
      &job_, net::OK, DeviceManagementService::kServiceUnavailable, "");
}

TEST_F(RealtimeReportingJobConfigurationTest, OnURLLoadComplete_UnknownError) {
  EXPECT_CALL(callback_observer_,
              OnURLLoadComplete(&job_, DM_STATUS_HTTP_STATUS_ERROR,
                                DeviceManagementService::kInvalidURL,
                                testing::Eq(absl::nullopt)));
  configuration_->OnURLLoadComplete(&job_, net::OK,
                                    DeviceManagementService::kInvalidURL, "");
}

TEST_F(RealtimeReportingJobConfigurationTest, ShouldRetry_Success) {
  auto response_string =
      CreateResponseString(CreateResponse({ids[0], ids[1], ids[2]}, {}, {}));
  auto should_retry = configuration_->ShouldRetry(
      DeviceManagementService::kSuccess, response_string);
  EXPECT_EQ(DeviceManagementService::Job::NO_RETRY, should_retry);
}

TEST_F(RealtimeReportingJobConfigurationTest, ShouldRetry_PartialFalure) {
  // Batch failures are retried
  auto response_string =
      CreateResponseString(CreateResponse({ids[0], ids[1]}, {ids[2]}, {}));
  auto should_retry = configuration_->ShouldRetry(
      DeviceManagementService::kSuccess, response_string);
  EXPECT_EQ(DeviceManagementService::Job::RETRY_WITH_DELAY, should_retry);
}

TEST_F(RealtimeReportingJobConfigurationTest, ShouldRetry_PermanentFailure) {
  // Permanent failures are not retried.
  auto response_string =
      CreateResponseString(CreateResponse({ids[0], ids[1]}, {}, {ids[2]}));
  auto should_retry = configuration_->ShouldRetry(
      DeviceManagementService::kSuccess, response_string);
  EXPECT_EQ(DeviceManagementService::Job::NO_RETRY, should_retry);
}

TEST_F(RealtimeReportingJobConfigurationTest, OnBeforeRetry_HttpFailure) {
  // No change should be made to the payload in this case.
  auto original_payload = configuration_->GetPayload();
  configuration_->OnBeforeRetry(DeviceManagementService::kServiceUnavailable,
                                "");
  EXPECT_EQ(original_payload, configuration_->GetPayload());
}

TEST_F(RealtimeReportingJobConfigurationTest, OnBeforeRetry_PartialBatch) {
  // Only those events whose ids are in failed_uploads should be in the payload
  // after the OnBeforeRetry call.
  auto response_string =
      CreateResponseString(CreateResponse({ids[0]}, {ids[1]}, {ids[2]}));
  configuration_->OnBeforeRetry(DeviceManagementService::kSuccess,
                                response_string);
  absl::optional<base::Value> payload =
      base::JSONReader::Read(configuration_->GetPayload());
  base::Value* events =
      payload->FindListKey(RealtimeReportingJobConfiguration::kEventListKey);
  EXPECT_EQ(1u, events->GetListDeprecated().size());
  auto& event = events->GetListDeprecated()[0];
  EXPECT_EQ(ids[1], *event.FindStringKey(kEventId));
}

}  // namespace policy
