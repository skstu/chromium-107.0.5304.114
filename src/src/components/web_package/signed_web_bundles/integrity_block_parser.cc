// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/web_package/signed_web_bundles/integrity_block_parser.h"

#include "base/strings/stringprintf.h"
#include "components/web_package/input_reader.h"
#include "components/web_package/mojom/web_bundle_parser.mojom-forward.h"
#include "components/web_package/signed_web_bundles/ed25519_public_key.h"
#include "components/web_package/web_bundle_parser.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace web_package {

namespace {

// CBOR of the bytes present at the start of the web bundle, including the magic
// string "🖋📦".
//
// The first 10 bytes of the integrity block format are:
//   83                             -- Array of length 3
//      48                          -- Byte string of length 8
//         F0 9F 96 8B F0 9F 93 A6  -- "🖋📦" in UTF-8
// Note: The length of the top level array is 3 (magic, version, signature
// stack).
constexpr uint8_t kIntegrityBlockMagicBytes[] = {0x83, 0x48,
                                                 // "🖋📦" magic bytes
                                                 0xF0, 0x9F, 0x96, 0x8B, 0xF0,
                                                 0x9F, 0x93, 0xA6};

// CBOR of the version string "1b\0\0".
//   44               -- Byte string of length 4
//       31 62 00 00  -- "1b\0\0"
constexpr uint8_t kIntegrityBlockVersionMagicBytes[] = {
    0x44, '1', 'b', 0x00, 0x00,
};

constexpr char kSignatureAttributesPublicKeyWithCBORHeader[] = {
    0x70,  // UTF-8 string of 16 bytes.
    'e',  'd', '2', '5', '5', '1', '9', 'P',
    'u',  'b', 'l', 'i', 'c', 'K', 'e', 'y',
};

}  // namespace

IntegrityBlockParser::IntegrityBlockParser(
    scoped_refptr<WebBundleParser::SharedBundleDataSource> data_source,
    WebBundleParser::ParseIntegrityBlockCallback callback)
    : data_source_(data_source), callback_(std::move(callback)) {
  data_source_->AddObserver(this);
}

IntegrityBlockParser::~IntegrityBlockParser() {
  data_source_->RemoveObserver(this);
}

void IntegrityBlockParser::Start() {
  // First, we will parse the `magic` and `version` bytes.
  const uint64_t length = sizeof(kIntegrityBlockMagicBytes) +
                          sizeof(kIntegrityBlockVersionMagicBytes);
  data_source_->Read(
      0, length,
      base::BindOnce(&IntegrityBlockParser::ParseMagicBytesAndVersion,
                     weak_factory_.GetWeakPtr()));
}

void IntegrityBlockParser::ParseMagicBytesAndVersion(
    const absl::optional<std::vector<uint8_t>>& data) {
  if (!data) {
    RunErrorCallbackAndDestroy(
        "Error reading integrity block magic bytes.",
        mojom::BundleParseErrorType::kParserInternalError);
    return;
  }

  InputReader input(*data);

  // Check the magic bytes.
  const auto magic = input.ReadBytes(sizeof(kIntegrityBlockMagicBytes));
  if (!magic || !std::equal(magic->begin(), magic->end(),
                            std::begin(kIntegrityBlockMagicBytes),
                            std::end(kIntegrityBlockMagicBytes))) {
    RunErrorCallbackAndDestroy("Wrong array size or magic bytes.");
    return;
  }

  // Let version be the result of reading 5 bytes from stream.
  const auto version =
      input.ReadBytes(sizeof(kIntegrityBlockVersionMagicBytes));
  if (!version) {
    RunErrorCallbackAndDestroy("Cannot read version bytes.");
    return;
  }

  if (std::equal(version->begin(), version->end(),
                 std::begin(kIntegrityBlockVersionMagicBytes),
                 std::end(kIntegrityBlockVersionMagicBytes))) {
    signature_stack_ =
        std::vector<mojom::BundleIntegrityBlockSignatureStackEntryPtr>();
  } else {
    RunErrorCallbackAndDestroy(
        "Unexpected integrity block version. Currently supported versions are: "
        "'1b\\0\\0'",
        mojom::BundleParseErrorType::kVersionError);
    return;
  }

  const uint64_t offset_in_stream = input.CurrentOffset();
  data_source_->Read(
      offset_in_stream, kMaxCBORItemHeaderSize,
      base::BindOnce(&IntegrityBlockParser::ParseSignatureStack,
                     weak_factory_.GetWeakPtr(), offset_in_stream));
}

