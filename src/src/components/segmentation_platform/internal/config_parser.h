// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_CONFIG_PARSER_H_
#define COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_CONFIG_PARSER_H_

#include <memory>
#include <string>

namespace segmentation_platform {

struct Config;

// Parses and returns the segmentation config from JSON string.
std::unique_ptr<Config> ParseConfigFromString(const std::string& config_str);

}  // namespace segmentation_platform

#endif  // COMPONENTS_SEGMENTATION_PLATFORM_INTERNAL_CONFIG_PARSER_H_
