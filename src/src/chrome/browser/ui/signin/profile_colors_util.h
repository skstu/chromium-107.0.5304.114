// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SIGNIN_PROFILE_COLORS_UTIL_H_
#define CHROME_BROWSER_UI_SIGNIN_PROFILE_COLORS_UTIL_H_

#include "base/callback_forward.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "third_party/skia/include/core/SkColor.h"

namespace chrome_colors {
struct ColorInfo;
}

namespace ui {
class ColorProvider;
}

class ProfileAttributesEntry;
class ProfileAttributesStorage;

struct ProfileThemeColors {
  SkColor profile_highlight_color;
  SkColor default_avatar_fill_color;
  SkColor default_avatar_stroke_color;

  bool operator<(const ProfileThemeColors& other) const;

  // Equality operators for testing.
  bool operator==(const ProfileThemeColors& other) const;
  bool operator!=(const ProfileThemeColors& other) const;
};

// Returns ProfileThemeColors for profiles with a theme autogenerated from
// `autogenerated_color`.
ProfileThemeColors GetProfileThemeColorsForAutogeneratedColor(
    SkColor autogenerated_color);

// Extracts ProfileThemeColors out of a color provider.
ProfileThemeColors GetCurrentProfileThemeColors(
    const ui::ColorProvider& color_provider);

// Returns ProfileThemeColors for profiles without autogenerated theme.
ProfileThemeColors GetDefaultProfileThemeColors();

// Returns the color that should be used to display text over the profile
// highlight color.
SkColor GetProfileForegroundTextColor(SkColor profile_highlight_color);

// Returns the color that should be used to display icons over the profile
// highlight color.
SkColor GetProfileForegroundIconColor(SkColor profile_highlight_color);

// Returns the color that should be used to generate the default avatar icon.
SkColor GetAvatarStrokeColor(const ui::ColorProvider& color_provider,
                             SkColor avatar_fill_color);

// Filters used for generating colors for a new profile. Exposed for tests.
bool IsSaturatedForAutoselection(SkColor color);
bool IsLightForAutoselection(SkColor color, double reference_lightness);

// Returns a new color for a profile, based on the colors of the existing
// profiles in `storage`. `random_generator` is called to provide randomness and
// must return a value smaller than provided `count`. This implementation
// function is mainly exposed for easier mocking in tests. In production code,
// GenerateNewProfileColor() should be sufficient. `current_profile` should be
// specified if a new profile is created within an existing profile (such as for
// sign-in interception) and thus the two colors should somehow match.
chrome_colors::ColorInfo GenerateNewProfileColorWithGenerator(
    ProfileAttributesStorage& storage,
    base::OnceCallback<size_t(size_t count)> random_generator,
    ProfileAttributesEntry* current_profile = nullptr);

// Returns a new random color for a profile, based on the colors of the existing
// profiles. `current_profile` should be specified if a new profile is created
// within an existing profile (such as for sign-in interception) and thus the
// two colors should somehow match.
chrome_colors::ColorInfo GenerateNewProfileColor(
    ProfileAttributesEntry* current_profile = nullptr);

#endif  // CHROME_BROWSER_UI_SIGNIN_PROFILE_COLORS_UTIL_H_