void IntegrityBlockParser::ParseSignatureStack(
    uint64_t offset_in_stream,
    const absl::optional<std::vector<uint8_t>>& data) {
  if (!data) {
    RunErrorCallbackAndDestroy("Error reading signature stack.");
    return;
  }

  InputReader input(*data);

  const auto signature_stack_size = input.ReadCBORHeader(CBORType::kArray);
  if (!signature_stack_size.has_value()) {
    RunErrorCallbackAndDestroy("Cannot parse the size of the signature stack.");
    return;
  }

  if (*signature_stack_size != 1 && *signature_stack_size != 2) {
    // TODO(cmfcmf): Support more signatures for key rotation.
    RunErrorCallbackAndDestroy(
        "The signature stack must contain one or two signatures (developer + "
        "potentially distributor signature).");
    return;
  }

  offset_in_stream += input.CurrentOffset();
  ReadSignatureStackEntry(offset_in_stream, *signature_stack_size);
}

void IntegrityBlockParser::ReadSignatureStackEntry(
    const uint64_t offset_in_stream,
    const uint64_t signature_stack_entries_left) {
  data_source_->Read(
      offset_in_stream, kMaxCBORItemHeaderSize,
      base::BindOnce(&IntegrityBlockParser::ParseSignatureStackEntry,
                     weak_factory_.GetWeakPtr(), offset_in_stream,
                     signature_stack_entries_left));
}

void IntegrityBlockParser::ParseSignatureStackEntry(
    uint64_t offset_in_stream,
    const uint64_t signature_stack_entries_left,
    const absl::optional<std::vector<uint8_t>>& data) {
  if (!data) {
    RunErrorCallbackAndDestroy("Error reading signature stack entry.");
    return;
  }

  InputReader input(*data);

  // Each signature stack entry should be an array with two elements:
  // attributes and signature
  const auto array_length = input.ReadCBORHeader(CBORType::kArray);
  if (!array_length.has_value()) {
    RunErrorCallbackAndDestroy(
        "Cannot parse the size of signature stack entry.");
    return;
  }

  if (*array_length != 2) {
    RunErrorCallbackAndDestroy(
        "Each signature stack entry must contain exactly two elements.");
    return;
  }

  mojom::BundleIntegrityBlockSignatureStackEntryPtr signature_stack_entry =
      mojom::BundleIntegrityBlockSignatureStackEntry::New();
  // Start to keep track of the complete CBOR bytes of the signature stack
  // entry.
  signature_stack_entry->complete_entry_cbor =
      std::vector(data->begin(), data->begin() + input.CurrentOffset());

  offset_in_stream += input.CurrentOffset();
  data_source_->Read(
      offset_in_stream, kMaxCBORItemHeaderSize,
      base::BindOnce(
          &IntegrityBlockParser::ParseSignatureStackEntryAttributesHeader,
          weak_factory_.GetWeakPtr(), offset_in_stream,
          signature_stack_entries_left, std::move(signature_stack_entry)));
}

void IntegrityBlockParser::ParseSignatureStackEntryAttributesHeader(
    uint64_t offset_in_stream,
    const uint64_t signature_stack_entries_left,
    mojom::BundleIntegrityBlockSignatureStackEntryPtr signature_stack_entry,
    const absl::optional<std::vector<uint8_t>>& data) {
  if (!data) {
    RunErrorCallbackAndDestroy(
        "Error reading signature stack entry's attributes header.");
    return;
  }

  InputReader input(*data);

  const auto attributes_length = input.ReadCBORHeader(CBORType::kMap);
  if (!attributes_length.has_value()) {
    RunErrorCallbackAndDestroy(
        "Cannot parse the size of signature stack entry's attributes.");
    return;
  }

  if (*attributes_length != 1) {
    RunErrorCallbackAndDestroy(
        "A signature stack entry's attributes must be a map with one element.");
    return;
  }

  // Keep track of the raw CBOR bytes of both the complete signature stack entry
  // and its attributes.
  signature_stack_entry->complete_entry_cbor.insert(
      signature_stack_entry->complete_entry_cbor.end(), data->begin(),
      data->begin() + input.CurrentOffset());
  signature_stack_entry->attributes_cbor.assign(
      data->begin(), data->begin() + input.CurrentOffset());

  offset_in_stream += input.CurrentOffset();
  data_source_->Read(
      offset_in_stream,
      sizeof(kSignatureAttributesPublicKeyWithCBORHeader) +
          kMaxCBORItemHeaderSize,
      base::BindOnce(
          &IntegrityBlockParser::ParseSignatureStackEntryAttributesPublicKeyKey,
          weak_factory_.GetWeakPtr(), offset_in_stream,
          signature_stack_entries_left, std::move(signature_stack_entry)));
}

