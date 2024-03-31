// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/update_checker.h"

#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/version.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "components/prefs/testing_pref_service.h"
#include "components/update_client/activity_data_service.h"
#include "components/update_client/component.h"
#include "components/update_client/net/url_loader_post_interceptor.h"
#include "components/update_client/persisted_data.h"
#include "components/update_client/test_activity_data_service.h"
#include "components/update_client/test_configurator.h"
#include "components/update_client/update_engine.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

using std::string;

namespace update_client {

namespace {

base::FilePath test_file(const char* file) {
  base::FilePath path;
  base::PathService::Get(base::DIR_SOURCE_ROOT, &path);
  return path.AppendASCII("components")
      .AppendASCII("test")
      .AppendASCII("data")
      .AppendASCII("update_client")
      .AppendASCII(file);
}

const char kUpdateItemId[] = "jebgalgnebhfojomionfpkfelancnnkf";

}  // namespace

class UpdateCheckerTest : public testing::TestWithParam<bool> {
 public:
  UpdateCheckerTest();

  UpdateCheckerTest(const UpdateCheckerTest&) = delete;
  UpdateCheckerTest& operator=(const UpdateCheckerTest&) = delete;

  ~UpdateCheckerTest() override;

  // Overrides from testing::Test.
  void SetUp() override;
  void TearDown() override;

  void UpdateCheckComplete(
      const absl::optional<ProtocolParser::Results>& results,
      ErrorCategory error_category,
      int error,
      int retry_after_sec);

 protected:
  void Quit();
  void RunThreads();

  std::unique_ptr<Component> MakeComponent() const;
  std::unique_ptr<Component> MakeComponent(const std::string& brand) const;
  std::unique_ptr<Component> MakeComponent(
      const std::string& brand,
      const std::string& install_data_index) const;

  scoped_refptr<TestConfigurator> config_;
  std::unique_ptr<TestActivityDataService> activity_data_service_;
  std::unique_ptr<TestingPrefServiceSimple> pref_;
  std::unique_ptr<PersistedData> metadata_;

  std::unique_ptr<UpdateChecker> update_checker_;

  std::unique_ptr<URLLoaderPostInterceptor> post_interceptor_;

  absl::optional<ProtocolParser::Results> results_;
  ErrorCategory error_category_ = ErrorCategory::kNone;
  int error_ = 0;
  int retry_after_sec_ = 0;

  scoped_refptr<UpdateContext> update_context_;

  bool is_foreground_ = false;

 private:
  scoped_refptr<UpdateContext> MakeMockUpdateContext() const;

