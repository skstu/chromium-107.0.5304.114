// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_TEXT_INPUT_H_
#define COMPONENTS_EXO_TEXT_INPUT_H_

#include <string>

#include "base/i18n/rtl.h"
#include "base/scoped_observation.h"
#include "base/strings/string_piece.h"
#include "components/exo/seat_observer.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_flags.h"
#include "ui/base/ime/text_input_mode.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/ime/virtual_keyboard_controller.h"
#include "ui/base/ime/virtual_keyboard_controller_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/range/range.h"

namespace ui {
class InputMethod;
}  // namespace ui

namespace exo {
class Surface;
class Seat;

// This class bridges the ChromeOS input method and a text-input context.
// It can be inactive, active, or in a pending state where Activate() was
// called but the associated window is not focused.
class TextInput : public ui::TextInputClient,
                  public ui::VirtualKeyboardControllerObserver,
                  public SeatObserver {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the text input session is activated.
    virtual void Activated() = 0;

    // Called when the text input session is deactivated. TextInput does not
    // refer to the delegate anymore.
    virtual void Deactivated() = 0;

    // Called when the virtual keyboard visibility state has changed.
    virtual void OnVirtualKeyboardVisibilityChanged(bool is_visible) = 0;

    // Called when the virtual keyboard's occluded bounds has changed.
    // The bounds are in screen DIP.
    virtual void OnVirtualKeyboardOccludedBoundsChanged(
        const gfx::Rect& screen_bounds) = 0;

    // Set the 'composition text' of the current text input.
    virtual void SetCompositionText(const ui::CompositionText& composition) = 0;

    // Commit |text| to the current text input session.
    virtual void Commit(const std::u16string& text) = 0;

    // Set the cursor position.
    // |surrounding_text| is the current surrounding text.
    // The |selection| range is in UTF-16 offsets of the current surrounding
    // text. |selection| must be a valid range, i.e.
    // selection.IsValid() && selection.GetMax() <= surrounding_text.length().
    virtual void SetCursor(base::StringPiece16 surrounding_text,
                           const gfx::Range& selection) = 0;

    // Delete the surrounding text of the current text input.
    // |surrounding_text| is the current surrounding text.
    // The delete |range| is in UTF-16 offsets of the current surrounding text.
    // |range| must be a valid range, i.e.
    // range.IsValid() && range.GetMax() <= surrounding_text.length().
    virtual void DeleteSurroundingText(base::StringPiece16 surrounding_text,
                                       const gfx::Range& range) = 0;

    // Sends a key event.
    virtual void SendKey(const ui::KeyEvent& event) = 0;

    // Called when the text direction has changed.
    virtual void OnTextDirectionChanged(
        base::i18n::TextDirection direction) = 0;

    // Sets composition from the current surrounding text offsets.
    // Offsets in |cursor| and |range| is relative to the beginning of
    // |surrounding_text|. Offsets in |ui_ime_text_spans| is relative to the new
    // composition, i.e. relative to |range|'s start. All offsets are in UTF16,
    // and must be valid.
    virtual void SetCompositionFromExistingText(
        base::StringPiece16 surrounding_text,
        const gfx::Range& cursor,
        const gfx::Range& range,
        const std::vector<ui::ImeTextSpan>& ui_ime_text_spans) = 0;

    // Clears all the grammar fragments in |range|.
    // |surrounding_text| is the current surrounding text, used for utf16 to
    // utf8 conversion.
    virtual void ClearGrammarFragments(base::StringPiece16 surrounding_text,
                                       const gfx::Range& range) = 0;

    // Adds a new grammar marker according to |fragments|. Clients should show
    // some visual indications such as underlining.
    // |surrounding_text| is the current surrounding text, used for utf16 to
    // utf8 conversion.
    virtual void AddGrammarFragment(base::StringPiece16 surrounding_text,
                                    const ui::GrammarFragment& fragment) = 0;

    // Sets the autocorrect range from the current surrounding text offsets.
    // Offsets in |range| is relative to the beginning of
    // |surrounding_text|. All offsets are in UTF16, and must be valid.
    virtual void SetAutocorrectRange(base::StringPiece16 surrounding_text,
                                     const gfx::Range& range) = 0;
  };

