// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_DESKS_TEMPLATES_SAVED_DESK_DIALOG_CONTROLLER_H_
#define ASH_WM_DESKS_TEMPLATES_SAVED_DESK_DIALOG_CONTROLLER_H_

#include <memory>

#include "ash/ash_export.h"
#include "ash/public/cpp/desk_template.h"
#include "ash/wm/desks/desks_controller.h"
#include "base/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace aura {
class Window;
}

namespace ash {

class SavedDeskDialog;

// SavedDeskDialogController controls when to show the various confirmation
// dialogs for modifying desk templates.
class ASH_EXPORT SavedDeskDialogController : public views::WidgetObserver {
 public:
  SavedDeskDialogController();
  SavedDeskDialogController(const SavedDeskDialogController&) = delete;
  SavedDeskDialogController& operator=(const SavedDeskDialogController&) =
      delete;
  ~SavedDeskDialogController() override;

  const views::Widget* dialog_widget() const { return dialog_widget_; }

  // Shows the dialog. The dialog will look slightly different depending on the
  // type. The `template_name` is used for the replace and delete dialogs, which
  // show the name of the template which will be replaced and deleted in the
  // dialog description.
  void ShowUnsupportedAppsDialog(
      aura::Window* root_window,
      const std::vector<aura::Window*>& unsupported_apps,
      DesksController::GetDeskTemplateCallback callback,
      std::unique_ptr<DeskTemplate> desk_template);
  void ShowReplaceDialog(aura::Window* root_window,
                         const std::u16string& template_name,
                         DeskTemplateType template_type,
                         base::OnceClosure on_accept_callback,
                         base::OnceClosure on_cancel_callback);
  // Show the delete template dialog when user press the delete button.
  // The `template_name` shows the name of the template which will be deleted in
  // the dialog description.
  void ShowDeleteDialog(aura::Window* root_window,
                        const std::u16string& template_name,
                        DeskTemplateType template_type,
                        base::OnceClosure on_accept_callback);

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // Creates and shows the dialog on `root_window`.
  void CreateDialogWidget(std::unique_ptr<SavedDeskDialog> dialog,
                          aura::Window* root_window);

  // Returns true if a dialog can be shown.
  bool CanShowDialog() const;

  // Callbacks for when a user has either accepted the unsupported apps dialog
  // or not.
  void OnUserAcceptedUnsupportedAppsDialog();
  void OnUserCanceledUnsupportedAppsDialog();

  // Pointer to the widget (if any) that contains the current dialog.
  views::Widget* dialog_widget_ = nullptr;

  // When a caller creates an unsupported apps dialog, they provide a callback
  // for the result. Since we can only bind the callback once, we have to store
  // the callback and DeskTemplate until we know what the user's choice is.
  DesksController::GetDeskTemplateCallback unsupported_apps_callback_;
  std::unique_ptr<DeskTemplate> unsupported_apps_template_;

  base::ScopedObservation<views::Widget, views::WidgetObserver>
      dialog_widget_observation_{this};

  base::WeakPtrFactory<SavedDeskDialogController> weak_ptr_factory_{this};
};

}  // namespace ash

#endif  // ASH_WM_DESKS_TEMPLATES_SAVED_DESK_DIALOG_CONTROLLER_H_