  base::test::TaskEnvironment task_environment_;
  base::OnceClosure quit_closure_;
};

// This test is parameterized for |is_foreground|.
INSTANTIATE_TEST_SUITE_P(Parameterized, UpdateCheckerTest, testing::Bool());

UpdateCheckerTest::UpdateCheckerTest()
    : task_environment_(base::test::TaskEnvironment::MainThreadType::IO) {}

UpdateCheckerTest::~UpdateCheckerTest() = default;

void UpdateCheckerTest::SetUp() {
  is_foreground_ = GetParam();

  config_ = base::MakeRefCounted<TestConfigurator>();

  pref_ = std::make_unique<TestingPrefServiceSimple>();
  activity_data_service_ = std::make_unique<TestActivityDataService>();
  PersistedData::RegisterPrefs(pref_->registry());
  metadata_ = std::make_unique<PersistedData>(pref_.get(),
                                              activity_data_service_.get());

  post_interceptor_ = std::make_unique<URLLoaderPostInterceptor>(
      config_->test_url_loader_factory());
  EXPECT_TRUE(post_interceptor_);

  update_checker_ = nullptr;

  error_ = 0;
  retry_after_sec_ = 0;
  update_context_ = MakeMockUpdateContext();
  update_context_->is_foreground = is_foreground_;
  update_context_->components_to_check_for_updates = {kUpdateItemId};
}

void UpdateCheckerTest::TearDown() {
  update_checker_ = nullptr;

  post_interceptor_.reset();

  config_ = nullptr;

  // The PostInterceptor requires the message loop to run to destruct correctly.
  // TODO(sorin): This is fragile and should be fixed.
  task_environment_.RunUntilIdle();
}

void UpdateCheckerTest::RunThreads() {
  base::RunLoop runloop;
  quit_closure_ = runloop.QuitClosure();
  runloop.Run();
}

void UpdateCheckerTest::Quit() {
  if (!quit_closure_.is_null())
    std::move(quit_closure_).Run();
}

void UpdateCheckerTest::UpdateCheckComplete(
    const absl::optional<ProtocolParser::Results>& results,
    ErrorCategory error_category,
    int error,
    int retry_after_sec) {
  results_ = results;
  error_category_ = error_category;
  error_ = error;
  retry_after_sec_ = retry_after_sec;
  Quit();
}

scoped_refptr<UpdateContext> UpdateCheckerTest::MakeMockUpdateContext() const {
  return base::MakeRefCounted<UpdateContext>(
      config_, false, false, std::vector<std::string>(),
      UpdateClient::CrxStateChangeCallback(),
      UpdateEngine::NotifyObserversCallback(), UpdateEngine::Callback(),
      nullptr);
}

std::unique_ptr<Component> UpdateCheckerTest::MakeComponent() const {
  return MakeComponent({});
}

std::unique_ptr<Component> UpdateCheckerTest::MakeComponent(
    const std::string& brand) const {
  return MakeComponent(brand, {});
}

std::unique_ptr<Component> UpdateCheckerTest::MakeComponent(
    const std::string& brand,
    const std::string& install_data_index) const {
  CrxComponent crx_component;
  crx_component.app_id = "jebgalgnebhfojomionfpkfelancnnkf";
  crx_component.brand = brand;
  crx_component.install_data_index = install_data_index;
  crx_component.name = "test_jebg";
  crx_component.pk_hash.assign(jebg_hash, jebg_hash + std::size(jebg_hash));
  crx_component.installer = nullptr;
  crx_component.version = base::Version("0.9");
  crx_component.fingerprint = "fp1";

  auto component = std::make_unique<Component>(*update_context_, kUpdateItemId);
  component->state_ = std::make_unique<Component::StateNew>(component.get());
  component->crx_component_ = crx_component;

  return component;
}

// This test is parameterized for |is_foreground|.
TEST_P(UpdateCheckerTest, UpdateCheckSuccess) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));

  config_->SetIsMachineExternallyManaged(true);
  config_->SetUpdaterStateProvider(base::BindRepeating([](bool /*is_machine*/) {
    return UpdaterStateAttributes{{"name", "Omaha"},
                                  {"ismachine", "1"},
                                  {"autoupdatecheckenabled", "1"},
                                  {"updatepolicy", "1"}};
  }));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] =
      MakeComponent("TEST", "foobar_install_data_index");

  auto& component = update_context_->components[kUpdateItemId];
  component->crx_component_->installer_attributes["ap"] = "some_ap";

  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}, {"testrequest", "1"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(1, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(1, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

  // Check the request.
  const auto root =
      base::JSONReader::Read(post_interceptor_->GetRequestBody(0));
  ASSERT_TRUE(root);
  const auto* request = root->FindKey("request");
  ASSERT_TRUE(request);
  EXPECT_TRUE(request->FindKey("@os"));
  EXPECT_EQ("fake_prodid", request->FindKey("@updater")->GetString());
  EXPECT_EQ("crx3", request->FindKey("acceptformat")->GetString());
  EXPECT_TRUE(request->FindKey("arch"));
  EXPECT_EQ("cr", request->FindKey("dedup")->GetString());
  EXPECT_EQ("params", request->FindKey("extra")->GetString());
  EXPECT_LT(0, request->FindPath({"hw", "physmemory"})->GetInt());
  EXPECT_TRUE(request->FindKey("nacl_arch"));
  EXPECT_EQ("fake_channel_string",
            request->FindKey("prodchannel")->GetString());
  EXPECT_EQ("30.0", request->FindKey("prodversion")->GetString());
  EXPECT_EQ("3.1", request->FindKey("protocol")->GetString());
  EXPECT_TRUE(request->FindKey("requestid"));
  EXPECT_TRUE(request->FindKey("sessionid"));
  EXPECT_EQ("1", request->FindKey("testrequest")->GetString());
  EXPECT_EQ("fake_channel_string",
            request->FindKey("updaterchannel")->GetString());
  EXPECT_EQ("30.0", request->FindKey("updaterversion")->GetString());
  EXPECT_TRUE(request->FindKey("domainjoined")->GetBool());

  // No "dlpref" is sent by default.
  EXPECT_FALSE(request->FindKey("dlpref"));

  EXPECT_TRUE(request->FindPath({"os", "arch"})->is_string());
  EXPECT_EQ("Fake Operating System",
            request->FindPath({"os", "platform"})->GetString());
  EXPECT_TRUE(request->FindPath({"os", "version"})->is_string());

  const auto& app = request->FindKey("app")->GetListDeprecated()[0];
  EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
  EXPECT_EQ("0.9", app.FindKey("version")->GetString());
  EXPECT_EQ("TEST", app.FindKey("brand")->GetString());
  EXPECT_EQ("fake_lang", app.FindKey("lang")->GetString());

  const auto& data = app.FindKey("data")->GetIfList()->front();
  EXPECT_EQ("install", data.FindKey("name")->GetString());
  EXPECT_EQ("foobar_install_data_index", data.FindKey("index")->GetString());
  EXPECT_FALSE(data.FindKey("text"));

  if (is_foreground_)
    EXPECT_EQ("ondemand", app.FindKey("installsource")->GetString());
  EXPECT_EQ("some_ap", app.FindKey("ap")->GetString());
  EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
  EXPECT_TRUE(app.FindKey("updatecheck"));
  EXPECT_TRUE(app.FindKey("ping"));
  EXPECT_EQ(-2, app.FindPath({"ping", "r"})->GetInt());
  EXPECT_EQ("fp1", app.FindPath({"packages", "package"})
                       ->GetListDeprecated()[0]
                       .FindKey("fp")
                       ->GetString());
#if BUILDFLAG(IS_WIN)
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  const auto* updater = request->FindKey("updater");
  EXPECT_TRUE(updater);
  EXPECT_EQ("Omaha", updater->FindKey("name")->GetString());
  EXPECT_TRUE(updater->FindKey("autoupdatecheckenabled")->is_bool());
  EXPECT_TRUE(updater->FindKey("ismachine")->is_bool());
  EXPECT_TRUE(updater->FindKey("updatepolicy")->is_int());
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
#endif  // IS_WIN

  // Check the arguments of the callback after parsing.
  EXPECT_EQ(ErrorCategory::kNone, error_category_);
  EXPECT_EQ(0, error_);
  EXPECT_TRUE(results_);
  EXPECT_EQ(1u, results_->list.size());
  const auto& result = results_->list.front();
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", result.extension_id.c_str());
  EXPECT_EQ("1.0", result.manifest.version);
  EXPECT_EQ("11.0.1.0", result.manifest.browser_min_version);
  EXPECT_EQ(1u, result.manifest.packages.size());
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf.crx",
               result.manifest.packages.front().name.c_str());
  EXPECT_EQ(1u, result.crx_urls.size());
  EXPECT_EQ(GURL("http://localhost/download/"), result.crx_urls.front());
  EXPECT_STREQ("this", result.action_run.c_str());

  // Check the DDOS protection header values.
  const auto extra_request_headers =
      std::get<1>(post_interceptor_->GetRequests()[0]);
  EXPECT_TRUE(extra_request_headers.HasHeader("X-Goog-Update-Interactivity"));
  std::string header;
  extra_request_headers.GetHeader("X-Goog-Update-Interactivity", &header);
  EXPECT_STREQ(is_foreground_ ? "fg" : "bg", header.c_str());
  extra_request_headers.GetHeader("X-Goog-Update-Updater", &header);
  EXPECT_STREQ("fake_prodid-30.0", header.c_str());
  extra_request_headers.GetHeader("X-Goog-Update-AppId", &header);
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", header.c_str());
}

// Tests that an invalid "ap" is not serialized.
TEST_P(UpdateCheckerTest, UpdateCheckInvalidAp) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent("TEST");

  // Make "ap" too long.
  auto& component = update_context_->components[kUpdateItemId];
  component->crx_component_->installer_attributes["ap"] = std::string(257, 'a');

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));

  RunThreads();

  const auto request = post_interceptor_->GetRequestBody(0);
    const auto root = base::JSONReader::Read(request);
    ASSERT_TRUE(root);
    const auto& app =
        root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
    EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
    EXPECT_EQ("0.9", app.FindKey("version")->GetString());
    EXPECT_EQ("TEST", app.FindKey("brand")->GetString());
    EXPECT_FALSE(app.FindKey("data"));
    if (is_foreground_)
      EXPECT_EQ("ondemand", app.FindKey("installsource")->GetString());
    EXPECT_FALSE(app.FindKey("ap"));
    EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
    EXPECT_TRUE(app.FindKey("updatecheck"));
    EXPECT_TRUE(app.FindKey("ping"));
    EXPECT_EQ(-2, app.FindPath({"ping", "r"})->GetInt());
    EXPECT_EQ("fp1", app.FindPath({"packages", "package"})
                         ->GetListDeprecated()[0]
                         .FindKey("fp")
                         ->GetString());
}

