// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cast_certificate/cast_cert_validator.h"

#include <memory>

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/cast_certificate/cast_cert_reader.h"
#include "components/cast_certificate/cast_cert_test_helpers.h"
#include "net/cert/pki/cert_errors.h"
#include "net/cert/pki/parsed_certificate.h"
#include "net/cert/pki/signature_algorithm.h"
#include "net/cert/pki/trust_store_in_memory.h"
#include "net/cert/x509_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cast_certificate {

namespace {

// Creates an std::string given a uint8_t array.
template <size_t N>
std::string CreateString(const uint8_t (&data)[N]) {
  return std::string(reinterpret_cast<const char*>(data), N);
}

enum TrustStoreDependency {
  // Uses the built-in trust store for Cast. This is how certificates are
  // verified in production.
  TRUST_STORE_BUILTIN,

  // Instead of using the built-in trust store, use root certificate in the
  // provided test chain as the trust anchor.
  //
  // This trust anchor is initialized with anchor constraints, similar to how
  // TrustAnchors in the built-in store are setup.
  TRUST_STORE_FROM_TEST_FILE,

  // This is the same as TRUST_STORE_FROM_TEST_FILE except the TrustAnchor is
  // setup to NOT enforce anchor constraints. This mode is useful for
  // verifying control groups. It is not how code works in production.
  TRUST_STORE_FROM_TEST_FILE_UNCONSTRAINED,
};

// Reads a test chain from |certs_file_name|, and asserts that verifying it as
// a Cast device certificate yields |expected_result|.
//
// RunTest() also checks that the resulting CertVerificationContext does not
// incorrectly verify invalid signatures.
//
//  * |expected_policy| - The policy that should have been identified for the
//                        device certificate.
//  * |time| - The timestamp to use when verifying the certificate.
//  * |trust_store_dependency| - Which trust store to use when verifying (see
//                               enum's definition).
//  * |optional_signed_data_file_name| - optional path to a PEM file containing
//        a valid signature generated by the device certificate.
//
void RunTest(CastCertError expected_result,
             const std::string& expected_common_name,
             CastDeviceCertPolicy expected_policy,
             const std::string& certs_file_name,
             const base::Time& time,
             TrustStoreDependency trust_store_dependency,
             const std::string& optional_signed_data_file_name) {
  base::test::TaskEnvironment te;
  auto certs = ReadCertificateChainFromFile(
      testing::GetCastCertificatesSubDirectory().AppendASCII(certs_file_name));

  std::unique_ptr<net::TrustStoreInMemory> trust_store;

  switch (trust_store_dependency) {
    case TRUST_STORE_BUILTIN:
      // Leave trust_store as nullptr.
      break;

    case TRUST_STORE_FROM_TEST_FILE:
    case TRUST_STORE_FROM_TEST_FILE_UNCONSTRAINED: {
      ASSERT_FALSE(certs.empty());

      // Parse the root certificate of the chain.
      net::CertErrors errors;
      scoped_refptr<net::ParsedCertificate> root =
          net::ParsedCertificate::Create(
              net::x509_util::CreateCryptoBuffer(certs.back()), {}, &errors);
      ASSERT_TRUE(root) << errors.ToDebugString();

      // Remove it from the chain.
      certs.pop_back();

      // Add it to the trust store as a trust anchor
      trust_store = std::make_unique<net::TrustStoreInMemory>();

      if (trust_store_dependency == TRUST_STORE_FROM_TEST_FILE_UNCONSTRAINED) {
        // This is a test-only mode where anchor constraints are not enforced.
        trust_store->AddTrustAnchor(std::move(root));
      } else {
        // Add a trust anchor and enforce constraints on it (regular mode for
        // built-in Cast roots).
        trust_store->AddTrustAnchorWithConstraints(std::move(root));
      }
    }
  }

  std::unique_ptr<CertVerificationContext> context;
  CastDeviceCertPolicy policy;

  CastCertError result = VerifyDeviceCertUsingCustomTrustStore(
      certs, time, &context, &policy, nullptr, CRLPolicy::CRL_OPTIONAL,
      trust_store.get());

  ASSERT_EQ(expected_result, result);
  if (expected_result != CastCertError::OK)
    return;

  EXPECT_EQ(expected_policy, policy);
  ASSERT_TRUE(context.get());

  // Test that the context is good.
  EXPECT_EQ(expected_common_name, context->GetCommonName());

  ASSERT_TRUE(context);

  // Test verification of some invalid signatures.
  EXPECT_FALSE(context->VerifySignatureOverData("bogus signature", "bogus data",
                                                CastDigestAlgorithm::SHA256));
  EXPECT_FALSE(context->VerifySignatureOverData("", "bogus data",
                                                CastDigestAlgorithm::SHA256));
  EXPECT_FALSE(
      context->VerifySignatureOverData("", "", CastDigestAlgorithm::SHA256));

  // If valid signatures are known for this device certificate, test them.
  if (!optional_signed_data_file_name.empty()) {
    auto signature_data = cast_certificate::testing::ReadSignatureTestData(
        optional_signed_data_file_name);

    // Test verification of a valid SHA1 signature.
    EXPECT_TRUE(context->VerifySignatureOverData(signature_data.signature_sha1,
                                                 signature_data.message,
                                                 CastDigestAlgorithm::SHA1));

    // Test verification of a valid SHA256 signature.
    EXPECT_TRUE(context->VerifySignatureOverData(
        signature_data.signature_sha256, signature_data.message,
        CastDigestAlgorithm::SHA256));
  }
}

// Creates a time in UTC at midnight.
//
// The maximum date usable here is limited to year 2038 on 32 bit systems due to
// base::Time::FromExploded clamping the range to what is supported by mktime
// and timegm.
base::Time CreateDate(int year, int month, int day) {
  base::Time::Exploded time = {0};
  time.year = year;
  time.month = month;
  time.day_of_month = day;
  base::Time result;
  EXPECT_TRUE(base::Time::FromUTCExploded(time, &result));
  return result;
}

// Returns 2016-04-01 00:00:00 UTC.
//
// This is a time when most of the test certificate paths are
// valid.
base::Time AprilFirst2016() {
  return CreateDate(2016, 4, 1);
}

// Returns 2015-01-01 00:00:00 UTC.
base::Time JanuaryFirst2015() {
  return CreateDate(2015, 1, 1);
}

// Returns 2037-03-01 00:00:00 UTC.
//
// This is so far in the future that the test chains in this unit-test
// should all be invalid.
base::Time MarchFirst2037() {
  return CreateDate(2037, 3, 1);
}

// Tests verifying a valid certificate chain of length 2:
//
//   0: 2ZZBG9 FA8FCA3EF91A
//   1: Eureka Gen1 ICA
//
// Chains to trust anchor:
//   Eureka Root CA    (built-in trust store)
TEST(VerifyCastDeviceCertTest, ChromecastGen1) {
  RunTest(CastCertError::OK, "2ZZBG9 FA8FCA3EF91A", CastDeviceCertPolicy::NONE,
          "chromecast_gen1.pem", AprilFirst2016(), TRUST_STORE_BUILTIN,
          "signeddata/2ZZBG9_FA8FCA3EF91A.pem");
}

// Tests verifying a valid certificate chain of length 2:
//
//  0: 2ZZBG9 FA8FCA3EF91A
//  1: Eureka Gen1 ICA
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
TEST(VerifyCastDeviceCertTest, ChromecastGen1Reissue) {
  RunTest(CastCertError::OK, "2ZZBG9 FA8FCA3EF91A", CastDeviceCertPolicy::NONE,
          "chromecast_gen1_reissue.pem", AprilFirst2016(), TRUST_STORE_BUILTIN,
          "signeddata/2ZZBG9_FA8FCA3EF91A.pem");
}

// Tests verifying a valid certificate chain of length 2:
//
//   0: 3ZZAK6 FA8FCA3F0D35
//   1: Chromecast ICA 3
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
TEST(VerifyCastDeviceCertTest, ChromecastGen2) {
  RunTest(CastCertError::OK, "3ZZAK6 FA8FCA3F0D35", CastDeviceCertPolicy::NONE,
          "chromecast_gen2.pem", AprilFirst2016(), TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 3:
//
//   0: -6394818897508095075
//   1: Asus fugu Cast ICA
//   2: Widevine Cast Subroot
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
TEST(VerifyCastDeviceCertTest, Fugu) {
  RunTest(CastCertError::OK, "-6394818897508095075", CastDeviceCertPolicy::NONE,
          "fugu.pem", AprilFirst2016(), TRUST_STORE_BUILTIN, "");
}

// Tests verifying an invalid certificate chain of length 1:
//
//  0: Cast Test Untrusted Device
//
// Chains to:
//   Cast Test Untrusted ICA    (Not part of trust store)
//
// This is invalid because it does not chain to a trust anchor.
TEST(VerifyCastDeviceCertTest, Unchained) {
  RunTest(CastCertError::ERR_CERTS_VERIFY_GENERIC, "",
          CastDeviceCertPolicy::NONE, "unchained.pem", AprilFirst2016(),
          TRUST_STORE_BUILTIN, "");
}

// Tests verifying one of the self-signed trust anchors (chain of length 1):
//
//  0: Cast Root CA
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
//
// Although this is a valid and trusted certificate (it is one of the
// trust anchors after all) it fails the test as it is not a *device
// certificate*.
TEST(VerifyCastDeviceCertTest, CastRootCa) {
  RunTest(CastCertError::ERR_CERTS_RESTRICTIONS, "", CastDeviceCertPolicy::NONE,
          "cast_root_ca.pem", AprilFirst2016(), TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 2:
//
//  0: 4ZZDZJ FA8FCA7EFE3C
//  1: Chromecast ICA 4 (Audio)
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, ChromecastAudio) {
  RunTest(CastCertError::OK, "4ZZDZJ FA8FCA7EFE3C",
          CastDeviceCertPolicy::AUDIO_ONLY, "chromecast_audio.pem",
          AprilFirst2016(), TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 3:
//
//  0: MediaTek Audio Dev Test
//  1: MediaTek Audio Dev Model
//  2: Cast Audio Dev Root CA
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, MtkAudioDev) {
  RunTest(CastCertError::OK, "MediaTek Audio Dev Test",
          CastDeviceCertPolicy::AUDIO_ONLY, "mtk_audio_dev.pem",
          JanuaryFirst2015(), TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 2:
//
//  0: 9V0000VB FA8FCA784D01
//  1: Cast TV ICA (Vizio)
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
TEST(VerifyCastDeviceCertTest, Vizio) {
  RunTest(CastCertError::OK, "9V0000VB FA8FCA784D01",
          CastDeviceCertPolicy::NONE, "vizio.pem", AprilFirst2016(),
          TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 2 using expired
// time points.
TEST(VerifyCastDeviceCertTest, ChromecastGen2InvalidTime) {
  const char* kCertsFile = "chromecast_gen2.pem";

  // Control test - certificate should be valid at some time otherwise
  // this test is pointless.
  RunTest(CastCertError::OK, "3ZZAK6 FA8FCA3F0D35", CastDeviceCertPolicy::NONE,
          kCertsFile, AprilFirst2016(), TRUST_STORE_BUILTIN, "");

  // Use a time before notBefore.
  RunTest(CastCertError::ERR_CERTS_DATE_INVALID, "", CastDeviceCertPolicy::NONE,
          kCertsFile, JanuaryFirst2015(), TRUST_STORE_BUILTIN, "");

  // Use a time after notAfter.
  RunTest(CastCertError::ERR_CERTS_DATE_INVALID, "", CastDeviceCertPolicy::NONE,
          kCertsFile, MarchFirst2037(), TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 3:
//
//  0: Audio Reference Dev Test
//  1: Audio Reference Dev Model
//  2: Cast Audio Dev Root CA
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, AudioRefDevTestChain3) {
  RunTest(CastCertError::OK, "Audio Reference Dev Test",
          CastDeviceCertPolicy::AUDIO_ONLY, "audio_ref_dev_test_chain_3.pem",
          AprilFirst2016(), TRUST_STORE_BUILTIN,
          "signeddata/AudioReferenceDevTest.pem");
}

// Tests verifying a valid certificate chain of length 3. Note that the first
// intermediate has a serial number that is 21 octets long, which violates RFC
// 5280. However cast verification accepts this certificate for compatibility
// reasons.
//
//  0: 8C579B806FFC8A9DFFFF F8:8F:CA:6B:E6:DA
//  1: Sony so16vic CA
//  2: Cast Audio Sony CA
//
// Chains to trust anchor:
//   Cast Root CA     (built-in trust store)
//
// This device certificate has a policy that means it is valid only for audio
// devices.
TEST(VerifyCastDeviceCertTest, IntermediateSerialNumberTooLong) {
  RunTest(CastCertError::OK, "8C579B806FFC8A9DFFFF F8:8F:CA:6B:E6:DA",
          CastDeviceCertPolicy::AUDIO_ONLY,
          "intermediate_serialnumber_toolong.pem", AprilFirst2016(),
          TRUST_STORE_BUILTIN, "");
}

// Tests verifying a valid certificate chain of length 2 when the trust anchor
// is "expired". This is expected to work since expiration is not an enforced
// anchor constraint, even though it may appear in the root certificate.
//
//  0: CastDevice
//  1: CastIntermediate
//
// Chains to trust anchor:
//   Expired CastRoot     (provided by test data)
TEST(VerifyCastDeviceCertTest, ExpiredTrustAnchor) {
  // The root certificate is only valid in 2015, so validating with a time in
  // 2016 means it is expired.
  RunTest(CastCertError::OK, "CastDevice", CastDeviceCertPolicy::NONE,
          "expired_root.pem", AprilFirst2016(), TRUST_STORE_FROM_TEST_FILE, "");
}

// Tests verifying a certificate chain where the root certificate has a pathlen
// constraint which is violated by the chain. In this case Root has a pathlen=1
// constraint, however neither intermediate is constrained.
//
// The expectation is for pathlen constraints on trust anchors to be enforced,
// so this validation must fail.
//
//  0: Target
//  1: Intermediate2
//  2: Intermediate1
//
// Chains to trust anchor:
//   Root     (provided by test data; has pathlen=1 constraint)
TEST(VerifyCastDeviceCertTest, ViolatesPathlenTrustAnchorConstraint) {
  // First do a control test -- when anchor constraints are NOT enforced this
  // chain should validate just fine.
  RunTest(CastCertError::OK, "Target", CastDeviceCertPolicy::NONE,
          "violates_root_pathlen_constraint.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE_UNCONSTRAINED, "");

  // Now do the real test and verify validation fails when using a TrustAncho
  // with pathlen constraint.
  RunTest(CastCertError::ERR_CERTS_VERIFY_GENERIC, "Target",
          CastDeviceCertPolicy::NONE, "violates_root_pathlen_constraint.pem",
          AprilFirst2016(), TRUST_STORE_FROM_TEST_FILE, "");
}

// Tests verifying a certificate chain with the policies:
//
//  Root:           policies={}
//  Intermediate:   policies={anyPolicy}
//  Leaf:           policies={anyPolicy}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAnypolicyLeafAnypolicy) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::NONE,
          "policies_ica_anypolicy_leaf_anypolicy.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={anyPolicy}
//   Leaf:           policies={audioOnly}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAnypolicyLeafAudioonly) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::AUDIO_ONLY,
          "policies_ica_anypolicy_leaf_audioonly.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={anyPolicy}
//   Leaf:           policies={foo}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAnypolicyLeafFoo) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::NONE,
          "policies_ica_anypolicy_leaf_foo.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={anyPolicy}
//   Leaf:           policies={}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAnypolicyLeafNone) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::NONE,
          "policies_ica_anypolicy_leaf_none.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={audioOnly}
//   Leaf:           policies={anyPolicy}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAudioonlyLeafAnypolicy) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::AUDIO_ONLY,
          "policies_ica_audioonly_leaf_anypolicy.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={audioOnly}
//   Leaf:           policies={audioOnly}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAudioonlyLeafAudioonly) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::AUDIO_ONLY,
          "policies_ica_audioonly_leaf_audioonly.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={audioOnly}
//   Leaf:           policies={foo}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAudioonlyLeafFoo) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::AUDIO_ONLY,
          "policies_ica_audioonly_leaf_foo.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={audioOnly}
//   Leaf:           policies={}
TEST(VerifyCastDeviceCertTest, PoliciesIcaAudioonlyLeafNone) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::AUDIO_ONLY,
          "policies_ica_audioonly_leaf_none.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={}
//   Leaf:           policies={anyPolicy}
TEST(VerifyCastDeviceCertTest, PoliciesIcaNoneLeafAnypolicy) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::NONE,
          "policies_ica_none_leaf_anypolicy.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={}
//   Leaf:           policies={audioOnly}
TEST(VerifyCastDeviceCertTest, PoliciesIcaNoneLeafAudioonly) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::AUDIO_ONLY,
          "policies_ica_none_leaf_audioonly.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={}
//   Leaf:           policies={foo}
TEST(VerifyCastDeviceCertTest, PoliciesIcaNoneLeafFoo) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::NONE,
          "policies_ica_none_leaf_foo.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Test verifying a certificate chain with the policies:
//
//   Root:           policies={}
//   Intermediate:   policies={}
//   Leaf:           policies={}
TEST(VerifyCastDeviceCertTest, PoliciesIcaNoneLeafNone) {
  RunTest(CastCertError::OK, "Leaf", CastDeviceCertPolicy::NONE,
          "policies_ica_none_leaf_none.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE, "");
}

// Tests verifying a certificate chain where the leaf certificate has a
// 1024-bit RSA key. Verification should fail since the target's key is
// too weak.
TEST(VerifyCastDeviceCertTest, DeviceCertHas1024BitRsaKey) {
  RunTest(CastCertError::ERR_CERTS_VERIFY_GENERIC, "RSA 1024 Device Cert",
          CastDeviceCertPolicy::NONE, "rsa1024_device_cert.pem",
          AprilFirst2016(), TRUST_STORE_FROM_TEST_FILE, "");
}

// Tests verifying a certificate chain where the leaf certificate has a
// 2048-bit RSA key, and then verifying signed data (both SHA1 and SHA256)
// for it.
TEST(VerifyCastDeviceCertTest, DeviceCertHas2048BitRsaKey) {
  RunTest(CastCertError::OK, "RSA 2048 Device Cert", CastDeviceCertPolicy::NONE,
          "rsa2048_device_cert.pem", AprilFirst2016(),
          TRUST_STORE_FROM_TEST_FILE,
          "signeddata/rsa2048_device_cert_data.pem");
}

}  // namespace

}  // namespace cast_certificate