  explicit TextInput(std::unique_ptr<Delegate> delegate);
  TextInput(const TextInput&) = delete;
  TextInput& operator=(const TextInput&) = delete;
  ~TextInput() override;

  // Request to activate the text input context on the surface. Activation will
  // occur immediately if the associated window is already focused, or
  // otherwise when the window gains focus.
  void Activate(Seat* seat, Surface* surface);

  // Deactivates the text input context.
  void Deactivate();

  // Shows the virtual keyboard if needed.
  void ShowVirtualKeyboardIfEnabled();

  // Hides the virtual keyboard.
  void HideVirtualKeyboard();

  // Re-synchronize the current status when the surrounding text has changed
  // during the text input session.
  void Resync();

  // Resets the current input method composition state.
  void Reset();

  // Sets the surrounding text in the app.
  // |cursor_pos| is the range of |text|.
  void SetSurroundingText(const std::u16string& text,
                          const gfx::Range& cursor_pos);

  // Sets the text input type, mode, flags, and |should_do_learning|.
  void SetTypeModeFlags(ui::TextInputType type,
                        ui::TextInputMode mode,
                        int flags,
                        bool should_do_learning);

  // Sets the bounds of the text caret, relative to the window origin.
  void SetCaretBounds(const gfx::Rect& bounds);

  // Sets grammar fragment at the cursor position.
  void SetGrammarFragmentAtCursor(
      const absl::optional<ui::GrammarFragment>& fragment);

  // Sets the autocorrect range and bounds. `autocorrect_bounds` is the
  // bounding rect around the autocorreced text, and are relative to
  // to the window origin.
  void SetAutocorrectInfo(const gfx::Range& autocorrect_range,
                          const gfx::Rect& autocorrect_bounds);

  Delegate* delegate() { return delegate_.get(); }

  // ui::TextInputClient:
  void SetCompositionText(const ui::CompositionText& composition) override;
  size_t ConfirmCompositionText(bool keep_selection) override;
  void ClearCompositionText() override;
  void InsertText(const std::u16string& text,
                  InsertTextCursorBehavior cursor_behavior) override;
  void InsertChar(const ui::KeyEvent& event) override;
  ui::TextInputType GetTextInputType() const override;
  ui::TextInputMode GetTextInputMode() const override;
  base::i18n::TextDirection GetTextDirection() const override;
  int GetTextInputFlags() const override;
  bool CanComposeInline() const override;
  gfx::Rect GetCaretBounds() const override;
  gfx::Rect GetSelectionBoundingBox() const override;
  bool GetCompositionCharacterBounds(size_t index,
                                     gfx::Rect* rect) const override;
  bool HasCompositionText() const override;
  ui::TextInputClient::FocusReason GetFocusReason() const override;
  bool GetTextRange(gfx::Range* range) const override;
  bool GetCompositionTextRange(gfx::Range* range) const override;
  bool GetEditableSelectionRange(gfx::Range* range) const override;
  bool SetEditableSelectionRange(const gfx::Range& range) override;
  bool GetTextFromRange(const gfx::Range& range,
                        std::u16string* text) const override;
  void OnInputMethodChanged() override;
  bool ChangeTextDirectionAndLayoutAlignment(
      base::i18n::TextDirection direction) override;
  void ExtendSelectionAndDelete(size_t before, size_t after) override;
  void EnsureCaretNotInRect(const gfx::Rect& rect) override;
  bool IsTextEditCommandEnabled(ui::TextEditCommand command) const override;
  void SetTextEditCommandForNextKeyEvent(ui::TextEditCommand command) override;
  ukm::SourceId GetClientSourceForMetrics() const override;
  bool ShouldDoLearning() override;
  bool SetCompositionFromExistingText(
      const gfx::Range& range,
      const std::vector<ui::ImeTextSpan>& ui_ime_text_spans) override;
  gfx::Range GetAutocorrectRange() const override;
  gfx::Rect GetAutocorrectCharacterBounds() const override;
  bool SetAutocorrectRange(const gfx::Range& range) override;
  absl::optional<ui::GrammarFragment> GetGrammarFragmentAtCursor()
      const override;
  bool ClearGrammarFragments(const gfx::Range& range) override;
  bool AddGrammarFragments(
      const std::vector<ui::GrammarFragment>& fragments) override;
  void GetActiveTextInputControlLayoutBounds(
      absl::optional<gfx::Rect>* control_bounds,
      absl::optional<gfx::Rect>* selection_bounds) override {}