TEST_P(UpdateCheckerTest, UpdateCheckSuccessNoBrand) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent("TOOLONG");

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));

  RunThreads();

  const auto request = post_interceptor_->GetRequestBody(0);

    const auto root = base::JSONReader::Read(request);
    ASSERT_TRUE(root);
    const auto& app =
        root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
    EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
    EXPECT_EQ("0.9", app.FindKey("version")->GetString());
    EXPECT_FALSE(app.FindKey("brand"));
    if (is_foreground_)
      EXPECT_EQ("ondemand", app.FindKey("installsource")->GetString());
    EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
    EXPECT_TRUE(app.FindKey("updatecheck"));
    EXPECT_TRUE(app.FindKey("ping"));
    EXPECT_EQ(-2, app.FindPath({"ping", "r"})->GetInt());
    EXPECT_EQ("fp1", app.FindPath({"packages", "package"})
                         ->GetListDeprecated()[0]
                         .FindKey("fp")
                         ->GetString());
}

TEST_P(UpdateCheckerTest, UpdateCheckFallback) {
  config_->SetUpdateCheckUrls(
      {GURL("https://localhost2/update2"), GURL("https://localhost2/update2")});

  // 404 first.
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), net::HTTP_NOT_FOUND));
  // Then OK.
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent("TOOLONG");

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));

  RunThreads();
  EXPECT_EQ(2, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  EXPECT_EQ(2, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();
}

// Simulates a 403 server response error.
TEST_P(UpdateCheckerTest, UpdateCheckError) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), net::HTTP_FORBIDDEN));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(1, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  EXPECT_EQ(1, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

  EXPECT_EQ(ErrorCategory::kUpdateCheck, error_category_);
  EXPECT_EQ(403, error_);
  EXPECT_FALSE(results_);
}

TEST_P(UpdateCheckerTest, UpdateCheckDownloadPreference) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));

  config_->SetDownloadPreference(string("cacheable"));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  // The request must contain dlpref="cacheable".
  const auto request = post_interceptor_->GetRequestBody(0);
  const auto root = base::JSONReader::Read(request);
  ASSERT_TRUE(root);
  EXPECT_EQ("cacheable",
            root->FindKey("request")->FindKey("dlpref")->GetString());
}