// Parse the attribute map key of the public key attribute
void IntegrityBlockParser::ParseSignatureStackEntryAttributesPublicKeyKey(
    uint64_t offset_in_stream,
    const uint64_t signature_stack_entries_left,
    mojom::BundleIntegrityBlockSignatureStackEntryPtr signature_stack_entry,
    const absl::optional<std::vector<uint8_t>>& data) {
  if (!data) {
    RunErrorCallbackAndDestroy(
        "Error reading signature stack entry's ed25519PublicKey attribute.");
    return;
  }

  InputReader input(*data);
  const auto attribute_name =
      input.ReadBytes(sizeof(kSignatureAttributesPublicKeyWithCBORHeader));
  if (!attribute_name) {
    RunErrorCallbackAndDestroy(
        "Error reading signature stack entry's ed25519PublicKey attribute.");
    return;
  }

  if (!std::equal(attribute_name->begin(), attribute_name->end(),
                  std::begin(kSignatureAttributesPublicKeyWithCBORHeader),
                  std::end(kSignatureAttributesPublicKeyWithCBORHeader))) {
    RunErrorCallbackAndDestroy(
        "The signature stack entry's attribute must have 'ed25519PublicKey' as "
        "its key.");
    return;
  }

  const auto public_key_value_size =
      input.ReadCBORHeader(CBORType::kByteString);
  if (!public_key_value_size.has_value()) {
    RunErrorCallbackAndDestroy(
        "The value of the signature stack entry's ed25519PublicKey attribute "
        "must be a byte string.");
    return;
  }

  // Keep track of the raw CBOR bytes of both the complete signature stack entry
  // and its attributes.
  signature_stack_entry->complete_entry_cbor.insert(
      signature_stack_entry->complete_entry_cbor.end(), data->begin(),
      data->begin() + input.CurrentOffset());
  signature_stack_entry->attributes_cbor.insert(
      signature_stack_entry->attributes_cbor.end(), data->begin(),
      data->begin() + input.CurrentOffset());

  offset_in_stream += input.CurrentOffset();
  data_source_->Read(
      offset_in_stream, *public_key_value_size,
      base::BindOnce(&IntegrityBlockParser::
                         ReadSignatureStackEntryAttributesPublicKeyValue,
                     weak_factory_.GetWeakPtr(), offset_in_stream,
                     signature_stack_entries_left,
                     std::move(signature_stack_entry)));
}

void IntegrityBlockParser::ReadSignatureStackEntryAttributesPublicKeyValue(
    uint64_t offset_in_stream,
    const uint64_t signature_stack_entries_left,
    mojom::BundleIntegrityBlockSignatureStackEntryPtr signature_stack_entry,
    const absl::optional<std::vector<uint8_t>>& public_key) {
  if (!public_key) {
    RunErrorCallbackAndDestroy(
        "Error reading signature stack entry's public key.");
    return;
  }
  if (public_key->size() != Ed25519PublicKey::kLength) {
    RunErrorCallbackAndDestroy(
        base::StringPrintf("The public key does not have the correct length, "
                           "expected %zu bytes.",
                           Ed25519PublicKey::kLength));
    return;
  }

  // Keep track of the raw CBOR bytes of both the complete signature stack entry
  // and its attributes.
  signature_stack_entry->complete_entry_cbor.insert(
      signature_stack_entry->complete_entry_cbor.end(), public_key->begin(),
      public_key->end());
  signature_stack_entry->attributes_cbor.insert(
      signature_stack_entry->attributes_cbor.end(), public_key->begin(),
      public_key->end());

  signature_stack_entry->public_key = *public_key;

  offset_in_stream += public_key->size();
  data_source_->Read(
      offset_in_stream, kMaxCBORItemHeaderSize,
      base::BindOnce(
          &IntegrityBlockParser::ParseSignatureStackEntrySignatureHeader,
          weak_factory_.GetWeakPtr(), offset_in_stream,
          signature_stack_entries_left, std::move(signature_stack_entry)));
}

