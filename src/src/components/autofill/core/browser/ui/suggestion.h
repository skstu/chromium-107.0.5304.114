// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_UI_SUGGESTION_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_UI_SUGGESTION_H_

#include <string>

#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "base/types/strong_alias.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/ui/popup_item_ids.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

namespace autofill {

struct Suggestion {
  using IsLoading = base::StrongAlias<class IsLoadingTag, bool>;
  using BackendId = base::StrongAlias<struct BackendIdTag, std::string>;
  using Payload = absl::variant<BackendId, GURL>;

  enum MatchMode {
    PREFIX_MATCH,    // for prefix matched suggestions;
    SUBSTRING_MATCH  // for substring matched suggestions;
  };

  // The text information shown on the UI layer for a Suggestion.
  struct Text {
    using IsPrimary = base::StrongAlias<class IsPrimaryTag, bool>;
    using ShouldTruncate = base::StrongAlias<class ShouldTruncateTag, bool>;

    Text();
    explicit Text(std::u16string value,
                  IsPrimary is_primary = IsPrimary(false),
                  ShouldTruncate should_truncate = ShouldTruncate(false));
    Text(const Text& other);
    Text(Text& other);
    Text& operator=(const Text& other);
    Text& operator=(Text&& other);
    ~Text();
    bool operator==(const Suggestion::Text& text) const;
    bool operator!=(const Suggestion::Text& text) const;

    // The text value to be shown.
    std::u16string value;

    // Whether the text should be shown with a primary style.
    IsPrimary is_primary = IsPrimary(false);

    // Whether the text should be truncated if the bubble width is limited.
    ShouldTruncate should_truncate = ShouldTruncate(false);
  };

  Suggestion();
  explicit Suggestion(std::u16string main_text);
  // Constructor for unit tests. It will convert the strings from UTF-8 to
  // UTF-16.
  Suggestion(base::StringPiece main_text,
             base::StringPiece label,
             std::string icon,
             int frontend_id);
  Suggestion(base::StringPiece main_text,
             base::StringPiece minor_text,
             base::StringPiece label,
             std::string icon,
             int frontend_id);
  Suggestion(const Suggestion& other);
  Suggestion(Suggestion&& other);
  Suggestion& operator=(const Suggestion& other);
  Suggestion& operator=(Suggestion&& other);
  ~Suggestion();

  template <typename T>
  T GetPayload() const {
#if DCHECK_IS_ON()
    DCHECK(Invariant());
#endif
    return absl::holds_alternative<T>(payload) ? absl::get<T>(payload) : T{};
  }

#if DCHECK_IS_ON()
  bool Invariant() const {
    switch (frontend_id) {
      case PopupItemId::POPUP_ITEM_ID_SEE_PROMO_CODE_DETAILS:
        return absl::holds_alternative<GURL>(payload);
      default:
        return absl::holds_alternative<BackendId>(payload);
    }
  }
#endif

  // Payload generated by the backend layer. This payload is either a GUID that
  // identifies the exact autofill profile that generated this suggestion, or a
  // GURL that the suggestion should navigate to upon being accepted.
  Payload payload;

  // TODO(crbug.com/1325509): Convert |frontend_id| from an int to a
  // PopupItemId.
  // ID for the frontend to use in identifying the particular result. Positive
  // values are sent over IPC to identify the item selected. Negative values
  // (see popup_item_ids.h) have special built-in meanings.
  int frontend_id = 0;

  // The texts that will be displayed on the first line in a suggestion. The
  // order of showing the two texts on the first line depends on whether it is
  // in RTL languages. The |main_text| includes the text value to be filled in
  // the form, while the |minor_text| includes other supplementary text value to
  // be shown also on the first line.
  Text main_text;
  Text minor_text;

  // The secondary texts displayed in a suggestion. The labels are presented as
  // a N*M matrix, and the position of the text in the matrix decides where the
  // text will be shown on the UI. (e.g. The text labels[1][2] will be shown on
  // the second line, third column in the grid view of label).
  std::vector<std::vector<Text>> labels;

  // A label to be shown beneath |label| that will display information about any
  // credit card offers or rewards.
  std::u16string offer_label;

  // Used only for passwords to show the password value.
  // Also used to display an extra line of information if two line
  // display is enabled.
  std::u16string additional_label;

  // Contains an image to display for the suggestion.
  gfx::Image custom_icon;

#if BUILDFLAG(IS_ANDROID)
  // The url for the custom icon. This is used by android to fetch the image as
  // android does not support gfx::Image directly.
  GURL custom_icon_url;

  // On Android, the icon can be at the start of the suggestion before the label
  // or at the end of the label.
  bool is_icon_at_start = false;
#endif  // BUILDFLAG(IS_ANDROID)

  // TODO(crbug.com/1019660): Identify icons with enum instead of strings.
  // If |custom_icon| is empty, the name of the fallback built-in icon.
  std::string icon;

  // An icon that appears after the suggestion in the suggestion view. For
  // passwords, this icon string shows whether the suggestion originates from
  // local or account store. It is also used on the settings entry for the
  // credit card Autofill popup to indicate if all credit cards are server
  // cards. It also holds Google Password Manager icon on the settings entry for
  // the passwords Autofill popup.
  std::string trailing_icon;

  MatchMode match = PREFIX_MATCH;

  // Whether suggestion was interacted with and is now in a loading state.
  IsLoading is_loading = IsLoading(false);

  // The In-Product-Help feature that should be shown for the suggestion.
  std::string feature_for_iph;

  // If specified, this text will be played back as voice over for a11y.
  absl::optional<std::u16string> voice_over;
};

#if defined(UNIT_TEST)
inline void PrintTo(const Suggestion& suggestion, std::ostream* os) {
  *os << std::endl
      << "Suggestion (frontend_id:" << suggestion.frontend_id
      << ", main_text:\"" << suggestion.main_text.value << "\""
      << (suggestion.main_text.is_primary ? "(Primary)" : "(Not Primary)")
      << ", minor_text:\"" << suggestion.minor_text.value << "\""
      << (suggestion.minor_text.is_primary ? "(Primary)" : "(Not Primary)")
      << ", additional_label: \"" << suggestion.additional_label << "\""
      << ", icon:" << suggestion.icon
      << ", trailing_icon:" << suggestion.trailing_icon << ")";
}
#endif

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_UI_SUGGESTION_H_