// This test is checking that an update check signed with CUP fails, since there
// is currently no entity that can respond with a valid signed response.
// A proper CUP test requires network mocks, which are not available now.
TEST_P(UpdateCheckerTest, UpdateCheckCupError) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));

  config_->SetEnabledCupSigning(true);
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent("TEST");

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));

  RunThreads();

  EXPECT_EQ(1, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(1, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

  // Check the request.
  const auto& request = post_interceptor_->GetRequestBody(0);
  const auto root = base::JSONReader::Read(request);
  ASSERT_TRUE(root);
  const auto& app =
      root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
  EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
  EXPECT_EQ("0.9", app.FindKey("version")->GetString());
  EXPECT_EQ("TEST", app.FindKey("brand")->GetString());
  if (is_foreground_)
    EXPECT_EQ("ondemand", app.FindKey("installsource")->GetString());
  EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
  EXPECT_TRUE(app.FindKey("updatecheck"));
  EXPECT_TRUE(app.FindKey("ping"));
  EXPECT_EQ(-2, app.FindPath({"ping", "r"})->GetInt());
  EXPECT_EQ("fp1", app.FindPath({"packages", "package"})
                       ->GetListDeprecated()[0]
                       .FindKey("fp")
                       ->GetString());

  // Expect an error since the response is not trusted.
  EXPECT_EQ(ErrorCategory::kUpdateCheck, error_category_);
  EXPECT_EQ(-10000, error_);
  EXPECT_FALSE(results_);
}

// Tests that the UpdateCheckers will not make an update check for a
// component that requires encryption when the update check URL is unsecure.
TEST_P(UpdateCheckerTest, UpdateCheckRequiresEncryptionError) {
  config_->SetUpdateCheckUrl(GURL("http:\\foo\bar"));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  auto& component = update_context_->components[kUpdateItemId];
  component->crx_component_->requires_network_encryption = true;

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(ErrorCategory::kUpdateCheck, error_category_);
  EXPECT_EQ(-10002, error_);
  EXPECT_FALSE(component->next_version_.IsValid());
}

// Tests that the PersistedData will get correctly update and reserialize
// the elapsed_days value.
TEST_P(UpdateCheckerTest, UpdateCheckLastRollCall) {
  const char* filename = "updatecheck_reply_4.json";
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), test_file(filename)));
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), test_file(filename)));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  // Do two update-checks.
  activity_data_service_->SetDaysSinceLastRollCall(kUpdateItemId, 5);
  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());
  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(2, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(2, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

    const auto root1 =
        base::JSONReader::Read(post_interceptor_->GetRequestBody(0));
    ASSERT_TRUE(root1);
    const auto& app1 =
        root1->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
    EXPECT_EQ(5, app1.FindPath({"ping", "r"})->GetInt());
    const auto root2 =
        base::JSONReader::Read(post_interceptor_->GetRequestBody(1));
    ASSERT_TRUE(root2);
    const auto& app2 =
        root2->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
    EXPECT_EQ(3383, app2.FindPath({"ping", "rd"})->GetInt());
    EXPECT_TRUE(app2.FindPath({"ping", "ping_freshness"})->is_string());
}

TEST_P(UpdateCheckerTest, UpdateCheckLastActive) {
  const char* filename = "updatecheck_reply_4.json";
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), test_file(filename)));
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), test_file(filename)));
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"), test_file(filename)));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  activity_data_service_->SetActiveBit(kUpdateItemId, true);
  activity_data_service_->SetDaysSinceLastActive(kUpdateItemId, 10);
  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  // The active bit should be reset.
  EXPECT_FALSE(activity_data_service_->GetActiveBit(kUpdateItemId));

  activity_data_service_->SetActiveBit(kUpdateItemId, true);
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());
  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  // The active bit should be reset.
  EXPECT_FALSE(activity_data_service_->GetActiveBit(kUpdateItemId));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());
  update_checker_->CheckForUpdates(
      update_context_, {{"extra", "params"}},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_FALSE(activity_data_service_->GetActiveBit(kUpdateItemId));

  EXPECT_EQ(3, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(3, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

    {
      const auto root =
          base::JSONReader::Read(post_interceptor_->GetRequestBody(0));
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(10, app.FindPath({"ping", "a"})->GetInt());
      EXPECT_EQ(-2, app.FindPath({"ping", "r"})->GetInt());
    }
    {
      const auto root =
          base::JSONReader::Read(post_interceptor_->GetRequestBody(1));
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(3383, app.FindPath({"ping", "ad"})->GetInt());
      EXPECT_EQ(3383, app.FindPath({"ping", "rd"})->GetInt());
      EXPECT_TRUE(app.FindPath({"ping", "ping_freshness"})->is_string());
    }
    {
      const auto root =
          base::JSONReader::Read(post_interceptor_->GetRequestBody(2));
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(3383, app.FindPath({"ping", "rd"})->GetInt());
      EXPECT_TRUE(app.FindPath({"ping", "ping_freshness"})->is_string());
    }
}

TEST_P(UpdateCheckerTest, UpdateCheckInstallSource) {
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  auto& component = update_context_->components[kUpdateItemId];
  auto crx_component = component->crx_component();

  if (is_foreground_) {
    {
      auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
          config_->test_url_loader_factory());
      EXPECT_TRUE(post_interceptor->ExpectRequest(
          std::make_unique<PartialMatch>("updatecheck"),
          test_file("updatecheck_reply_1.json")));
      update_checker_->CheckForUpdates(
          update_context_, {},
          base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                         base::Unretained(this)));
      RunThreads();
      const auto& request = post_interceptor->GetRequestBody(0);
        const auto root = base::JSONReader::Read(request);
        ASSERT_TRUE(root);
        const auto& app =
            root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
        EXPECT_EQ("ondemand", app.FindKey("installsource")->GetString());
        EXPECT_FALSE(app.FindKey("installedby"));
    }
    {
      auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
          config_->test_url_loader_factory());
      EXPECT_TRUE(post_interceptor->ExpectRequest(
          std::make_unique<PartialMatch>("updatecheck"),
          test_file("updatecheck_reply_1.json")));
      crx_component->install_source = "sideload";
      crx_component->install_location = "policy";
      component->set_crx_component(*crx_component);
      update_checker_->CheckForUpdates(
          update_context_, {},
          base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                         base::Unretained(this)));
      RunThreads();
      const auto& request = post_interceptor->GetRequestBody(0);
        const auto root = base::JSONReader::Read(request);
        ASSERT_TRUE(root);
        const auto& app =
            root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
        EXPECT_EQ("sideload", app.FindKey("installsource")->GetString());
        EXPECT_EQ("policy", app.FindKey("installedby")->GetString());
    }
    return;
  }

  DCHECK(!is_foreground_);
  {
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_FALSE(app.FindKey("installsource"));
  }
  {
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    crx_component->install_source = "webstore";
    crx_component->install_location = "external";
    component->set_crx_component(*crx_component);
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ("webstore", app.FindKey("installsource")->GetString());
      EXPECT_EQ("external", app.FindKey("installedby")->GetString());
  }
}

