// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/autofill/autofill_popup_view_android.h"

#include <memory>
#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/command_line.h"
#include "base/strings/strcat.h"
#include "chrome/android/chrome_jni_headers/AutofillPopupBridge_jni.h"
#include "chrome/browser/android/resource_mapper.h"
#include "chrome/browser/autofill/autofill_keyboard_accessory_adapter.h"
#include "chrome/browser/ui/android/autofill/autofill_keyboard_accessory_view.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller_utils.h"
#include "components/autofill/core/browser/ui/popup_item_ids.h"
#include "components/autofill/core/browser/ui/suggestion.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/security_state/core/security_state.h"
#include "ui/android/view_android.h"
#include "ui/android/window_android.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/android/gurl_android.h"

using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace autofill {

AutofillPopupViewAndroid::AutofillPopupViewAndroid(
    base::WeakPtr<AutofillPopupController> controller)
    : controller_(controller), deleting_index_(-1) {}

AutofillPopupViewAndroid::~AutofillPopupViewAndroid() {}

void AutofillPopupViewAndroid::Show() {
  OnSuggestionsChanged();
}

void AutofillPopupViewAndroid::Hide() {
  controller_ = nullptr;
  JNIEnv* env = base::android::AttachCurrentThread();
  if (!java_object_.is_null()) {
    Java_AutofillPopupBridge_dismiss(env, java_object_);
  } else {
    // Hide() should delete |this| either via Java dismiss or directly.
    delete this;
  }
}

void AutofillPopupViewAndroid::OnSelectedRowChanged(
    absl::optional<int> previous_row_selection,
    absl::optional<int> current_row_selection) {}

void AutofillPopupViewAndroid::OnSuggestionsChanged() {
  if (java_object_.is_null())
    return;

  const ScopedJavaLocalRef<jobject> view = popup_view_.view();
  if (view.is_null())
    return;

  ui::ViewAndroid* view_android = controller_->container_view();

  DCHECK(view_android);
  JNIEnv* env = base::android::AttachCurrentThread();
  view_android->SetAnchorRect(view, controller_->element_bounds());

  size_t count = controller_->GetLineCount();
  ScopedJavaLocalRef<jobjectArray> data_array =
      Java_AutofillPopupBridge_createAutofillSuggestionArray(env, count);

  for (size_t i = 0; i < count; ++i) {
    std::u16string value_text =
        controller_->GetSuggestionMinorTextAt(i).empty()
            ? controller_->GetSuggestionMainTextAt(i)
            : base::StrCat({controller_->GetSuggestionMainTextAt(i), u" ",
                            controller_->GetSuggestionMinorTextAt(i)});
    ScopedJavaLocalRef<jstring> value =
        base::android::ConvertUTF16ToJavaString(env, value_text);
    ScopedJavaLocalRef<jstring> label =
        base::android::ConvertUTF8ToJavaString(env, std::string());
    std::vector<std::vector<autofill::Suggestion::Text>> suggestion_labels =
        controller_->GetSuggestionLabelsAt(i);
    if (!suggestion_labels.empty()) {
      DCHECK_EQ(suggestion_labels.size(), 1U);
      DCHECK_EQ(suggestion_labels[0].size(), 1U);
      label = base::android::ConvertUTF16ToJavaString(
          env, std::move(suggestion_labels[0][0].value));
    }
    int android_icon_id = 0;

    const Suggestion& suggestion = controller_->GetSuggestionAt(i);
    if (!suggestion.icon.empty()) {
      android_icon_id = ResourceMapper::MapToJavaDrawableId(
          GetIconResourceID(suggestion.icon));
    }

    bool is_deletable =
        controller_->GetRemovalConfirmationText(i, nullptr, nullptr);
    bool is_label_multiline =
        suggestion.frontend_id ==
            POPUP_ITEM_ID_INSECURE_CONTEXT_PAYMENT_DISABLED_MESSAGE ||
        suggestion.frontend_id == POPUP_ITEM_ID_CREDIT_CARD_SIGNIN_PROMO ||
        suggestion.frontend_id == POPUP_ITEM_ID_MIXED_FORM_MESSAGE;
    // Set the offer title to display as the item tag.
    ScopedJavaLocalRef<jstring> item_tag =
        base::android::ConvertUTF16ToJavaString(env, suggestion.offer_label);
    Java_AutofillPopupBridge_addToAutofillSuggestionArray(
        env, data_array, i, value, label, item_tag, android_icon_id,
        suggestion.is_icon_at_start, suggestion.frontend_id, is_deletable,
        is_label_multiline, /*isLabelBold*/ false,
        url::GURLAndroid::FromNativeGURL(env, suggestion.custom_icon_url));
  }

  Java_AutofillPopupBridge_show(env, java_object_, data_array,
                                controller_->IsRTL());
}

absl::optional<int32_t> AutofillPopupViewAndroid::GetAxUniqueId() {
  NOTIMPLEMENTED() << "See https://crbug.com/985927";
  return absl::nullopt;
}

void AutofillPopupViewAndroid::SuggestionSelected(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint list_index) {
  // Race: Hide() may have already run.
  if (controller_)
    controller_->AcceptSuggestion(list_index);
}

void AutofillPopupViewAndroid::DeletionRequested(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint list_index) {
  if (!controller_ || java_object_.is_null())
    return;

  std::u16string confirmation_title, confirmation_body;
  if (!controller_->GetRemovalConfirmationText(list_index, &confirmation_title,
          &confirmation_body)) {
    return;
  }

  deleting_index_ = list_index;
  Java_AutofillPopupBridge_confirmDeletion(
      env, java_object_,
      base::android::ConvertUTF16ToJavaString(env, confirmation_title),
      base::android::ConvertUTF16ToJavaString(env, confirmation_body));
}

void AutofillPopupViewAndroid::DeletionConfirmed(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  if (!controller_)
    return;

  CHECK_GE(deleting_index_, 0);
  controller_->RemoveSuggestion(deleting_index_);
}

void AutofillPopupViewAndroid::PopupDismissed(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  if (controller_)
    controller_->ViewDestroyed();

  // The controller has now deleted itself. Remove dangling weak reference.
  controller_ = nullptr;
  delete this;
}

bool AutofillPopupViewAndroid::Init() {
  JNIEnv* env = base::android::AttachCurrentThread();
  ui::ViewAndroid* view_android = controller_->container_view();

  DCHECK(view_android);
  popup_view_ = view_android->AcquireAnchorView();
  const ScopedJavaLocalRef<jobject> view = popup_view_.view();
  if (view.is_null())
    return false;
  ui::WindowAndroid* window_android = view_android->GetWindowAndroid();
  if (!window_android)
    return false;  // The window might not be attached (yet or anymore).

  java_object_.Reset(Java_AutofillPopupBridge_create(
      env, view, reinterpret_cast<intptr_t>(this),
      window_android->GetJavaObject()));
  return true;
}

bool AutofillPopupViewAndroid::WasSuppressed() {
  return java_object_ &&
         Java_AutofillPopupBridge_wasSuppressed(
             base::android::AttachCurrentThread(), java_object_);
}

// static
AutofillPopupView* AutofillPopupView::Create(
    base::WeakPtr<AutofillPopupController> controller) {
  if (IsKeyboardAccessoryEnabled()) {
    auto adapter =
        std::make_unique<AutofillKeyboardAccessoryAdapter>(controller);
    auto accessory_view =
        std::make_unique<AutofillKeyboardAccessoryView>(adapter->GetWeakPtr());
    if (!accessory_view->Initialize())
      return nullptr;  // Don't create an adapter without initialized view.
    adapter->SetAccessoryView(std::move(accessory_view));
    return adapter.release();
  }

  auto popup_view = std::make_unique<AutofillPopupViewAndroid>(controller);
  if (!popup_view->Init() || popup_view->WasSuppressed())
    return nullptr;
  return popup_view.release();
}

}  // namespace autofill