// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/lookalikes/core/features.h"

namespace lookalikes {
namespace features {

const base::Feature kLookalikeDigitalAssetLinks{
    "LookalikeDigitalAssetLinks", base::FEATURE_DISABLED_BY_DEFAULT};

const char kLookalikeDigitalAssetLinksTimeoutParameter[] = "timeout";

const base::Feature kDetectComboSquattingLookalikes{
    "ComboSquattingLookalikes", base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features
}  // namespace lookalikes
