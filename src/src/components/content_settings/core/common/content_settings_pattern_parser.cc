// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/content_settings/core/common/content_settings_pattern_parser.h"

#include <stddef.h>

#include "base/notreached.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "url/url_constants.h"

namespace {

const char kDomainWildcard[] = "[*.]";
const size_t kDomainWildcardLength = 4;
const char kHostWildcard[] = "*";
const char kPathWildcard[] = "*";
const char kPortWildcard[] = "*";
const char kSchemeWildcard[] = "*";
const char kUrlPathSeparator = '/';
const char kUrlPortSeparator = ':';
const char kUrlPortAndPathSeparator[] = ":/";
// A domain wildcard pattern involves exactly one separating dot,
// inside the square brackets. This is a common misunderstanding of that
// pattern that we want to check for. See: https://crbug.com/823706.
const char kDomainWildcardWithSuperfluousDot[] = "[*.].";

}  // namespace

namespace content_settings {

void PatternParser::Parse(base::StringPiece pattern_spec,
                          ContentSettingsPattern::BuilderInterface* builder) {
  if (pattern_spec == "*") {
    builder->WithSchemeWildcard();
    builder->WithDomainWildcard();
    builder->WithPortWildcard();
    return;
  }

  // Initialize components for the individual patterns parts to empty
  // sub-strings.
  base::StringPiece scheme_piece;
  base::StringPiece host_piece;
  base::StringPiece port_piece;
  base::StringPiece path_piece;

  base::StringPiece::size_type start = 0;
  base::StringPiece::size_type current_pos = 0;

  if (pattern_spec.empty())
    return;

  // Test if a scheme pattern is in the spec.
  const base::StringPiece standard_scheme_separator(
      url::kStandardSchemeSeparator);
  current_pos = pattern_spec.find(standard_scheme_separator, start);
  if (current_pos != base::StringPiece::npos) {
    scheme_piece = pattern_spec.substr(start, current_pos - start);
    start = current_pos + standard_scheme_separator.size();
    current_pos = start;
  } else {
    current_pos = start;
  }

  if (start >= pattern_spec.size())
    return;  // Bad pattern spec.

  // Jump to the end of domain wildcards or an IPv6 addresses. IPv6 addresses
  // contain ':'. So first move to the end of an IPv6 address befor searching
  // for the ':' that separates the port form the host.
  if (pattern_spec[current_pos] == '[')
    current_pos = pattern_spec.find("]", start);

  if (current_pos == base::StringPiece::npos)
    return;  // Bad pattern spec.

  current_pos =
      pattern_spec.find_first_of(kUrlPortAndPathSeparator, current_pos);
  if (current_pos == base::StringPiece::npos) {
    // No port spec found AND no path found.
    current_pos = pattern_spec.size();
    host_piece = pattern_spec.substr(start, current_pos - start);
    start = current_pos;
  } else if (pattern_spec[current_pos] == kUrlPathSeparator) {
    // Pattern has a path spec.
    host_piece = pattern_spec.substr(start, current_pos - start);
    start = current_pos;
  } else if (pattern_spec[current_pos] == kUrlPortSeparator) {
    // Port spec found.
    host_piece = pattern_spec.substr(start, current_pos - start);
    start = current_pos + 1;
    if (start < pattern_spec.size()) {
      current_pos = pattern_spec.find(kUrlPathSeparator, start);
      if (current_pos == base::StringPiece::npos) {
        current_pos = pattern_spec.size();
      }
      port_piece = pattern_spec.substr(start, current_pos - start);
      start = current_pos;
    }
  } else {
    NOTREACHED();
  }

  current_pos = pattern_spec.size();
  if (start < current_pos) {
    // Pattern has a path spec.
    path_piece = pattern_spec.substr(start, current_pos - start);
  }

  // Set pattern parts.
  if (!scheme_piece.empty()) {
    if (scheme_piece == kSchemeWildcard) {
      builder->WithSchemeWildcard();
    } else {
      builder->WithScheme(std::string(scheme_piece));
    }
  } else {
    builder->WithSchemeWildcard();
  }

  if (!host_piece.empty()) {
    if (host_piece == kHostWildcard) {
      if (ContentSettingsPattern::IsNonWildcardDomainNonPortScheme(
              scheme_piece)) {
        builder->Invalid();
        return;
      }

      builder->WithDomainWildcard();
    } else if (base::StartsWith(host_piece, kDomainWildcard,
                                base::CompareCase::SENSITIVE)) {
      if (ContentSettingsPattern::IsNonWildcardDomainNonPortScheme(
              scheme_piece)) {
        builder->Invalid();
        return;
      }

      if (base::StartsWith(host_piece, kDomainWildcardWithSuperfluousDot,
                           base::CompareCase::SENSITIVE)) {
        builder->Invalid();
        return;
      }

      host_piece.remove_prefix(kDomainWildcardLength);
      builder->WithDomainWildcard();
      builder->WithHost(std::string(host_piece));
    } else {
      // If the host contains a wildcard symbol then it is invalid.
      if (host_piece.find(kHostWildcard) != base::StringPiece::npos) {
        builder->Invalid();
        return;
      }
      builder->WithHost(std::string(host_piece));
    }
  }

  if (!port_piece.empty()) {
    if (ContentSettingsPattern::IsNonWildcardDomainNonPortScheme(
            scheme_piece)) {
      builder->Invalid();
      return;
    }

    if (port_piece == kPortWildcard) {
      builder->WithPortWildcard();
    } else {
      // Check if the port string represents a valid port.
      for (const auto port_char : port_piece) {
        if (!base::IsAsciiDigit(port_char)) {
          builder->Invalid();
          return;
        }
      }
      // TODO(markusheintz): Check port range.
      builder->WithPort(std::string(port_piece));
    }
  } else if (!ContentSettingsPattern::IsNonWildcardDomainNonPortScheme(
                 scheme_piece) &&
             !base::EqualsCaseInsensitiveASCII(scheme_piece,
                                               url::kFileScheme)) {
    builder->WithPortWildcard();
  }

  if (!path_piece.empty()) {
    if (path_piece.substr(1) == kPathWildcard)
      builder->WithPathWildcard();
    else
      builder->WithPath(std::string(path_piece));
  }
}

// static
std::string PatternParser::ToString(
    const ContentSettingsPattern::PatternParts& parts) {
  // Return the most compact form to support legacy code and legacy pattern
  // strings.
  if (parts.is_scheme_wildcard && parts.has_domain_wildcard &&
      parts.host.empty() && parts.is_port_wildcard) {
    return "*";
  }

  std::string str;

  if (!parts.is_scheme_wildcard) {
    str += parts.scheme;
    str += url::kStandardSchemeSeparator;
  }

  if (parts.scheme == url::kFileScheme) {
    if (parts.is_path_wildcard) {
      str += kUrlPathSeparator;
      str += kPathWildcard;
      return str;
    }
    str += parts.path;
    return str;
  }

  if (parts.has_domain_wildcard) {
    if (parts.host.empty())
      str += kHostWildcard;
    else
      str += kDomainWildcard;
  }
  str += parts.host;

  if (ContentSettingsPattern::IsNonWildcardDomainNonPortScheme(parts.scheme)) {
    if (parts.path.empty())
      str += kUrlPathSeparator;
    else
      str += parts.path;
    return str;
  }

  if (!parts.is_port_wildcard) {
    str += kUrlPortSeparator;
    str += parts.port;
  }

  return str;
}

}  // namespace content_settings