void IntegrityBlockParser::ParseSignatureStackEntrySignatureHeader(
    uint64_t offset_in_stream,
    const uint64_t signature_stack_entries_left,
    mojom::BundleIntegrityBlockSignatureStackEntryPtr signature_stack_entry,
    const absl::optional<std::vector<uint8_t>>& data) {
  if (!data) {
    RunErrorCallbackAndDestroy(
        "Error reading CBOR header of the signature stack entry's signature.");
    return;
  }

  InputReader input(*data);

  const auto signature_length = input.ReadCBORHeader(CBORType::kByteString);
  if (!signature_length.has_value()) {
    RunErrorCallbackAndDestroy(
        "Cannot parse the size of signature stack entry's signature.");
    return;
  }
  if (*signature_length != ED25519_SIGNATURE_LEN) {
    RunErrorCallbackAndDestroy(
        base::StringPrintf("The signature does not have the correct length, "
                           "expected %u bytes.",
                           ED25519_SIGNATURE_LEN));
    return;
  }

  // Keep track of the raw CBOR bytes of the complete signature stack entry.
  signature_stack_entry->complete_entry_cbor.insert(
      signature_stack_entry->complete_entry_cbor.end(), data->begin(),
      data->begin() + input.CurrentOffset());

  offset_in_stream += input.CurrentOffset();
  data_source_->Read(
      offset_in_stream, *signature_length,
      base::BindOnce(&IntegrityBlockParser::ParseSignatureStackEntrySignature,
                     weak_factory_.GetWeakPtr(), offset_in_stream,
                     signature_stack_entries_left,
                     std::move(signature_stack_entry)));
}

void IntegrityBlockParser::ParseSignatureStackEntrySignature(
    uint64_t offset_in_stream,
    uint64_t signature_stack_entries_left,
    mojom::BundleIntegrityBlockSignatureStackEntryPtr signature_stack_entry,
    const absl::optional<std::vector<uint8_t>>& signature) {
  if (!signature) {
    RunErrorCallbackAndDestroy(
        "Error reading signature-stack entry signature.");
    return;
  }

  // Keep track of the raw CBOR bytes of the complete signature stack entry.
  signature_stack_entry->complete_entry_cbor.insert(
      signature_stack_entry->complete_entry_cbor.end(), signature->begin(),
      signature->end());

  signature_stack_entry->signature = *signature;

  signature_stack_.emplace_back(std::move(signature_stack_entry));

  offset_in_stream += signature->size();

  DCHECK(signature_stack_entries_left > 0);
  --signature_stack_entries_left;
  if (signature_stack_entries_left > 0) {
    ReadSignatureStackEntry(offset_in_stream, signature_stack_entries_left);
  } else {
    RunSuccessCallbackAndDestroy(offset_in_stream);
  }
}

void IntegrityBlockParser::RunSuccessCallbackAndDestroy(
    const uint64_t offset_in_stream) {
  mojom::BundleIntegrityBlockPtr integrity_block =
      mojom::BundleIntegrityBlock::New();
  integrity_block->size = offset_in_stream;
  integrity_block->signature_stack = std::move(signature_stack_);

  std::move(callback_).Run(std::move(integrity_block), nullptr);
  delete this;
}

void IntegrityBlockParser::RunErrorCallbackAndDestroy(
    const std::string& message,
    mojom::BundleParseErrorType error_type) {
  std::move(callback_).Run(
      nullptr, mojom::BundleIntegrityBlockParseError::New(error_type, message));
  delete this;
}

void IntegrityBlockParser::OnDisconnect() {
  RunErrorCallbackAndDestroy("Data source disconnected.");
}

}  // namespace web_package