TEST_P(UpdateCheckerTest, ComponentDisabled) {
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  auto& component = update_context_->components[kUpdateItemId];
  auto crx_component = component->crx_component();

  {
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
      EXPECT_FALSE(app.FindKey("disabled"));
  }

  {
    crx_component->disabled_reasons = {};
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
      EXPECT_FALSE(app.FindKey("disabled"));
  }

  {
    crx_component->disabled_reasons = {0};
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(false, app.FindKey("enabled")->GetBool());
      const auto& disabled = app.FindKey("disabled")->GetListDeprecated();
      EXPECT_EQ(1u, disabled.size());
      EXPECT_EQ(0, disabled[0].FindKey("reason")->GetInt());
  }
  {
    crx_component->disabled_reasons = {1};
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(false, app.FindKey("enabled")->GetBool());
      const auto& disabled = app.FindKey("disabled")->GetListDeprecated();
      EXPECT_EQ(1u, disabled.size());
      EXPECT_EQ(1, disabled[0].FindKey("reason")->GetInt());
  }

  {
    crx_component->disabled_reasons = {4, 8, 16};
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(false, app.FindKey("enabled")->GetBool());
      const auto& disabled = app.FindKey("disabled")->GetListDeprecated();
      EXPECT_EQ(3u, disabled.size());
      EXPECT_EQ(4, disabled[0].FindKey("reason")->GetInt());
      EXPECT_EQ(8, disabled[1].FindKey("reason")->GetInt());
      EXPECT_EQ(16, disabled[2].FindKey("reason")->GetInt());
  }

  {
    crx_component->disabled_reasons = {0, 4, 8, 16};
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(false, app.FindKey("enabled")->GetBool());
      const auto& disabled = app.FindKey("disabled")->GetListDeprecated();
      EXPECT_EQ(4u, disabled.size());
      EXPECT_EQ(0, disabled[0].FindKey("reason")->GetInt());
      EXPECT_EQ(4, disabled[1].FindKey("reason")->GetInt());
      EXPECT_EQ(8, disabled[2].FindKey("reason")->GetInt());
      EXPECT_EQ(16, disabled[3].FindKey("reason")->GetInt());
  }
}

