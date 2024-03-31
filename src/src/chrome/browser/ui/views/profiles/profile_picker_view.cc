// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_picker_view.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/containers/contains.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/accelerator_table.h"
#include "chrome/browser/ui/views/profiles/profile_creation_signed_in_flow_controller.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "chrome/browser/ui/webui/signin/profile_picker_ui.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/google_chrome_strings.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/startup_metric_utils/browser/startup_metric_utils.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "chrome/browser/ui/views/profiles/profile_picker_dice_sign_in_provider.h"
#include "chrome/browser/ui/views/profiles/profile_picker_dice_sign_in_toolbar.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/shell_integration_win.h"
#include "ui/base/win/shell.h"
#include "ui/views/win/hwnd_util.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/global_keyboard_shortcuts_mac.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/ui/views/profiles/lacros_first_run_signed_in_flow_controller.h"
#include "chrome/grit/generated_resources.h"
#endif

namespace {

ProfilePickerView* g_profile_picker_view = nullptr;
base::OnceClosure* g_profile_picker_opened_callback_for_testing = nullptr;

constexpr int kWindowWidth = 1024;
constexpr int kWindowHeight = 758;
constexpr float kMaxRatioOfWorkArea = 0.9;

constexpr int kSupportedAcceleratorCommands[] = {
    IDC_CLOSE_TAB,  IDC_CLOSE_WINDOW,    IDC_EXIT,
    IDC_FULLSCREEN, IDC_MINIMIZE_WINDOW, IDC_BACK,
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    IDC_RELOAD
#endif
};

class ProfilePickerWidget : public views::Widget {
 public:
  explicit ProfilePickerWidget(ProfilePickerView* profile_picker_view)
      : profile_picker_view_(profile_picker_view) {
    views::Widget::InitParams params;
    params.delegate = profile_picker_view_;
    Init(std::move(params));
  }
  ~ProfilePickerWidget() override = default;

 private:
  const raw_ptr<ProfilePickerView> profile_picker_view_;
};

}  // namespace

// static
void ProfilePicker::Show(Params&& params) {
  // Re-open with new params if necessary.
  if (g_profile_picker_view && g_profile_picker_view->MaybeReopen(params))
    return;

  if (g_profile_picker_view) {
    g_profile_picker_view->UpdateParams(std::move(params));
  } else {
    // TODO(crbug.com/1340791): This is temporarily added to understand
    // crbug.com/1340791. Remove when it is resolved.
    LOG(WARNING) << "ProfilePickerView is created";
    g_profile_picker_view = new ProfilePickerView(std::move(params));
  }
  g_profile_picker_view->Display();
}

// static
GURL ProfilePicker::GetOnSelectProfileTargetUrl() {
  if (g_profile_picker_view) {
    return g_profile_picker_view->GetOnSelectProfileTargetUrl();
  }
  return GURL();
}