  // ui::VirtualKeyboardControllerObserver:
  void OnKeyboardVisible(const gfx::Rect& keyboard_rect) override;
  void OnKeyboardHidden() override;

  // SeatObserver:
  void OnSurfaceFocused(Surface* gained_focus,
                        Surface* lost_focus,
                        bool has_focused_surface) override;

 private:
  void AttachInputMethod();
  void DetachInputMethod();
  void ResetCompositionTextCache();

  // Delegate to talk to actual its client.
  std::unique_ptr<Delegate> delegate_;

  // On requesting to show Virtual Keyboard, InputMethod may not be connected.
  // So, remember the request temporarily, and then on InputMethod connection
  // show the Virtual Keyboard.
  bool pending_vk_visible_ = false;

  // |surface_| and |seat_| are non-null if and only if the TextInput is in a
  // pending or active state, in which case the TextInput will be observing the
  // Seat.
  Surface* surface_ = nullptr;
  Seat* seat_ = nullptr;

  // If the TextInput is active (associated window has focus) and the
  // InputMethod is available, this is set and the TextInput will be its
  // focused client. Otherwise, it is null and the TextInput is not attached
  // to any InputMethod, so the TextInputClient overrides will not be called.
  ui::InputMethod* input_method_ = nullptr;

  base::ScopedObservation<ui::VirtualKeyboardController,
                          ui::VirtualKeyboardControllerObserver>
      virtual_keyboard_observation_{this};

  // Cache of the current caret bounding box, sent from the client.
  gfx::Rect caret_bounds_;

  // Cache of the current input field attributes sent from the client.
  ui::TextInputType input_type_ = ui::TEXT_INPUT_TYPE_NONE;
  ui::TextInputMode input_mode_ = ui::TEXT_INPUT_MODE_DEFAULT;
  int flags_ = ui::TEXT_INPUT_FLAG_NONE;
  bool should_do_learning_ = true;

  // Cache of the current surrounding text, sent from the client.
  std::u16string surrounding_text_;

  // Cache of the current cursor position in the surrounding text, sent from
  // the client. Maybe "invalid" value, if not available.
  gfx::Range cursor_pos_ = gfx::Range::InvalidRange();

  // Cache of the current composition range (set in absolute indices).
  gfx::Range composition_range_ = gfx::Range::InvalidRange();

  // Cache of the current composition, updated from Chrome OS IME.
  ui::CompositionText composition_;

  // Cache of the current text input direction, update from the Chrome OS IME.
  base::i18n::TextDirection direction_ = base::i18n::UNKNOWN_DIRECTION;

  // Cache of the grammar fragment at cursor position, send from Lacros side.
  // Wayland API sends the fragment range in utf8 and what IME needs is utf16.
  // To correctly convert the utf8 range to utf16, we need the updated
  // surrounding text, which is not available when we receive the grammar
  // fragment. It is guaranteed that on Lacros side, it always updates grammar
  // fragment before updating surrounding text. So we store the utf8 fragment in
  // |grammar_fragment_at_cursor_utf8_| when we receive it and when we receive
  // the surrounding text update next time, we convert the utf8 fragment to
  // utf16 fragment and store it in |grammar_fragment_at_cursor_utf16_|. When
  // IME requests current grammar fragment, we always return the utf16 version.
  absl::optional<ui::GrammarFragment> grammar_fragment_at_cursor_utf8_;
  absl::optional<ui::GrammarFragment> grammar_fragment_at_cursor_utf16_;

  struct AutocorrectInfo {
    gfx::Range range;
    gfx::Rect bounds;
  };

  // Latest autocorrect information that was sent from the Wayland client.
  // along with the last surrounding text change.
  AutocorrectInfo autocorrect_info_;

  // Latest autocorrect information that was received without a receiving a
  // corresponding surrounding text. Once this class receives a surrounding text
  // update, `autocorrect_info_` will take on this pending value, if it exists.
  absl::optional<AutocorrectInfo> pending_autocorrect_info_;
};

}  // namespace exo

#endif  // COMPONENTS_EXO_TEXT_INPUT_H_
