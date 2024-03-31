// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/smb_shares/smb_share_dialog.h"

#include "base/callback_helpers.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/ash/smb_client/smb_service.h"
#include "chrome/browser/ash/smb_client/smb_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chromeos/smb_shares/smb_handler.h"
#include "chrome/browser/ui/webui/chromeos/smb_shares/smb_shares_localized_strings_provider.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace chromeos {
namespace smb_dialog {
namespace {

constexpr int kSmbShareDialogHeight = 515;

void AddSmbSharesStrings(content::WebUIDataSource* html_source) {
  // Add strings specific to smb_dialog.
  smb_dialog::AddLocalizedStrings(html_source);

  // Add additional strings that are not specific to smb_dialog.
  static const struct {
    const char* name;
    int id;
  } localized_strings[] = {
      {"addSmbShare", IDS_SETTINGS_DOWNLOADS_SMB_SHARES_ADD_SHARE},
      {"add", IDS_ADD},
      {"cancel", IDS_CANCEL},
  };
  for (const auto& entry : localized_strings) {
    html_source->AddLocalizedString(entry.name, entry.id);
  }
}

}  // namespace

// static
void SmbShareDialog::Show() {
  SmbShareDialog* dialog = new SmbShareDialog();
  dialog->ShowSystemDialog();
}

SmbShareDialog::SmbShareDialog()
    : SystemWebDialogDelegate(GURL(chrome::kChromeUISmbShareURL),
                              std::u16string() /* title */) {}

SmbShareDialog::~SmbShareDialog() = default;

void SmbShareDialog::GetDialogSize(gfx::Size* size) const {
  size->SetSize(SystemWebDialogDelegate::kDialogWidth, kSmbShareDialogHeight);
}

SmbShareDialogUI::SmbShareDialogUI(content::WebUI* web_ui)
    : ui::WebDialogUI(web_ui) {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUISmbShareHost);

  source->DisableTrustedTypesCSP();

  AddSmbSharesStrings(source);

  Profile* const profile = Profile::FromWebUI(web_ui);
  const user_manager::User* user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(profile);

  source->AddBoolean("isActiveDirectoryUser",
                     user && user->IsActiveDirectoryUser());

  const ash::smb_client::SmbService* const smb_service =
      ash::smb_client::SmbServiceFactory::Get(profile);
  bool is_kerberos_enabled =
      smb_service && smb_service->IsKerberosEnabledViaPolicy();
  source->AddBoolean("isKerberosEnabled", is_kerberos_enabled);

  bool is_guest = user_manager::UserManager::Get()->IsLoggedInAsGuest() ||
                  user_manager::UserManager::Get()->IsLoggedInAsPublicAccount();
  source->AddBoolean("isGuest", is_guest);

  source->UseStringsJs();
  source->SetDefaultResource(IDR_SMB_SHARES_DIALOG_CONTAINER_HTML);
  source->AddResourcePath("smb_share_dialog.js", IDR_SMB_SHARES_DIALOG_JS);

  web_ui->AddMessageHandler(std::make_unique<SmbHandler>(
      Profile::FromWebUI(web_ui), base::DoNothing()));

  content::WebUIDataSource::Add(Profile::FromWebUI(web_ui), source);
}

SmbShareDialogUI::~SmbShareDialogUI() = default;

bool SmbShareDialog::ShouldShowCloseButton() const {
  return false;
}

}  // namespace smb_dialog
}  // namespace chromeos