// static
base::FilePath ProfilePicker::GetSwitchProfilePath() {
  if (g_profile_picker_view &&
      g_profile_picker_view->weak_signed_in_flow_controller_) {
    return g_profile_picker_view->weak_signed_in_flow_controller_
        ->switch_profile_path();
  }
  return base::FilePath();
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
// static
void ProfilePicker::SwitchToDiceSignIn(
    absl::optional<SkColor> profile_color,
    base::OnceCallback<void(bool)> switch_finished_callback) {
  if (g_profile_picker_view) {
    g_profile_picker_view->SwitchToDiceSignIn(
        profile_color, std::move(switch_finished_callback));
  }
}
#endif

// static
void ProfilePicker::SwitchToSignedInFlow(absl::optional<SkColor> profile_color,
                                         Profile* signed_in_profile) {
  if (g_profile_picker_view) {
    g_profile_picker_view->profile_color_ = profile_color;
    g_profile_picker_view->SwitchToSignedInFlow(
        signed_in_profile,
        content::WebContents::Create(
            content::WebContents::CreateParams(signed_in_profile)),
        /*is_saml=*/false);
  }
}

// static
void ProfilePicker::CancelSignedInFlow() {
  if (g_profile_picker_view) {
    g_profile_picker_view->CancelSignedInFlow();
  }
}

// static
base::FilePath ProfilePicker::GetPickerProfilePath() {
  return ProfileManager::GetSystemProfilePath();
}

// static
void ProfilePicker::ShowDialog(content::BrowserContext* browser_context,
                               const GURL& url,
                               const base::FilePath& profile_path) {
  if (g_profile_picker_view) {
    g_profile_picker_view->ShowDialog(browser_context, url, profile_path);
  }
}

// static
void ProfilePicker::HideDialog() {
  if (g_profile_picker_view) {
    g_profile_picker_view->HideDialog();
  }
}

// static
base::FilePath ProfilePicker::GetForceSigninProfilePath() {
  if (g_profile_picker_view) {
    return g_profile_picker_view->GetForceSigninProfilePath();
  }

  return base::FilePath();
}

// static
void ProfilePicker::Hide() {
  if (g_profile_picker_view)
    g_profile_picker_view->Clear();
}

// static
bool ProfilePicker::IsOpen() {
  return g_profile_picker_view;
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
// static
bool ProfilePicker::IsLacrosFirstRunOpen() {
  return ProfilePicker::IsOpen() &&
         g_profile_picker_view->params_.entry_point() ==
             ProfilePicker::EntryPoint::kLacrosPrimaryProfileFirstRun;
}
#endif

bool ProfilePicker::IsActive() {
  if (!IsOpen())
    return false;

#if BUILDFLAG(IS_MAC)
  return g_profile_picker_view->GetWidget()->IsVisible();
#else
  return g_profile_picker_view->GetWidget()->IsActive();
#endif
}

// static
views::WebView* ProfilePicker::GetWebViewForTesting() {
  if (!g_profile_picker_view)
    return nullptr;
  return g_profile_picker_view->web_view_;
}

// static
views::View* ProfilePicker::GetViewForTesting() {
  return g_profile_picker_view;
}

// static
void ProfilePicker::AddOnProfilePickerOpenedCallbackForTesting(
    base::OnceClosure callback) {
  DCHECK(!g_profile_picker_opened_callback_for_testing);
  DCHECK(!callback.is_null());
  g_profile_picker_opened_callback_for_testing =
      new base::OnceClosure(std::move(callback));
}

// ProfilePickerForceSigninDialog
// -------------------------------------------------------------

// static
void ProfilePickerForceSigninDialog::ShowReauthDialog(
    content::BrowserContext* browser_context,
    const std::string& email,
    const base::FilePath& profile_path) {
  DCHECK(signin_util::IsForceSigninEnabled());
  if (!ProfilePicker::IsActive())
    return;
  GURL url = signin::GetEmbeddedReauthURLWithEmail(
      signin_metrics::AccessPoint::ACCESS_POINT_USER_MANAGER,
      signin_metrics::Reason::kReauthentication, email);
  ProfilePicker::ShowDialog(browser_context, url, profile_path);
}

// static
void ProfilePickerForceSigninDialog::ShowForceSigninDialog(
    content::BrowserContext* browser_context,
    const base::FilePath& profile_path) {
  DCHECK(signin_util::IsForceSigninEnabled());
  if (!ProfilePicker::IsActive())
    return;

  GURL url = signin::GetEmbeddedPromoURL(
      signin_metrics::AccessPoint::ACCESS_POINT_USER_MANAGER,
      signin_metrics::Reason::kForcedSigninPrimaryAccount, true);

  ProfilePicker::ShowDialog(browser_context, url, profile_path);
}

void ProfilePickerForceSigninDialog::ShowDialogAndDisplayErrorMessage(
    content::BrowserContext* browser_context) {
  DCHECK(signin_util::IsForceSigninEnabled());
  if (!ProfilePicker::IsActive())
    return;

  GURL url(chrome::kChromeUISigninErrorURL);
  ProfilePicker::ShowDialog(browser_context, url, base::FilePath());
  return;
}

// static
void ProfilePickerForceSigninDialog::DisplayErrorMessage() {
  DCHECK(signin_util::IsForceSigninEnabled());
  if (g_profile_picker_view) {
    g_profile_picker_view->DisplayErrorMessage();
  }
}

// static
void ProfilePickerForceSigninDialog::HideDialog() {
  ProfilePicker::HideDialog();
}

// ProfilePickerView::NavigationFinishedObserver ------------------------------

ProfilePickerView::NavigationFinishedObserver::NavigationFinishedObserver(
    const GURL& url,
    base::OnceClosure closure,
    content::WebContents* contents)
    : content::WebContentsObserver(contents),
      url_(url),
      closure_(std::move(closure)) {}

ProfilePickerView::NavigationFinishedObserver::~NavigationFinishedObserver() =
    default;

void ProfilePickerView::NavigationFinishedObserver::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!closure_ || navigation_handle->GetURL() != url_ ||
      !navigation_handle->HasCommitted()) {
    return;
  }
  std::move(closure_).Run();
}