TEST_P(UpdateCheckerTest, UpdateCheckUpdateDisabled) {
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  auto& component = update_context_->components[kUpdateItemId];
  auto crx_component = component->crx_component();

  // Ignore this test parameter to keep the test simple.
  update_context_->is_foreground = false;
  {
    // Tests the scenario where:
    //  * the component updates are enabled.
    // Expects the group policy to be ignored and the update check to not
    // include the "updatedisabled" attribute.
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
      EXPECT_EQ("0.9", app.FindKey("version")->GetString());
      EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
      EXPECT_TRUE(app.FindKey("updatecheck")->DictEmpty());
  }
  {
    // Tests the scenario where:
    //  * the component updates are disabled.
    // Expects the update check to include the "updatedisabled" attribute.
    crx_component->updates_enabled = false;
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_1.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
      const auto root = base::JSONReader::Read(request);
      ASSERT_TRUE(root);
      const auto& app =
          root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
      EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
      EXPECT_EQ("0.9", app.FindKey("version")->GetString());
      EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
      EXPECT_TRUE(app.FindPath({"updatecheck", "updatedisabled"})->GetBool());
  }
}

TEST_P(UpdateCheckerTest, SameVersionUpdateAllowed) {
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  auto& component = update_context_->components[kUpdateItemId];
  auto crx_component = component->crx_component();
  EXPECT_FALSE(crx_component->same_version_update_allowed);
  {
    // Tests that `same_version_update_allowed` is not serialized when its
    // value is false.
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_noupdate.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
    const auto root = base::JSONReader::Read(request);
    ASSERT_TRUE(root);
    const auto& app = root->FindPath("request.app")->GetListDeprecated()[0];
    EXPECT_STREQ(kUpdateItemId, app.FindStringPath("appid")->c_str());
    EXPECT_TRUE(app.FindDictKey("updatecheck"));
    EXPECT_FALSE(app.FindPath("updatecheck.sameversionupdate"));
  }
  {
    // Tests that `same_version_update_allowed` is serialized when its
    // value is true.
    crx_component->same_version_update_allowed = true;
    component->set_crx_component(*crx_component);
    auto post_interceptor = std::make_unique<URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    EXPECT_TRUE(post_interceptor->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_noupdate.json")));
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();
    const auto& request = post_interceptor->GetRequestBody(0);
    const auto root = base::JSONReader::Read(request);
    ASSERT_TRUE(root);
    const auto& app = root->FindPath("request.app")->GetListDeprecated()[0];
    EXPECT_STREQ(kUpdateItemId, app.FindStringPath("appid")->c_str());
    EXPECT_EQ(app.FindBoolPath("updatecheck.sameversionupdate").value(), true);
  }
}

TEST_P(UpdateCheckerTest, NoUpdateActionRun) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_noupdate.json")));
  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(1, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(1, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

  // Check the arguments of the callback after parsing.
  EXPECT_EQ(ErrorCategory::kNone, error_category_);
  EXPECT_EQ(0, error_);
  EXPECT_TRUE(results_);
  EXPECT_EQ(1u, results_->list.size());
  const auto& result = results_->list.front();
  EXPECT_STREQ("jebgalgnebhfojomionfpkfelancnnkf", result.extension_id.c_str());
  EXPECT_STREQ("noupdate", result.status.c_str());
  EXPECT_STREQ("this", result.action_run.c_str());
}

TEST_P(UpdateCheckerTest, UpdatePauseResume) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_noupdate.json")));
  post_interceptor_->url_job_request_ready_callback(base::BindOnce(
      [](URLLoaderPostInterceptor* post_interceptor) {
        post_interceptor->Resume();
      },
      post_interceptor_.get()));
  post_interceptor_->Pause();

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent("TEST");

  // Ignore this test parameter to keep the test simple.
  update_context_->is_foreground = false;

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  const auto& request = post_interceptor_->GetRequestBody(0);
    const auto root = base::JSONReader::Read(request);
    ASSERT_TRUE(root);
    const auto& app =
        root->FindKey("request")->FindKey("app")->GetListDeprecated()[0];
    EXPECT_EQ(kUpdateItemId, app.FindKey("appid")->GetString());
    EXPECT_EQ("0.9", app.FindKey("version")->GetString());
    EXPECT_EQ("TEST", app.FindKey("brand")->GetString());
    EXPECT_EQ(true, app.FindKey("enabled")->GetBool());
    EXPECT_TRUE(app.FindKey("updatecheck")->DictEmpty());
    EXPECT_EQ(-2, app.FindPath({"ping", "r"})->GetInt());
    EXPECT_EQ("fp1", app.FindKey("packages")
                         ->FindKey("package")
                         ->GetListDeprecated()[0]
                         .FindKey("fp")
                         ->GetString());
}

// Tests that an update checker object and its underlying SimpleURLLoader can
// be safely destroyed while it is paused.
TEST_P(UpdateCheckerTest, UpdateResetUpdateChecker) {
  base::RunLoop runloop;
  auto quit_closure = runloop.QuitClosure();

  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_1.json")));
  post_interceptor_->url_job_request_ready_callback(base::BindOnce(
      [](base::OnceClosure quit_closure) { std::move(quit_closure).Run(); },
      std::move(quit_closure)));
  post_interceptor_->Pause();

  update_context_->components[kUpdateItemId] = MakeComponent();

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());
  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  runloop.Run();
}

