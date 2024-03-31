// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/policy_loader_ios.h"

#import <UIKit/UIKit.h>

#include <memory>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/json/json_string_value_serializer.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/test_simple_task_runner.h"
#include "base/values.h"
#include "components/policy/core/common/async_policy_provider.h"
#include "components/policy/core/common/configuration_policy_provider_test.h"
#include "components/policy/core/common/policy_bundle.h"
#import "components/policy/core/common/policy_loader_ios_constants.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_test_utils.h"
#include "components/policy/core/common/policy_types.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace policy {

namespace {

class TestHarness : public PolicyProviderTestHarness {
 public:
  TestHarness(bool encode_complex_data_as_json);
  TestHarness(const TestHarness&) = delete;
  TestHarness& operator=(const TestHarness&) = delete;
  ~TestHarness() override;

  void SetUp() override;

  ConfigurationPolicyProvider* CreateProvider(
      SchemaRegistry* registry,
      scoped_refptr<base::SequencedTaskRunner> task_runner) override;

  void InstallEmptyPolicy() override;
  void InstallStringPolicy(const std::string& policy_name,
                           const std::string& policy_value) override;
  void InstallIntegerPolicy(const std::string& policy_name,
                            int policy_value) override;
  void InstallBooleanPolicy(const std::string& policy_name,
                            bool policy_value) override;
  void InstallStringListPolicy(const std::string& policy_name,
                               const base::ListValue* policy_value) override;
  void InstallDictionaryPolicy(const std::string& policy_name,
                               const base::Value::Dict& policy_value) override;

  static PolicyProviderTestHarness* Create();
  static PolicyProviderTestHarness* CreateWithJSONEncoding();

 private:
  // Merges the policies in |policy| into the current policy dictionary
  // in NSUserDefaults, after making sure that the policy dictionary
  // exists.
  void AddPolicies(NSDictionary* policy);

  // If true, the test harness will encode complex data (dicts and lists) as
  // JSON strings.
  bool encode_complex_data_as_json_;
};

TestHarness::TestHarness(bool encode_complex_data_as_json)
    : PolicyProviderTestHarness(POLICY_LEVEL_MANDATORY,
                                POLICY_SCOPE_MACHINE,
                                POLICY_SOURCE_PLATFORM),
      encode_complex_data_as_json_(encode_complex_data_as_json) {}

TestHarness::~TestHarness() {
  // Cleanup any policies left from the test.
  [[NSUserDefaults standardUserDefaults]
      removeObjectForKey:kPolicyLoaderIOSConfigurationKey];
}

void TestHarness::SetUp() {
  // Make sure there is no pre-existing policy present.
  [[NSUserDefaults standardUserDefaults]
      removeObjectForKey:kPolicyLoaderIOSConfigurationKey];
}

ConfigurationPolicyProvider* TestHarness::CreateProvider(
    SchemaRegistry* registry,
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  return new AsyncPolicyProvider(
      registry, std::make_unique<PolicyLoaderIOS>(registry, task_runner));
}

void TestHarness::InstallEmptyPolicy() {
  AddPolicies(@{});
}

void TestHarness::InstallStringPolicy(const std::string& policy_name,
                                      const std::string& policy_value) {
  NSString* key = base::SysUTF8ToNSString(policy_name);
  NSString* value = base::SysUTF8ToNSString(policy_value);
  AddPolicies(@{
      key: value
  });
}

void TestHarness::InstallIntegerPolicy(const std::string& policy_name,
                                       int policy_value) {
  NSString* key = base::SysUTF8ToNSString(policy_name);
  AddPolicies(@{
      key: [NSNumber numberWithInt:policy_value]
  });
}

void TestHarness::InstallBooleanPolicy(const std::string& policy_name,
                                       bool policy_value) {
  NSString* key = base::SysUTF8ToNSString(policy_name);
  AddPolicies(@{
      key: [NSNumber numberWithBool:policy_value]
  });
}

void TestHarness::InstallStringListPolicy(const std::string& policy_name,
                                          const base::ListValue* policy_value) {
  NSString* key = base::SysUTF8ToNSString(policy_name);
  base::ScopedCFTypeRef<CFPropertyListRef> value(
      ValueToProperty(*policy_value));

  if (encode_complex_data_as_json_) {
    // Convert |policy_value| to a JSON-encoded string.
    std::string json_string;
    JSONStringValueSerializer serializer(&json_string);
    ASSERT_TRUE(serializer.Serialize(*policy_value));

    AddPolicies(@{key : base::SysUTF8ToNSString(json_string)});
  } else {
    AddPolicies(@{key : (__bridge NSArray*)(value.get())});
  }
}

void TestHarness::InstallDictionaryPolicy(
    const std::string& policy_name,
    const base::Value::Dict& policy_value) {
  NSString* key = base::SysUTF8ToNSString(policy_name);

  if (encode_complex_data_as_json_) {
    // Convert |policy_value| to a JSON-encoded string.
    std::string json_string;
    JSONStringValueSerializer serializer(&json_string);
    ASSERT_TRUE(serializer.Serialize(policy_value));

    AddPolicies(@{key : base::SysUTF8ToNSString(json_string)});
  } else {
    base::ScopedCFTypeRef<CFPropertyListRef> value(
        ValueToProperty(base::Value(policy_value.Clone())));
    AddPolicies(@{key : (__bridge NSDictionary*)(value.get())});
  }
}

// static
PolicyProviderTestHarness* TestHarness::Create() {
  return new TestHarness(false);
}

// static
PolicyProviderTestHarness* TestHarness::CreateWithJSONEncoding() {
  return new TestHarness(true);
}

void TestHarness::AddPolicies(NSDictionary* policy) {
  NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
  NSMutableDictionary* chromePolicy = [[NSMutableDictionary alloc] init];

  NSDictionary* previous =
      [defaults dictionaryForKey:kPolicyLoaderIOSConfigurationKey];
  if (previous) {
    [chromePolicy addEntriesFromDictionary:previous];
  }

  [chromePolicy addEntriesFromDictionary:policy];
  [[NSUserDefaults standardUserDefaults]
      setObject:chromePolicy
         forKey:kPolicyLoaderIOSConfigurationKey];
}

}  // namespace

INSTANTIATE_TEST_SUITE_P(PolicyProviderIOSChromePolicyTest,
                         ConfigurationPolicyProviderTest,
                         testing::Values(TestHarness::Create));

INSTANTIATE_TEST_SUITE_P(PolicyProviderIOSChromePolicyJSONTest,
                         ConfigurationPolicyProviderTest,
                         testing::Values(TestHarness::CreateWithJSONEncoding));

}  // namespace policy