// ProfilePickerView ----------------------------------------------------------

void ProfilePickerView::UpdateParams(ProfilePicker::Params&& params) {
  DCHECK(params_.CanReusePickerWindow(params));

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  // Cancel any flow that was in progress.
  params_.NotifyAccountSelected(std::string());
  params_.NotifyFirstRunExited(
      ProfilePicker::FirstRunExitStatus::kQuitEarly,
      ProfilePicker::FirstRunExitSource::kReusingWindow, base::OnceClosure());
#endif

  params_ = std::move(params);
}

void ProfilePickerView::DisplayErrorMessage() {
  dialog_host_.DisplayErrorMessage();
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
void ProfilePickerView::NotifyAccountSelected(const std::string& gaia_id) {
  params_.NotifyAccountSelected(gaia_id);
}
#endif

void ProfilePickerView::ShowScreen(
    content::WebContents* contents,
    const GURL& url,
    base::OnceClosure navigation_finished_closure) {
  if (url.is_empty()) {
    DCHECK(!navigation_finished_closure);
    ShowScreenFinished(contents);
    return;
  }

  contents->GetController().LoadURL(url, content::Referrer(),
                                    ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                    std::string());

  // Special-case the first ever screen to make sure the WebView has a contents
  // assigned in the moment when it gets displayed. This avoids a black flash on
  // Win (and potentially other GPU artifacts on other platforms). The rest of
  // the work can still be done asynchronously in ShowScreenFinished().
  if (web_view_->GetWebContents() == nullptr)
    web_view_->SetWebContents(contents);

  // Binding as Unretained as `this` outlives member
  // `show_screen_finished_observer_`. If ShowScreen gets called twice in a
  // short period of time, the first callback may never get called as the first
  // observer gets destroyed here or later in ShowScreenFinished(). This is okay
  // as all the previous values get replaced by the new values.
  show_screen_finished_observer_ = std::make_unique<NavigationFinishedObserver>(
      url,
      base::BindOnce(&ProfilePickerView::ShowScreenFinished,
                     base::Unretained(this), contents,
                     std::move(navigation_finished_closure)),
      contents);

  if (!GetWidget()->IsVisible())
    GetWidget()->Show();
}

void ProfilePickerView::ShowScreenInPickerContents(
    const GURL& url,
    base::OnceClosure navigation_finished_closure) {
  ShowScreen(contents_.get(), url, std::move(navigation_finished_closure));
}

void ProfilePickerView::Clear() {
  TRACE_EVENT1("browser,startup", "ProfilePickerView::Clear", "state", state_);
  if (state_ == kClosing)
    return;

  if (state_ == kReady) {
    GetWidget()->Close();
    state_ = kClosing;
    return;
  }

  WindowClosing();
  DeleteDelegate();
}

bool ProfilePickerView::ShouldUseDarkColors() const {
  return GetNativeTheme()->ShouldUseDarkColors();
}

content::WebContents* ProfilePickerView::GetPickerContents() const {
  return contents_.get();
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
void ProfilePickerView::SetNativeToolbarVisible(bool visible) {
  if (!visible) {
    toolbar_->SetVisible(false);
    return;
  }

  if (toolbar_->children().empty()) {
    toolbar_->BuildToolbar(
        base::BindRepeating(&ProfilePickerView::NavigateBack,
                            // Binding as Unretained as `this` is the
                            // `toolbar_`'s parent and outlives it.
                            base::Unretained(this)));
  }
  toolbar_->SetVisible(true);
}

SkColor ProfilePickerView::GetPreferredBackgroundColor() const {
  return GetColorProvider()->GetColor(kColorToolbar);
}
#endif

bool ProfilePickerView::HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  // Forward the keyboard event to AcceleratorPressed() through the
  // FocusManager.
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

bool ProfilePickerView::HandleContextMenu(
    content::RenderFrameHost& render_frame_host,
    const content::ContextMenuParams& params) {
  // Ignores context menu.
  return true;
}

gfx::NativeView ProfilePickerView::GetHostView() const {
  return GetWidget()->GetNativeView();
}

gfx::Point ProfilePickerView::GetDialogPosition(const gfx::Size& size) {
  gfx::Size widget_size = GetWidget()->GetWindowBoundsInScreen().size();
  return gfx::Point(std::max(0, (widget_size.width() - size.width()) / 2), 0);
}

gfx::Size ProfilePickerView::GetMaximumDialogSize() {
  return GetWidget()->GetWindowBoundsInScreen().size();
}

void ProfilePickerView::AddObserver(
    web_modal::ModalDialogHostObserver* observer) {}

void ProfilePickerView::RemoveObserver(
    web_modal::ModalDialogHostObserver* observer) {}

ProfilePickerView::ProfilePickerView(ProfilePicker::Params&& params)
    : keep_alive_(KeepAliveOrigin::USER_MANAGER_VIEW,
                  KeepAliveRestartOption::DISABLED),
      params_(std::move(params)) {
  // Setup the WidgetDelegate.
  SetHasWindowSizeControls(true);
  SetTitle(IDS_PRODUCT_NAME);

  ConfigureAccelerators();

  // Record creation metrics.
  base::UmaHistogramEnumeration("ProfilePicker.Shown", params_.entry_point());
  if (params_.entry_point() == ProfilePicker::EntryPoint::kOnStartup) {
    DCHECK(creation_time_on_startup_.is_null());
    creation_time_on_startup_ = base::TimeTicks::Now();
    base::UmaHistogramTimes("ProfilePicker.StartupTime.BeforeCreation",
                            creation_time_on_startup_ -
                                startup_metric_utils::MainEntryPointTicks());
  }
}

ProfilePickerView::~ProfilePickerView() {
  if (contents_)
    contents_->SetDelegate(nullptr);
}

bool ProfilePickerView::MaybeReopen(ProfilePicker::Params& params) {
  // Re-open if already closing or if the picker cannot be reused with `params`.
  if (state_ != kClosing && params.CanReusePickerWindow(params_))
    return false;

  restart_on_window_closing_ =
      base::BindOnce(&ProfilePicker::Show, std::move(params));
  // No-op if already closing.
  ProfilePicker::Hide();
  return true;
}

void ProfilePickerView::Display() {
  DCHECK_NE(state_, kClosing);
  TRACE_EVENT2("browser,startup", "ProfilePickerView::Display", "entry_point",
               params_.entry_point(), "state", state_);

  if (state_ == kNotStarted) {
    state_ = kInitializing;
    // Build the layout synchronously before creating the picker profile to
    // simplify tests.
    BuildLayout();

    g_browser_process->profile_manager()->CreateProfileAsync(
        params_.profile_path(),
        base::BindOnce(&ProfilePickerView::OnPickerProfileCreated,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  if (state_ == kInitializing)
    return;

  GetWidget()->Activate();
}

void ProfilePickerView::OnPickerProfileCreated(Profile* picker_profile) {
  TRACE_EVENT1(
      "browser,startup", "ProfilePickerView::OnPickerProfileCreated",
      "profile_path",
      (picker_profile ? picker_profile->GetPath().AsUTF8Unsafe() : ""));
  DCHECK(picker_profile);
  Init(picker_profile);
}

void ProfilePickerView::Init(Profile* picker_profile) {
  DCHECK_EQ(state_, kInitializing);
  TRACE_EVENT1(
      "browser,startup", "ProfilePickerView::Init", "profile_path",
      (picker_profile ? picker_profile->GetPath().AsUTF8Unsafe() : ""));
  contents_ = content::WebContents::Create(
      content::WebContents::CreateParams(picker_profile));
  contents_->SetDelegate(this);

  // Destroy the System Profile when the ProfilePickerView is closed (assuming
  // its refcount hits 0). We need to use GetOriginalProfile() here because
  // |profile_picker| is an OTR Profile, and ScopedProfileKeepAlive only
  // supports non-OTR Profiles. Trying to acquire a keepalive on the OTR Profile
  // would trigger a DCHECK.
  //
  // TODO(crbug.com/1153922): Once OTR Profiles use refcounting, remove the call
  // to GetOriginalProfile(). The OTR Profile will hold a keepalive on the
  // regular Profile, so the ownership model will be more straightforward.
  profile_keep_alive_ = std::make_unique<ScopedProfileKeepAlive>(
      picker_profile->GetOriginalProfile(),
      ProfileKeepAliveOrigin::kProfilePickerView);

  // The widget is owned by the native widget.
  new ProfilePickerWidget(this);

#if BUILDFLAG(IS_WIN)
  // Set the app id for the user manager to the app id of its parent.
  ui::win::SetAppIdForWindow(
      shell_integration::win::GetAppUserModelIdForBrowser(
          picker_profile->GetPath()),
      views::HWNDForWidget(GetWidget()));
#endif

  Step initial_step = Step::kUnknown;
  if (params_.entry_point() ==
      ProfilePicker::EntryPoint::kLacrosPrimaryProfileFirstRun) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
    // TODO(crbug.com/1300109): Consider some refactoring to share this
    // `WebContents` for usage in this class instead of a separate `contents_`.
    std::unique_ptr<content::WebContents> contents_for_signed_in_flow =
        content::WebContents::Create(
            content::WebContents::CreateParams(picker_profile));

    initial_step = Step::kPostSignInFlow;
    initialized_steps_[initial_step] =
        ProfileManagementStepController::CreateForPostSignInFlow(
            this,
            std::make_unique<LacrosFirstRunSignedInFlowController>(
                this, picker_profile, std::move(contents_for_signed_in_flow),
                base::BindOnce(&ProfilePicker::Params::NotifyFirstRunExited,
                               // Unretained ok because the controller is owned
                               // by this through `initialized_steps_`.
                               base::Unretained(&params_))));
#else
    NOTREACHED();
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
  } else {
    initial_step = Step::kProfilePicker;
    initialized_steps_[initial_step] =
        ProfileManagementStepController::CreateForProfilePickerApp(
            this, params_.GetInitialURL());
  }

  SwitchToStep(initial_step);
  state_ = kReady;

  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(prefs::kBrowserProfilePickerShown, true);

  if (params_.entry_point() == ProfilePicker::EntryPoint::kOnStartup) {
    DCHECK(!creation_time_on_startup_.is_null());
    base::UmaHistogramTimes("ProfilePicker.StartupTime.WebViewCreated",
                            base::TimeTicks::Now() - creation_time_on_startup_);
  }

  if (g_profile_picker_opened_callback_for_testing) {
    std::move(*g_profile_picker_opened_callback_for_testing).Run();
    delete g_profile_picker_opened_callback_for_testing;
    g_profile_picker_opened_callback_for_testing = nullptr;
  }
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
void ProfilePickerView::SwitchToDiceSignIn(
    absl::optional<SkColor> profile_color,
    base::OnceCallback<void(bool)> switch_finished_callback) {
  DCHECK_EQ(Step::kProfilePicker, current_step_);
  profile_color_ = profile_color;

  // TODO(crbug.com/1360774): Consider having forced signin as separate step
  // controller for `Step::kAccountSelection`.
  if (signin_util::IsForceSigninEnabled()) {
    SwitchToForcedSignIn(std::move(switch_finished_callback));
    return;
  }

  if (!initialized_steps_.contains(Step::kAccountSelection)) {
    initialized_steps_[Step::kAccountSelection] =
        ProfileManagementStepController::CreateForDiceSignIn(
            /*host=*/this,
            std::make_unique<ProfilePickerDiceSignInProvider>(this),
            base::BindOnce(&ProfilePickerView::SwitchToSignedInFlow,
                           // Binding as Unretained as `this` outlives the step
                           // controllers.
                           base::Unretained(this)));
  }
  auto pop_closure = base::BindOnce(
      &ProfilePickerView::SwitchToStep,
      // Binding as Unretained as `this` outlives the step
      // controllers.
      base::Unretained(this), Step::kProfilePicker,
      /*reset_state=*/false, /*pop_step_callback=*/base::OnceClosure(),
      /*step_switch_finished_callback=*/base::OnceCallback<void(bool)>());
  SwitchToStep(Step::kAccountSelection,
               /*reset_state=*/false, std::move(pop_closure),
               std::move(switch_finished_callback));
}

void ProfilePickerView::SwitchToForcedSignIn(
    base::OnceCallback<void(bool)> switch_finished_callback) {
  DCHECK(signin_util::IsForceSigninEnabled());
  size_t icon_index = profiles::GetPlaceholderAvatarIndex();
  ProfileManager::CreateMultiProfileAsync(
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .ChooseNameForNewProfile(icon_index),
      icon_index, /*is_hidden=*/true,
      base::BindOnce(&ProfilePickerView::OnProfileForDiceForcedSigninCreated,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(switch_finished_callback)));
}

void ProfilePickerView::OnProfileForDiceForcedSigninCreated(
    base::OnceCallback<void(bool)> switch_finished_callback,
    Profile* profile) {
  DCHECK(signin_util::IsForceSigninEnabled());
  if (!profile) {
    std::move(switch_finished_callback).Run(false);
    return;
  }

  std::move(switch_finished_callback).Run(true);
  ProfilePickerForceSigninDialog::ShowForceSigninDialog(
      web_view_->GetWebContents()->GetBrowserContext(), profile->GetPath());
}

#endif

void ProfilePickerView::SwitchToSignedInFlow(
    Profile* signed_in_profile,
    std::unique_ptr<content::WebContents> contents,
    bool is_saml) {
  DCHECK(!signin_util::IsForceSigninEnabled());
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  DCHECK_EQ(Step::kAccountSelection, current_step_);
#endif
  DCHECK(signed_in_profile);

  DCHECK(!initialized_steps_.contains(Step::kPostSignInFlow));

  // TODO(crbug.com/1360055): Split out the SAML flow directly from here instead
  // of using `ProfileCreationSignedInFlowController` for it.
  auto signed_in_flow = std::make_unique<ProfileCreationSignedInFlowController>(
      /*host=*/this, signed_in_profile, std::move(contents), profile_color_,
      is_saml);

  weak_signed_in_flow_controller_ = signed_in_flow->GetWeakPtr();
  initialized_steps_[Step::kPostSignInFlow] =
      ProfileManagementStepController::CreateForPostSignInFlow(
          this, std::move(signed_in_flow));

  SwitchToStep(Step::kPostSignInFlow);

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  // If we need to go back, we should go all the way to the beginning of the
  // flow and after that, recreate the account selection step to ensure no data
  // leaks if we select a different account.
  // We also erase the step after the switch here because it holds a
  // `ScopedProfileKeepAlive` and we need the next step to register its own
  // before this the account selection's is released.
  initialized_steps_.erase(Step::kAccountSelection);
#endif
}

void ProfilePickerView::CancelSignedInFlow() {
  // Triggered from either entreprise welcome or profile switch.
  DCHECK_EQ(Step::kPostSignInFlow, current_step_);

  switch (params_.entry_point()) {
    case ProfilePicker::EntryPoint::kOnStartup:
    case ProfilePicker::EntryPoint::kProfileMenuManageProfiles:
    case ProfilePicker::EntryPoint::kOpenNewWindowAfterProfileDeletion:
    case ProfilePicker::EntryPoint::kNewSessionOnExistingProcess:
    case ProfilePicker::EntryPoint::kProfileLocked:
    case ProfilePicker::EntryPoint::kUnableToCreateBrowser:
    case ProfilePicker::EntryPoint::kBackgroundModeManager:
    case ProfilePicker::EntryPoint::kProfileIdle: {
      SwitchToStep(Step::kProfilePicker, /*reset_state=*/true);
      initialized_steps_.erase(Step::kPostSignInFlow);
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
      initialized_steps_.erase(Step::kAccountSelection);
#endif
      return;
    }
    case ProfilePicker::EntryPoint::kProfileMenuAddNewProfile: {
      // This results in destroying `this`.
      Clear();
      return;
    }
    case ProfilePicker::EntryPoint::kLacrosSelectAvailableAccount:
      NOTREACHED() << "Signed in flow is not reachable from this entry point";
      return;
    case ProfilePicker::EntryPoint::kLacrosPrimaryProfileFirstRun:
      NOTREACHED() << "Signed in flow is not cancellable";
      return;
  }
}

void ProfilePickerView::WindowClosing() {
  views::WidgetDelegateView::WindowClosing();
  // Now that the window is closed, we can allow a new one to be opened.
  // (WindowClosing comes in asynchronously from the call to Close() and we
  // may have already opened a new instance).
  // TODO(crbug.com/1340791): The logging message is added to understand
  // crbug.com/1340791 further temporarily. Remove it when it is resolved.
  if (g_profile_picker_view == this) {
    LOG(WARNING) << "The ProfilePickerView is deleted";
    g_profile_picker_view = nullptr;
  } else {
    LOG(WARNING) << "The WindowClosing event is observed, but which is not "
                 << "for the global ProfilePickerView.";
  }

  // Show a new profile window if it has been requested while the current window
  // was closing.
  if (state_ == kClosing && restart_on_window_closing_)
    std::move(restart_on_window_closing_).Run();
}

views::ClientView* ProfilePickerView::CreateClientView(views::Widget* widget) {
  return new views::ClientView(widget, TransferOwnershipOfContentsView());
}

views::View* ProfilePickerView::GetContentsView() {
  return this;
}

std::u16string ProfilePickerView::GetAccessibleWindowTitle() const {
  if (!web_view_ || !web_view_->GetWebContents() ||
      web_view_->GetWebContents()->GetTitle().empty()) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
    return l10n_util::GetStringUTF16(IDS_PROFILE_PICKER_MAIN_VIEW_TITLE_LACROS);
#else
    return l10n_util::GetStringUTF16(IDS_PROFILE_PICKER_MAIN_VIEW_TITLE);
#endif
  }
  return web_view_->GetWebContents()->GetTitle();
}

gfx::Size ProfilePickerView::CalculatePreferredSize() const {
  gfx::Size preferred_size = gfx::Size(kWindowWidth, kWindowHeight);
  gfx::Size work_area_size = GetWidget()->GetWorkAreaBoundsInScreen().size();
  // Keep the window smaller then |work_area_size| so that it feels more like a
  // dialog then like the actual Chrome window.
  gfx::Size max_dialog_size = ScaleToFlooredSize(
      work_area_size, kMaxRatioOfWorkArea, kMaxRatioOfWorkArea);
  preferred_size.SetToMin(max_dialog_size);
  return preferred_size;
}

gfx::Size ProfilePickerView::GetMinimumSize() const {
  // On small screens, the preferred size may be smaller than the picker
  // minimum size. In that case there will be scrollbars on the picker.
  gfx::Size minimum_size = GetPreferredSize();
  minimum_size.SetToMin(ProfilePickerUI::GetMinimumSize());
  return minimum_size;
}

bool ProfilePickerView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const auto& iter = accelerator_table_.find(accelerator);
  DCHECK(iter != accelerator_table_.end());
  int command_id = iter->second;
  switch (command_id) {
    case IDC_CLOSE_TAB:
    case IDC_CLOSE_WINDOW:
      // kEscKeyPressed is used although that shortcut is disabled (this is
      // Ctrl-Shift-W instead).
      GetWidget()->CloseWithReason(views::Widget::ClosedReason::kEscKeyPressed);
      break;
    case IDC_EXIT:
      chrome::AttemptUserExit();
      break;
    case IDC_FULLSCREEN:
      GetWidget()->SetFullscreen(!GetWidget()->IsFullscreen());
      break;
    case IDC_MINIMIZE_WINDOW:
      GetWidget()->Minimize();
      break;
    case IDC_BACK: {
      NavigateBack();
      break;
    }
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    // Always reload bypassing cache.
    case IDC_RELOAD:
    case IDC_RELOAD_BYPASSING_CACHE:
    case IDC_RELOAD_CLEARING_CACHE:
      DCHECK(initialized_steps_.contains(current_step_));
      initialized_steps_.at(current_step_)->OnReloadRequested();
      break;

#endif
    default:
      NOTREACHED() << "Unexpected command_id: " << command_id;
      break;
  }

  return true;
}

void ProfilePickerView::BuildLayout() {
  SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical)
      .SetMainAxisAlignment(views::LayoutAlignment::kStart)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetDefault(
          views::kFlexBehaviorKey,
          views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                                   views::MaximumFlexSizeRule::kUnbounded));

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  auto toolbar = std::make_unique<ProfilePickerDiceSignInToolbar>();
  toolbar_ = AddChildView(std::move(toolbar));
  // Toolbar gets built and set visible once we it's needed for the Dice signin.
  SetNativeToolbarVisible(false);
#endif

  auto web_view = std::make_unique<views::WebView>();
  web_view->set_allow_accelerators(true);
  web_view_ = AddChildView(std::move(web_view));
}

void ProfilePickerView::ShowScreenFinished(
    content::WebContents* contents,
    base::OnceClosure navigation_finished_closure) {
  // Stop observing for this (or any previous) navigation.
  if (show_screen_finished_observer_)
    show_screen_finished_observer_.reset();

  web_view_->SetWebContents(contents);
  contents->Focus();

  if (navigation_finished_closure)
    std::move(navigation_finished_closure).Run();
}

void ProfilePickerView::SwitchToStep(
    Step step,
    bool reset_state,
    base::OnceClosure pop_step_callback,
    base::OnceCallback<void(bool)> step_switch_finished_callback) {
  DCHECK_NE(Step::kUnknown, step);
  DCHECK_NE(current_step_, step);

  auto* new_step_controller = initialized_steps_.at(step).get();
  DCHECK(new_step_controller);
  new_step_controller->set_pop_step_callback(std::move(pop_step_callback));
  new_step_controller->Show(std::move(step_switch_finished_callback),
                            reset_state);

  if (initialized_steps_.contains(current_step_)) {
    initialized_steps_.at(current_step_)->OnHidden();
  }

  current_step_ = step;
}

void ProfilePickerView::NavigateBack() {
  DCHECK(initialized_steps_.contains(current_step_));
  initialized_steps_.at(current_step_)->OnNavigateBackRequested();
}

void ProfilePickerView::ConfigureAccelerators() {
  const std::vector<AcceleratorMapping> accelerator_list(GetAcceleratorList());
  for (const auto& entry : accelerator_list) {
    if (!base::Contains(kSupportedAcceleratorCommands, entry.command_id))
      continue;
    ui::Accelerator accelerator(entry.keycode, entry.modifiers);
    accelerator_table_[accelerator] = entry.command_id;
    AddAccelerator(accelerator);
  }

#if BUILDFLAG(IS_MAC)
  // Check Mac-specific accelerators. Note: Chrome does not support dynamic or
  // user-configured accelerators on Mac. Default static accelerators are used
  // instead.
  for (int command_id : kSupportedAcceleratorCommands) {
    ui::Accelerator accelerator;
    bool mac_accelerator_found =
        GetDefaultMacAcceleratorForCommandId(command_id, &accelerator);
    if (mac_accelerator_found) {
      accelerator_table_[accelerator] = command_id;
      AddAccelerator(accelerator);
    }
  }
#endif  // BUILDFLAG(IS_MAC)
}

void ProfilePickerView::ShowDialog(content::BrowserContext* browser_context,
                                   const GURL& url,
                                   const base::FilePath& profile_path) {
  gfx::NativeView parent = GetWidget()->GetNativeView();
  dialog_host_.ShowDialog(browser_context, url, profile_path, parent);
}

void ProfilePickerView::HideDialog() {
  dialog_host_.HideDialog();
}

base::FilePath ProfilePickerView::GetForceSigninProfilePath() const {
  return dialog_host_.GetForceSigninProfilePath();
}

GURL ProfilePickerView::GetOnSelectProfileTargetUrl() const {
  return params_.on_select_profile_target_url();
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
// static
void ProfilePicker::NotifyAccountSelected(const std::string& gaia_id) {
  if (!g_profile_picker_view)
    return;
  g_profile_picker_view->NotifyAccountSelected(gaia_id);
}
#endif

BEGIN_METADATA(ProfilePickerView, views::WidgetDelegateView)
ADD_READONLY_PROPERTY_METADATA(base::FilePath, ForceSigninProfilePath)
END_METADATA