// The update response contains a protocol version which does not match the
// expected protocol version.
TEST_P(UpdateCheckerTest, ParseErrorProtocolVersionMismatch) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_parse_error.json")));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(1, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(1, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

  EXPECT_EQ(ErrorCategory::kUpdateCheck, error_category_);
  EXPECT_EQ(-10003, error_);
  EXPECT_FALSE(results_);
}

// The update response contains a status |error-unknownApplication| for the
// app. The response is succesfully parsed and a result is extracted to
// indicate this status.
TEST_P(UpdateCheckerTest, ParseErrorAppStatusErrorUnknownApplication) {
  EXPECT_TRUE(post_interceptor_->ExpectRequest(
      std::make_unique<PartialMatch>("updatecheck"),
      test_file("updatecheck_reply_unknownapp.json")));

  update_checker_ = UpdateChecker::Create(config_, metadata_.get());

  update_context_->components[kUpdateItemId] = MakeComponent();

  update_checker_->CheckForUpdates(
      update_context_, {},
      base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                     base::Unretained(this)));
  RunThreads();

  EXPECT_EQ(1, post_interceptor_->GetHitCount())
      << post_interceptor_->GetRequestsAsString();
  ASSERT_EQ(1, post_interceptor_->GetCount())
      << post_interceptor_->GetRequestsAsString();

  EXPECT_EQ(ErrorCategory::kNone, error_category_);
  EXPECT_EQ(0, error_);
  EXPECT_TRUE(results_);
  EXPECT_EQ(1u, results_->list.size());
  const auto& result = results_->list.front();
  EXPECT_STREQ("error-unknownApplication", result.status.c_str());
}

TEST_P(UpdateCheckerTest, DomainJoined) {
  for (const auto is_managed : std::initializer_list<absl::optional<bool>>{
           absl::nullopt, false, true}) {
    EXPECT_TRUE(post_interceptor_->ExpectRequest(
        std::make_unique<PartialMatch>("updatecheck"),
        test_file("updatecheck_reply_noupdate.json")));
    update_checker_ = UpdateChecker::Create(config_, metadata_.get());

    update_context_->components[kUpdateItemId] = MakeComponent();

    config_->SetIsMachineExternallyManaged(is_managed);
    update_checker_->CheckForUpdates(
        update_context_, {},
        base::BindOnce(&UpdateCheckerTest::UpdateCheckComplete,
                       base::Unretained(this)));
    RunThreads();

    ASSERT_EQ(post_interceptor_->GetCount(), 1);
    const auto root =
        base::JSONReader::Read(post_interceptor_->GetRequestBody(0));
    post_interceptor_->Reset();

    // What is injected in the update checker by the configurator must
    // match what is sent in the update check.
    ASSERT_TRUE(root);
    EXPECT_EQ(is_managed, root->FindBoolPath("request.domainjoined"));
  }
}

}  // namespace update_client
