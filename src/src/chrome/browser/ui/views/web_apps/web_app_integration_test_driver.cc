// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/web_apps/web_app_integration_test_driver.h"

#include <codecvt>
#include <ostream>
#include <string>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/memory/raw_ptr.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/strings/pattern.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/apps/app_service/app_icon/app_icon_source.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/intent_helper/intent_picker_features.h"
#include "chrome/browser/banners/test_app_banner_manager_desktop.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/intent_picker_tab_helper.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/startup/web_app_startup_utils.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/location_bar/custom_tab_bar_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/web_apps/file_handler_launch_dialog_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/ui/web_applications/web_app_dialog_utils.h"
#include "chrome/browser/ui/web_applications/web_app_launch_utils.h"
#include "chrome/browser/ui/web_applications/web_app_menu_model.h"
#include "chrome/browser/ui/webui/app_management/app_management_page_handler.h"
#include "chrome/browser/ui/webui/app_settings/web_app_settings_ui.h"
#include "chrome/browser/ui/webui/ntp/app_launcher_handler.h"
#include "chrome/browser/ui/webui/web_app_internals/web_app_internals_source.h"
#include "chrome/browser/web_applications/app_service/web_app_publisher_helper.h"
#include "chrome/browser/web_applications/commands/run_on_os_login_command.h"
#include "chrome/browser/web_applications/manifest_update_manager.h"
#include "chrome/browser/web_applications/os_integration/web_app_file_handler_registration.h"
#include "chrome/browser/web_applications/os_integration/web_app_shortcut.h"
#include "chrome/browser/web_applications/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/policy/web_app_policy_manager.h"
#include "chrome/browser/web_applications/test/app_registry_cache_waiter.h"
#include "chrome/browser/web_applications/test/web_app_icon_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_install_test_utils.h"
#include "chrome/browser/web_applications/test/web_app_test_observers.h"
#include "chrome/browser/web_applications/user_display_mode.h"
#include "chrome/browser/web_applications/web_app_command_manager.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_icon_generator.h"
#include "chrome/browser/web_applications/web_app_id.h"
#include "chrome/browser/web_applications/web_app_install_finalizer.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/browser/web_applications/web_app_sync_bridge.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/services/app_service/public/mojom/types.mojom.h"
#include "components/webapps/browser/features.h"
#include "components/webapps/browser/install_result_code.h"
#include "components/webapps/browser/installable/installable_metrics.h"
#include "components/webapps/browser/uninstall_result_code.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "content/public/test/test_web_ui.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/network_switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom-shared.h"
#include "third_party/re2/src/re2/re2.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/dialog_test.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget.h"
#include "ui/webui/resources/cr_components/app_management/app_management.mojom-forward.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/ui/views/apps/app_dialog/app_uninstall_dialog_view.h"
#include "components/services/app_service/public/mojom/types.mojom-shared.h"
#else
#include "chrome/browser/ui/webui/ntp/app_launcher_handler.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#endif

#if BUILDFLAG(IS_MAC)
#include <ImageIO/ImageIO.h>
#include "chrome/browser/apps/app_shim/app_shim_manager_mac.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/web_applications/app_shim_registry_mac.h"
#include "net/base/filename_util.h"
#include "skia/ext/skia_utils_mac.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "base/test/test_reg_util_win.h"
#include "base/win/shortcut.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/web_applications/os_integration/web_app_handler_registration_utils_win.h"
#include "chrome/installer/util/shell_util.h"
#endif

namespace web_app::integration_tests {

namespace {

using ::testing::Eq;

Site InstallableSiteToSite(InstallableSite site) {
  switch (site) {
    case InstallableSite::kStandalone:
      return Site::kStandalone;
    case InstallableSite::kMinimalUi:
      return Site::kMinimalUi;
    case InstallableSite::kStandaloneNestedA:
      return Site::kStandaloneNestedA;
    case InstallableSite::kStandaloneNestedB:
      return Site::kStandaloneNestedB;
    case InstallableSite::kWco:
      return Site::kWco;
    case InstallableSite::kIsolated:
      return Site::kIsolated;
    case InstallableSite::kFileHandler:
      return Site::kFileHandler;
    case InstallableSite::kNoServiceWorker:
      return Site::kNoServiceWorker;
    case InstallableSite::kNotInstalled:
      return Site::kNotInstalled;
  }
}

// Flushes the shortcuts tasks, which seem to sometimes still hang around after
// our tasks are done.
// TODO(crbug.com/1273568): Investigate the true source of flakiness instead of
// papering over it here.
void FlushShortcutTasks() {
  // Execute the UI thread task runner before and after the shortcut task runner
  // to ensure that tasks get to the shortcut runner, and then any scheduled
  // replies on the UI thread get run.
  {
    base::RunLoop loop;
    content::GetUIThreadTaskRunner({})->PostTask(FROM_HERE, loop.QuitClosure());
    loop.Run();
  }
  {
    base::RunLoop loop;
    internals::GetShortcutIOTaskRunner()->PostTask(FROM_HERE,
                                                   loop.QuitClosure());
    loop.Run();
  }
  {
    base::RunLoop loop;
    content::GetUIThreadTaskRunner({})->PostTask(FROM_HERE, loop.QuitClosure());
    loop.Run();
  }
}

struct SiteConfig {
  std::string relative_scope_url;
  std::string relative_start_url;
  std::string relative_manifest_id;
  std::string app_name;
  std::u16string wco_not_enabled_title;
  SkColor icon_color;
  base::flat_set<std::string> alternate_titles;
};

base::flat_map<Site, SiteConfig> g_site_configs = {
    {Site::kStandalone,
     {.relative_scope_url = "/webapps_integration/standalone/",
      .relative_start_url = "/webapps_integration/standalone/basic.html",
      .relative_manifest_id = "webapps_integration/standalone/basic.html",
      .app_name = "Site A",
      // WCO disabled is the defaulting state so the title when disabled should
      // match with the app's name.
      .wco_not_enabled_title = u"Site A",
      .icon_color = SK_ColorGREEN,
      .alternate_titles = {"Site A - Updated name"}}},
    {Site::kMinimalUi,
     {.relative_scope_url = "/webapps_integration/minimal_ui/",
      .relative_start_url = "/webapps_integration/minimal_ui/basic.html",
      .relative_manifest_id = "webapps_integration/minimal_ui/basic.html",
      .app_name = "Site B",
      .wco_not_enabled_title = u"Site B",
      .icon_color = SK_ColorBLACK}},
    {Site::kNotPromotable,
     {.relative_scope_url = "/webapps_integration/not_promotable/",
      .relative_start_url = "/webapps_integration/not_promotable/basic.html",
      .relative_manifest_id = "webapps_integration/not_promotable/basic.html",
      .app_name = "Site C",
      .wco_not_enabled_title = u"Site C",
      .icon_color = SK_ColorTRANSPARENT}},
    {Site::kWco,
     {.relative_scope_url = "/webapps_integration/wco/",
      .relative_start_url = "/webapps_integration/wco/basic.html",
      .relative_manifest_id = "webapps_integration/wco/basic.html",
      .app_name = "Site WCO",
      .wco_not_enabled_title = u"Site WCO",
      .icon_color = SK_ColorGREEN}},
    {Site::kStandaloneNestedA,
     {.relative_scope_url = "/webapps_integration/standalone/foo/",
      .relative_start_url = "/webapps_integration/standalone/foo/basic.html",
      .relative_manifest_id = "webapps_integration/standalone/foo/basic.html",
      .app_name = "Site A Foo",
      .wco_not_enabled_title = u"Site A Foo",
      .icon_color = SK_ColorGREEN}},
    {Site::kStandaloneNestedB,
     {.relative_scope_url = "/webapps_integration/standalone/bar/",
      .relative_start_url = "/webapps_integration/standalone/bar/basic.html",
      .relative_manifest_id = "webapps_integration/standalone/bar/basic.html",
      .app_name = "Site A Bar",
      .wco_not_enabled_title = u"Site A Bar",
      .icon_color = SK_ColorGREEN}},
    {Site::kIsolated,
     {.relative_scope_url = "/webapps_integration/isolated_app/",
      // This file actually lives in /webapps_integration/isolated_app/. We
      // serve this directory as root in a special test server to allow the
      // isolated app to live at the root scope.
      .relative_start_url = "/basic.html",
      // same note for this file
      .relative_manifest_id = "basic.html",
      .app_name = "Isolated App",
      .wco_not_enabled_title = u"Isolated App",
      .icon_color = SK_ColorGREEN}},
    {Site::kFileHandler,
     {.relative_scope_url = "/webapps_integration/file_handler/",
      .relative_start_url = "/webapps_integration/file_handler/basic.html",
      .relative_manifest_id = "webapps_integration/file_handler/basic.html",
      .app_name = "File Handler",
      .wco_not_enabled_title = u"File Handler",
      .icon_color = SK_ColorBLACK}},
    {Site::kNoServiceWorker,
     {.relative_scope_url = "/webapps_integration/site_no_service_worker/",
      .relative_start_url =
          "/webapps_integration/site_no_service_worker/basic.html",
      .relative_manifest_id =
          "webapps_integration/site_no_service_worker/basic.html",
      .app_name = "Site NoServiceWorker",
      .wco_not_enabled_title = u"Site NoServiceWorker",
      .icon_color = SK_ColorGREEN}},
    {Site::kNotInstalled,
     {.relative_scope_url = "/webapps_integration/not_installed/",
      .relative_start_url = "/webapps_integration/not_installed/basic.html",
      .relative_manifest_id = "webapps_integration/not_installed/basic.html",
      .app_name = "Not Installed",
      .wco_not_enabled_title = u"Not Installed",
      .icon_color = SK_ColorBLUE}},
};

struct DisplayConfig {
  std::string manifest_url_param;
};

base::flat_map<Display, DisplayConfig> g_display_configs = {
    {Display::kBrowser,
     {.manifest_url_param = "?manifest=manifest_browser.json"}},
    {Display::kMinimalUi,
     {.manifest_url_param = "?manifest=manifest_minimal_ui.json"}},
    {Display::kStandalone, {.manifest_url_param = "?manifest=basic.json"}},
    {Display::kWco,
     {.manifest_url_param =
          "?manifest=manifest_window_controls_overlay.json"}}};

struct ScopeConfig {
  std::string manifest_url_param;
};

base::flat_map<Site, ScopeConfig> g_scope_configs = {
    {Site::kStandalone,
     {.manifest_url_param = "?manifest=manifest_scope_Standalone.json"}}};

ScopeConfig GetScopeUpdateConfiguration(Site scope) {
  CHECK(base::Contains(g_scope_configs, scope));
  return g_scope_configs.find(scope)->second;
}

DisplayConfig GetDisplayUpdateConfiguration(Display display) {
  CHECK(base::Contains(g_display_configs, display));
  return g_display_configs.find(display)->second;
}

SiteConfig GetSiteConfiguration(Site site) {
  CHECK(base::Contains(g_site_configs, site));
  return g_site_configs.find(site)->second;
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
SiteConfig GetSiteConfigurationFromAppName(const std::string& app_name) {
  SiteConfig config;
  bool is_app_found = false;
  for (auto const& [site, check_config] : g_site_configs) {
    if (check_config.app_name == app_name ||
        base::Contains(check_config.alternate_titles, app_name)) {
      config = check_config;
      is_app_found = true;
      break;
    }
  }
  CHECK(is_app_found) << "Could not find " << app_name;
  return config;
}
#endif

#if !BUILDFLAG(IS_CHROMEOS)
class TestAppLauncherHandler : public AppLauncherHandler {
 public:
  TestAppLauncherHandler(extensions::ExtensionService* extension_service,
                         WebAppProvider* provider,
                         content::TestWebUI* test_web_ui)
      : AppLauncherHandler(extension_service, provider) {
    DCHECK(test_web_ui->GetWebContents());
    DCHECK(test_web_ui->GetWebContents()->GetBrowserContext());
    set_web_ui(test_web_ui);
  }
};
#endif

class BrowserAddedWaiter final : public BrowserListObserver {
 public:
  BrowserAddedWaiter() { BrowserList::AddObserver(this); }
  ~BrowserAddedWaiter() override { BrowserList::RemoveObserver(this); }

  void Wait() { run_loop_.Run(); }

  // BrowserListObserver
  void OnBrowserAdded(Browser* browser) override {
    browser_added_ = browser;
    BrowserList::RemoveObserver(this);
    // Post a task to ensure the Remove event has been dispatched to all
    // observers.
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  run_loop_.QuitClosure());
  }
  Browser* browser_added() const { return browser_added_; }

 private:
  base::RunLoop run_loop_;
  raw_ptr<Browser> browser_added_ = nullptr;
};

bool AreAppBrowsersOpen(const Profile* profile, const AppId& app_id) {
  BrowserList* browser_list = BrowserList::GetInstance();
  std::vector<Browser*> app_browsers;
  for (Browser* browser : *browser_list) {
    if (browser->profile() != profile)
      continue;
    if (AppBrowserController::IsForWebApp(browser, app_id))
      return true;
  }
  return false;
}

class UninstallCompleteWaiter final : public BrowserListObserver,
                                      public WebAppInstallManagerObserver {
 public:
  explicit UninstallCompleteWaiter(Profile* profile, const AppId& app_id)
      : profile_(profile),
        app_id_(app_id),
        app_unregistration_waiter_(profile,
                                   app_id,
                                   apps::Readiness::kUninstalledByUser) {
    BrowserList::AddObserver(this);
    WebAppProvider* provider = WebAppProvider::GetForTest(profile);
    observation_.Observe(&provider->install_manager());
    uninstall_complete_ = provider->registrar().GetAppById(app_id) == nullptr;
    MaybeFinishWaiting();
  }

  ~UninstallCompleteWaiter() override {
    BrowserList::RemoveObserver(this);
    observation_.Reset();
  }

  void Wait() {
    app_unregistration_waiter_.Await();
    run_loop_.Run();
  }

  // BrowserListObserver
  void OnBrowserRemoved(Browser* browser) override { MaybeFinishWaiting(); }

  // WebAppInstallManagerObserver
  void OnWebAppUninstalled(const AppId& app_id) override {
    if (app_id != app_id_)
      return;
    uninstall_complete_ = true;
    MaybeFinishWaiting();
  }

  void MaybeFinishWaiting() {
    if (!uninstall_complete_)
      return;
    if (AreAppBrowsersOpen(profile_, app_id_))
      return;

    BrowserList::RemoveObserver(this);
    observation_.Reset();
    // Post a task to ensure the Remove event has been dispatched to all
    // observers.
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  run_loop_.QuitClosure());
  }

 private:
  const Profile* profile_;
  const AppId app_id_;
  bool uninstall_complete_ = false;
  base::RunLoop run_loop_;
  AppReadinessWaiter app_unregistration_waiter_;

  base::ScopedObservation<WebAppInstallManager, WebAppInstallManagerObserver>
      observation_{this};
};

Browser* GetBrowserForAppId(const AppId& app_id) {
  BrowserList* browser_list = BrowserList::GetInstance();
  for (Browser* browser : *browser_list) {
    if (AppBrowserController::IsForWebApp(browser, app_id))
      return browser;
  }
  return nullptr;
}

#if BUILDFLAG(IS_WIN)
std::vector<std::wstring> GetFileExtensionsForProgId(
    const std::wstring& file_handler_prog_id) {
  const std::wstring prog_id_path =
      base::StrCat({ShellUtil::kRegClasses, L"\\", file_handler_prog_id});

  // Get list of handled file extensions from value FileExtensions at
  // HKEY_CURRENT_USER\Software\Classes\<file_handler_prog_id>.
  base::win::RegKey file_extensions_key(HKEY_CURRENT_USER, prog_id_path.c_str(),
                                        KEY_QUERY_VALUE);
  std::wstring handled_file_extensions;
  EXPECT_EQ(file_extensions_key.ReadValue(L"FileExtensions",
                                          &handled_file_extensions),
            ERROR_SUCCESS);
  return base::SplitString(handled_file_extensions, std::wstring(L";"),
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

base::FilePath GetShortcutProfile(base::FilePath shortcut_path) {
  base::FilePath shortcut_profile;
  std::wstring cmd_line_string;
  if (base::win::ResolveShortcut(shortcut_path, nullptr, &cmd_line_string)) {
    base::CommandLine shortcut_cmd_line =
        base::CommandLine::FromString(L"program " + cmd_line_string);
    shortcut_profile =
        shortcut_cmd_line.GetSwitchValuePath(switches::kProfileDirectory);
  }
  return shortcut_profile;
}
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
bool IconManagerCheckIconTopLeftColor(WebAppIconManager& icon_manager,
                                      const AppId& app_id,
                                      std::vector<int> sizes_px,
                                      SkColor expected_icon_pixel_color) {
  bool icons_exist = icon_manager.HasIcons(app_id, IconPurpose::ANY, sizes_px);
  if (icons_exist) {
    for (int size_px : sizes_px) {
      SkColor icon_pixel_color =
          IconManagerReadAppIconPixel(icon_manager, app_id, size_px, 0, 0);
      if (icon_pixel_color != expected_icon_pixel_color)
        return false;
    }
    return true;
  }
  return false;
}
#endif

absl::optional<ProfileState> GetStateForProfile(StateSnapshot* state_snapshot,
                                                Profile* profile) {
  DCHECK(state_snapshot);
  DCHECK(profile);
  auto it = state_snapshot->profiles.find(profile);
  return it == state_snapshot->profiles.end()
             ? absl::nullopt
             : absl::make_optional<ProfileState>(it->second);
}

absl::optional<BrowserState> GetStateForBrowser(StateSnapshot* state_snapshot,
                                                Profile* profile,
                                                Browser* browser) {
  absl::optional<ProfileState> profile_state =
      GetStateForProfile(state_snapshot, profile);
  if (!profile_state) {
    return absl::nullopt;
  }

  auto it = profile_state->browsers.find(browser);
  return it == profile_state->browsers.end()
             ? absl::nullopt
             : absl::make_optional<BrowserState>(it->second);
}

absl::optional<TabState> GetStateForActiveTab(BrowserState browser_state) {
  if (!browser_state.active_tab) {
    return absl::nullopt;
  }

  auto it = browser_state.tabs.find(browser_state.active_tab);
  DCHECK(it != browser_state.tabs.end());
  return absl::make_optional<TabState>(it->second);
}

absl::optional<AppState> GetStateForAppId(StateSnapshot* state_snapshot,
                                          Profile* profile,
                                          const web_app::AppId& id) {
  absl::optional<ProfileState> profile_state =
      GetStateForProfile(state_snapshot, profile);
  if (!profile_state) {
    return absl::nullopt;
  }

  auto it = profile_state->apps.find(id);
  return it == profile_state->apps.end()
             ? absl::nullopt
             : absl::make_optional<AppState>(it->second);
}

#if !BUILDFLAG(IS_CHROMEOS)
AppManagementPageHandler CreateAppManagementPageHandler(Profile* profile) {
  mojo::PendingReceiver<app_management::mojom::Page> page;
  mojo::Remote<app_management::mojom::PageHandler> handler;
  static auto delegate =
      WebAppSettingsUI::CreateAppManagementPageHandlerDelegate(profile);
  return AppManagementPageHandler(handler.BindNewPipeAndPassReceiver(),
                                  page.InitWithNewPipeAndPassRemote(), profile,
                                  *delegate);
}
#endif

void ActivateBrowserAndWait(Browser* browser) {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  DCHECK(browser);
  DCHECK(browser->window());
  auto waiter = ui_test_utils::BrowserActivationWaiter(browser);
  browser->window()->Activate();
  waiter.WaitForActivation();
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)
}

}  // anonymous namespace

BrowserState::BrowserState(
    Browser* browser_ptr,
    base::flat_map<content::WebContents*, TabState> tab_state,
    content::WebContents* active_web_contents,
    const AppId& app_id,
    bool launch_icon_visible)
    : browser(browser_ptr),
      tabs(std::move(tab_state)),
      active_tab(active_web_contents),
      app_id(app_id),
      launch_icon_shown(launch_icon_visible) {}
BrowserState::~BrowserState() = default;
BrowserState::BrowserState(const BrowserState&) = default;
bool BrowserState::operator==(const BrowserState& other) const {
  return browser == other.browser && tabs == other.tabs &&
         active_tab == other.active_tab && app_id == other.app_id &&
         launch_icon_shown == other.launch_icon_shown;
}

AppState::AppState(web_app::AppId app_id,
                   std::string app_name,
                   GURL app_scope,
                   apps::RunOnOsLoginMode run_on_os_login_mode,
                   blink::mojom::DisplayMode effective_display_mode,
                   absl::optional<UserDisplayMode> user_display_mode,
                   std::string manifest_launcher_icon_filename,
                   bool installed_locally,
                   bool shortcut_created,
                   bool is_isolated)
    : id(std::move(app_id)),
      name(std::move(app_name)),
      scope(std::move(app_scope)),
      run_on_os_login_mode(run_on_os_login_mode),
      effective_display_mode(effective_display_mode),
      user_display_mode(user_display_mode),
      manifest_launcher_icon_filename(
          std::move(manifest_launcher_icon_filename)),
      is_installed_locally(installed_locally),
      is_shortcut_created(shortcut_created),
      is_isolated(is_isolated) {}
AppState::~AppState() = default;
AppState::AppState(const AppState&) = default;
bool AppState::operator==(const AppState& other) const {
  return id == other.id && name == other.name && scope == other.scope &&
         run_on_os_login_mode == other.run_on_os_login_mode &&
         effective_display_mode == other.effective_display_mode &&
         user_display_mode == other.user_display_mode &&
         manifest_launcher_icon_filename ==
             other.manifest_launcher_icon_filename &&
         is_installed_locally == other.is_installed_locally &&
         is_shortcut_created == other.is_shortcut_created &&
         is_isolated == other.is_isolated;
}

ProfileState::ProfileState(base::flat_map<Browser*, BrowserState> browser_state,
                           base::flat_map<web_app::AppId, AppState> app_state)
    : browsers(std::move(browser_state)), apps(std::move(app_state)) {}
ProfileState::~ProfileState() = default;
ProfileState::ProfileState(const ProfileState&) = default;
bool ProfileState::operator==(const ProfileState& other) const {
  return browsers == other.browsers && apps == other.apps;
}

StateSnapshot::StateSnapshot(
    base::flat_map<Profile*, ProfileState> profile_state)
    : profiles(std::move(profile_state)) {}
StateSnapshot::~StateSnapshot() = default;
StateSnapshot::StateSnapshot(const StateSnapshot&) = default;
bool StateSnapshot::operator==(const StateSnapshot& other) const {
  return profiles == other.profiles;
}

std::ostream& operator<<(std::ostream& os, const StateSnapshot& snapshot) {
  base::Value::Dict root;
  base::Value::Dict& profiles_dict = *root.EnsureDict("profiles");
  for (const auto& profile_pair : snapshot.profiles) {
    base::Value::Dict profile_dict;

    base::Value::Dict browsers_dict;
    const ProfileState& profile = profile_pair.second;
    for (const auto& browser_pair : profile.browsers) {
      base::Value::Dict browser_dict;
      const BrowserState& browser = browser_pair.second;

      browser_dict.Set("browser", base::StringPrintf("%p", browser.browser));

      base::Value::Dict tab_dicts;
      for (const auto& tab_pair : browser.tabs) {
        base::Value::Dict tab_dict;
        const TabState& tab = tab_pair.second;
        tab_dict.Set("url", tab.url.spec());
        tab_dicts.Set(base::StringPrintf("%p", tab_pair.first),
                      std::move(tab_dict));
      }
      browser_dict.Set("tabs", std::move(tab_dicts));
      browser_dict.Set("active_tab",
                       base::StringPrintf("%p", browser.active_tab));
      browser_dict.Set("app_id", browser.app_id);
      browser_dict.Set("launch_icon_shown", browser.launch_icon_shown);

      browsers_dict.Set(base::StringPrintf("%p", browser_pair.first),
                        std::move(browser_dict));
    }
    base::Value::Dict app_dicts;
    for (const auto& app_pair : profile.apps) {
      base::Value::Dict app_dict;
      const AppState& app = app_pair.second;

      app_dict.Set("id", app.id);
      app_dict.Set("name", app.name);
      app_dict.Set("effective_display_mode",
                   static_cast<int>(app.effective_display_mode));
      app_dict.Set("user_display_mode",
                   static_cast<int>(app.effective_display_mode));
      app_dict.Set("manifest_launcher_icon_filename",
                   app.manifest_launcher_icon_filename);
      app_dict.Set("is_installed_locally", app.is_installed_locally);
      app_dict.Set("is_shortcut_created", app.is_shortcut_created);
      app_dict.Set("is_isolated", app.is_isolated);

      app_dicts.Set(app_pair.first, std::move(app_dict));
    }

    profile_dict.Set("browsers", std::move(browsers_dict));
    profile_dict.Set("apps", std::move(app_dicts));
    profiles_dict.Set(base::StringPrintf("%p", profile_pair.first),
                      std::move(profile_dict));
  }
  os << root.DebugString();
  return os;
}

WebAppIntegrationTestDriver::WebAppIntegrationTestDriver(TestDelegate* delegate)
    : delegate_(delegate) {}

WebAppIntegrationTestDriver::~WebAppIntegrationTestDriver() = default;

void WebAppIntegrationTestDriver::SetUp() {
  isolated_app_test_server_ = std::make_unique<net::EmbeddedTestServer>();
  isolated_app_test_server_->AddDefaultHandlers(base::FilePath(
      FILE_PATH_LITERAL("chrome/test/data/webapps_integration/isolated_app/")));
  CHECK(isolated_app_test_server_->Start());

  webapps::TestAppBannerManagerDesktop::SetUp();
}

void WebAppIntegrationTestDriver::SetUpOnMainThread() {
  override_registration_ =
      ShortcutOverrideForTesting::OverrideForTesting(base::GetHomeDir());

  // Only support manifest updates on non-sync tests, as the current
  // infrastructure here only supports listening on one profile.
  if (!delegate_->IsSyncTest()) {
    observation_.Observe(&provider()->install_manager());
  }
  web_app::test::WaitUntilReady(
      web_app::WebAppProvider::GetForTest(browser()->profile()));
}

void WebAppIntegrationTestDriver::TearDownOnMainThread() {
  in_tear_down_ = true;
  LOG(INFO) << "TearDownOnMainThread: Start.";
  observation_.Reset();
  if (delegate_->IsSyncTest())
    SyncTurnOff();
  for (auto* profile : delegate_->GetAllProfiles()) {
    auto* provider = GetProviderForProfile(profile);
    std::vector<AppId> app_ids = provider->registrar().GetAppIds();
    for (auto& app_id : app_ids) {
      LOG(INFO) << "TearDownOnMainThread: Uninstalling " << app_id << ".";
      const WebApp* app = provider->registrar().GetAppById(app_id);
      if (app->IsPolicyInstalledApp())
        UninstallPolicyAppById(app_id);
      if (provider->registrar().IsInstalled(app_id)) {
        DCHECK(app->CanUserUninstallWebApp());
        UninstallCompleteWaiter uninstall_waiter(profile, app_id);
        base::RunLoop run_loop;
        provider->install_finalizer().UninstallWebApp(
            app_id, webapps::WebappUninstallSource::kAppsPage,
            base::BindLambdaForTesting([&](webapps::UninstallResultCode code) {
              EXPECT_EQ(code, webapps::UninstallResultCode::kSuccess);
              run_loop.Quit();
            }));
        run_loop.Run();
        uninstall_waiter.Wait();
      }
      LOG(INFO) << "TearDownOnMainThread: Uninstall complete.";
    }
    // TODO(crbug.com/1273568): Investigate the true source of flakiness instead
    // of papering over it here.
    provider->command_manager().AwaitAllCommandsCompleteForTesting();
    FlushShortcutTasks();
  }
  LOG(INFO) << "TearDownOnMainThread: Deleting dangling shortcuts.";
// TODO(crbug.com/1273568): Investigate the true source of flakiness instead of
// papering over it here.
#if BUILDFLAG(IS_WIN)
  if (override_registration_->shortcut_override->desktop.IsValid())
    ASSERT_TRUE(override_registration_->shortcut_override->desktop.Delete());
  if (override_registration_->shortcut_override->application_menu.IsValid())
    ASSERT_TRUE(
        override_registration_->shortcut_override->application_menu.Delete());
#elif BUILDFLAG(IS_MAC)
  if (override_registration_->shortcut_override->chrome_apps_folder.IsValid())
    ASSERT_TRUE(
        override_registration_->shortcut_override->chrome_apps_folder.Delete());
#elif BUILDFLAG(IS_LINUX)
  if (override_registration_->shortcut_override->desktop.IsValid())
    ASSERT_TRUE(override_registration_->shortcut_override->desktop.Delete());
#endif

  if (isolated_app_test_server_->Started()) {
    CHECK(isolated_app_test_server_->ShutdownAndWaitUntilComplete());
  }
  LOG(INFO)
      << "TearDownOnMainThread: Destroying shortcut override and waiting.";
  override_registration_.reset();

  LOG(INFO) << "TearDownOnMainThread: Complete.";

  // Print debug information if there was a failure.
  if (testing::Test::HasFailure()) {
    for (auto* profile : delegate_->GetAllProfiles()) {
      base::RunLoop debug_info_loop;
      WebAppInternalsSource::BuildWebAppInternalsJson(
          profile, base::BindLambdaForTesting([&](base::Value debug_info) {
            LOG(INFO) << "chrome://web-app-internals for profile "
                      << profile->GetDebugName() << ":";
            LOG(INFO) << debug_info.DebugString();
            debug_info_loop.Quit();
          }));
      debug_info_loop.Run();
    }
  }
}

void WebAppIntegrationTestDriver::AcceptAppIdUpdateDialog() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;

  views::Widget* widget = app_id_update_dialog_waiter_->WaitIfNeededAndGet();
  ASSERT_TRUE(widget != nullptr);
  views::test::AcceptDialog(widget);

  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::AwaitManifestUpdate(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id));
  if (!previous_manifest_updates_.contains(app_id)) {
    waiting_for_update_id_ = app_id;
    waiting_for_update_run_loop_ = std::make_unique<base::RunLoop>();
    Browser* browser = GetBrowserForAppId(app_id);
    while (browser != nullptr) {
      delegate_->CloseBrowserSynchronously(browser);
      browser = GetBrowserForAppId(app_id);
    }
    waiting_for_update_run_loop_->Run();
    waiting_for_update_run_loop_.reset();
  }
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::CloseCustomToolbar() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());
  content::WebContents* web_contents = app_view->GetActiveWebContents();
  content::TestNavigationObserver nav_observer(web_contents);
  EXPECT_TRUE(app_view->toolbar()
                  ->custom_tab_bar()
                  ->close_button_for_testing()
                  ->GetVisible());
  app_view->toolbar()->custom_tab_bar()->GoBackToAppForTesting();
  nav_observer.Wait();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ClosePwa() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser()) << "No current app browser";
  app_browser()->window()->Close();
  ui_test_utils::WaitForBrowserToClose(app_browser());
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::DisableRunOnOsLogin(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  SetRunOnOsLoginMode(site, apps::RunOnOsLoginMode::kNotRun);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::EnableRunOnOsLogin(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  SetRunOnOsLoginMode(site, apps::RunOnOsLoginMode::kWindowed);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::CreateShortcut(Site site,
                                                 WindowOptions options) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  MaybeNavigateTabbedBrowserInScope(site);
  bool open_in_window = options == WindowOptions::kWindowed;
  chrome::SetAutoAcceptWebAppDialogForTesting(
      /*auto_accept=*/true,
      /*auto_open_in_window=*/open_in_window);
  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  BrowserAddedWaiter browser_added_waiter;
  CHECK(chrome::ExecuteCommand(browser(), IDC_CREATE_SHORTCUT));
  active_app_id_ = observer.Wait();
  chrome::SetAutoAcceptWebAppDialogForTesting(false, false);
  if (open_in_window) {
    browser_added_waiter.Wait();
    app_browser_ = browser_added_waiter.browser_added();
    ActivateBrowserAndWait(app_browser_);
  }
  AppReadinessWaiter(profile(), active_app_id_).Await();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallMenuOption(InstallableSite site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  MaybeNavigateTabbedBrowserInScope(InstallableSiteToSite(site));
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(/*auto_accept=*/true);
  BrowserAddedWaiter browser_added_waiter;
  WebAppTestInstallWithOsHooksObserver install_observer(profile());
  install_observer.BeginListening();
  CHECK(chrome::ExecuteCommand(browser(), IDC_INSTALL_PWA));
  browser_added_waiter.Wait();
  active_app_id_ = install_observer.Wait();
  app_browser_ = browser_added_waiter.browser_added();
  ActivateBrowserAndWait(app_browser_);
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(/*auto_accept=*/false);
  AppReadinessWaiter(profile(), active_app_id_).Await();
  AfterStateChangeAction();
}

#if !BUILDFLAG(IS_CHROMEOS)
void WebAppIntegrationTestDriver::InstallLocally(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  content::TestWebUI test_web_ui;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  DCHECK(web_contents);
  test_web_ui.set_web_contents(web_contents);
  TestAppLauncherHandler handler(/*extension_service=*/nullptr, provider(),
                                 &test_web_ui);
  base::Value::List web_app_ids;
  web_app_ids.Append(app_id);

  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  handler.HandleInstallAppLocally(web_app_ids);
  observer.Wait();
  AppReadinessWaiter(profile(), app_id).Await();
  AfterStateChangeAction();
}
#endif

void WebAppIntegrationTestDriver::InstallOmniboxIcon(InstallableSite site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  MaybeNavigateTabbedBrowserInScope(InstallableSiteToSite(site));
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(true);

  auto* app_banner_manager =
      webapps::TestAppBannerManagerDesktop::FromWebContents(
          GetCurrentTab(browser()));
  app_banner_manager->WaitForInstallableCheck();

  web_app::AppId app_id;
  base::RunLoop run_loop;
  web_app::SetInstalledCallbackForTesting(base::BindLambdaForTesting(
      [&app_id, &run_loop](const web_app::AppId& installed_app_id,
                           webapps::InstallResultCode code) {
        app_id = installed_app_id;
        run_loop.Quit();
      }));

  BrowserAddedWaiter browser_added_waiter;
  ASSERT_TRUE(pwa_install_view()->GetVisible());
  WebAppTestInstallWithOsHooksObserver install_observer(profile());
  install_observer.BeginListening();
  pwa_install_view()->ExecuteForTesting();

  run_loop.Run();
  browser_added_waiter.Wait();
  active_app_id_ = install_observer.Wait();
  DCHECK_EQ(app_id, active_app_id_);
  app_browser_ = browser_added_waiter.browser_added();
  ActivateBrowserAndWait(app_browser_);
  chrome::SetAutoAcceptPWAInstallConfirmationForTesting(false);
  AppReadinessWaiter(profile(), active_app_id_).Await();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::InstallPolicyApp(Site site,
                                                   ShortcutOptions shortcut,
                                                   WindowOptions window) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  base::Value container = base::Value(window == WindowOptions::kWindowed
                                          ? kDefaultLaunchContainerWindowValue
                                          : kDefaultLaunchContainerTabValue);
  InstallPolicyAppInternal(
      site, std::move(container),
      /*create_shortcut=*/shortcut == ShortcutOptions::kWithShortcut);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::EnableWindowControlsOverlay(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());

  ASSERT_FALSE(app_view->IsWindowControlsOverlayEnabled());
  content::TitleWatcher title_watcher(
      app_view->GetActiveWebContents(),
      GetSiteConfiguration(site).wco_not_enabled_title + u": WCO Enabled");
  app_view->ToggleWindowControlsOverlayEnabled();
  std::ignore = title_watcher.WaitAndGetTitle();
  ASSERT_TRUE(app_view->IsWindowControlsOverlayEnabled());
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::DisableWindowControlsOverlay(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());

  ASSERT_TRUE(app_view->IsWindowControlsOverlayEnabled());
  content::TitleWatcher title_watcher(
      app_view->GetActiveWebContents(),
      GetSiteConfiguration(site).wco_not_enabled_title);
  app_view->ToggleWindowControlsOverlayEnabled();
  std::ignore = title_watcher.WaitAndGetTitle();
  ASSERT_FALSE(app_view->IsWindowControlsOverlayEnabled());
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ApplyRunOnOsLoginPolicyAllowed(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ApplyRunOnOsLoginPolicy(site, kAllowed);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ApplyRunOnOsLoginPolicyBlocked(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ApplyRunOnOsLoginPolicy(site, kBlocked);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ApplyRunOnOsLoginPolicyRunWindowed(
    Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ApplyRunOnOsLoginPolicy(site, kRunWindowed);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::RemoveRunOnOsLoginPolicy(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  GURL url = GetAppStartURL(site);
  {
    ListPrefUpdate updateList(profile()->GetPrefs(), prefs::kWebAppSettings);
    updateList->GetList().EraseIf([&](const base::Value& item) {
      return *item.GetDict().FindString(kManifestId) == url.spec();
    });
  }
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFileExpectDialog(
    Site site,
    FilesOptions files_options,
    AllowDenyOptions allow_deny,
    AskAgainOptions ask_again) {
  BeforeStateChangeAction(__FUNCTION__);
  AppId app_id = GetAppIdBySiteMode(site);
  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       "FileHandlerLaunchDialogView");
  FileHandlerLaunchDialogView::SetDefaultRememberSelectionForTesting(
      ask_again == AskAgainOptions::kRemember);
  std::vector<base::FilePath> file_paths = GetTestFilePaths(files_options);

  StartupBrowserCreator browser_creator;
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kAppId, app_id);
  for (auto file_path : file_paths) {
    command_line.AppendArgPath(file_path);
  }
  browser_creator.Start(command_line, profile()->GetPath(),
                        {profile(), StartupProfileMode::kBrowserWindow}, {});
  BrowserAddedWaiter browser_added_waiter;

  // Check the file handling dialog shows up.
  views::Widget* widget = waiter.WaitIfNeededAndGet();
  ASSERT_TRUE(widget != nullptr);

  views::test::WidgetDestroyedWaiter destroyed_waiter(widget);
  views::Widget::ClosedReason close_reason;
  if (allow_deny == AllowDenyOptions::kDeny) {
    close_reason = views::Widget::ClosedReason::kCancelButtonClicked;
    if (ask_again == AskAgainOptions::kRemember) {
      site_remember_deny_open_file.emplace(site);
    }
  } else {
    close_reason = views::Widget::ClosedReason::kAcceptButtonClicked;
  }
  // File handling dialog should be destroyed after choosing the action.
  widget->CloseWithReason(close_reason);
  destroyed_waiter.Wait();

  if (allow_deny == AllowDenyOptions::kAllow) {
    browser_added_waiter.Wait();
    app_browser_ = browser_added_waiter.browser_added();
    ActivateBrowserAndWait(app_browser_);
    EXPECT_EQ(app_browser()->app_controller()->app_id(), app_id);
  }
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFileExpectNoDialog(
    Site site,
    FilesOptions files_options) {
  BeforeStateChangeAction(__FUNCTION__);
  AppId app_id = GetAppIdBySiteMode(site);
  std::vector<base::FilePath> file_paths = GetTestFilePaths(files_options);
  BrowserAddedWaiter browser_added_waiter;
  base::RunLoop run_loop;

  web_app::startup::SetStartupDoneCallbackForTesting(run_loop.QuitClosure());
  StartupBrowserCreator browser_creator;
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kAppId, app_id);
  for (auto file_path : file_paths) {
    command_line.AppendArgPath(file_path);
  }
  browser_creator.Start(command_line, profile()->GetPath(),
                        {profile(), StartupProfileMode::kBrowserWindow}, {});
  run_loop.Run();

  // if the web app doesn't deny to open the file, wait for the app window.
  if (!base::Contains(site_remember_deny_open_file, site)) {
    app_browser_ = browser_added_waiter.browser_added();
    ActivateBrowserAndWait(app_browser_);
    EXPECT_EQ(app_browser()->app_controller()->app_id(), app_id);
  }
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFromChromeApps(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  ;
  WebAppRegistrar& app_registrar = provider()->registrar();
  DisplayMode display_mode = app_registrar.GetAppEffectiveDisplayMode(app_id);
  if (display_mode == blink::mojom::DisplayMode::kBrowser) {
    ui_test_utils::UrlLoadObserver url_observer(
        app_registrar.GetAppLaunchUrl(app_id),
        content::NotificationService::AllSources());
    LaunchBrowserForWebAppInTab(profile(), app_id);
    url_observer.Wait();
  } else {
    app_browser_ = LaunchWebAppBrowserAndWait(profile(), app_id);
    active_app_id_ = app_id;
    app_browser_ = GetBrowserForAppId(active_app_id_);
  }
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFromLaunchIcon(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  NavigateTabbedBrowserToSite(GetInScopeURL(site), NavigationMode::kNewTab);

  EXPECT_TRUE(intent_picker_view()->GetVisible());

  BrowserAddedWaiter browser_added_waiter;

  if (!IntentPickerBubbleView::intent_picker_bubble()) {
    views::NamedWidgetShownWaiter waiter(
        views::test::AnyWidgetTestPasskey{},
        IntentPickerBubbleView::kViewClassName);
    EXPECT_FALSE(IntentPickerBubbleView::intent_picker_bubble());
    intent_picker_view()->ExecuteForTesting();
    waiter.WaitIfNeededAndGet();
  }

  ASSERT_TRUE(IntentPickerBubbleView::intent_picker_bubble());
  EXPECT_TRUE(IntentPickerBubbleView::intent_picker_bubble()->GetVisible());

  IntentPickerBubbleView::intent_picker_bubble()->AcceptDialog();
  browser_added_waiter.Wait();
  app_browser_ = browser_added_waiter.browser_added();
  ActivateBrowserAndWait(app_browser_);
  ASSERT_TRUE(app_browser_->is_type_app());
  ASSERT_TRUE(AppBrowserController::IsForWebApp(app_browser_, app_id));
  active_app_id_ = app_browser()->app_controller()->app_id();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFromMenuOption(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  NavigateTabbedBrowserToSite(GetInScopeURL(site), NavigationMode::kNewTab);

  BrowserAddedWaiter browser_added_waiter;
  CHECK(chrome::ExecuteCommand(browser(), IDC_OPEN_IN_PWA_WINDOW));
  browser_added_waiter.Wait();
  app_browser_ = browser_added_waiter.browser_added();
  ActivateBrowserAndWait(app_browser_);
  active_app_id_ = app_id;

  ASSERT_TRUE(AppBrowserController::IsForWebApp(app_browser(), active_app_id_));
  EXPECT_EQ(app_browser()->app_controller()->app_id(), app_id);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::LaunchFromPlatformShortcut(Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  WebAppRegistrar& app_registrar = provider()->registrar();
  DisplayMode display_mode = app_registrar.GetAppEffectiveDisplayMode(app_id);
  bool is_open_in_app_browser =
      (display_mode != blink::mojom::DisplayMode::kBrowser);
  if (is_open_in_app_browser) {
    BrowserAddedWaiter browser_added_waiter;
    LaunchAppStartupBrowserCreator(app_id);
    browser_added_waiter.Wait();
    app_browser_ = browser_added_waiter.browser_added();
    ActivateBrowserAndWait(app_browser_);
    active_app_id_ = app_id;
    EXPECT_EQ(app_browser()->app_controller()->app_id(), app_id);
  } else {
    LaunchAppStartupBrowserCreator(app_id);
  }
  AfterStateChangeAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::OpenAppSettingsFromAppMenu(Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  Browser* app_browser = GetAppBrowserForSite(site);
  ASSERT_TRUE(app_browser);

  // Click App info from app browser.
  CHECK(chrome::ExecuteCommand(app_browser, IDC_WEB_APP_MENU_APP_INFO));

  content::WebContentsAddedObserver nav_observer;

  // Click settings from page info bubble.
  views::Widget* page_info_bubble =
      PageInfoBubbleView::GetPageInfoBubbleForTesting()->GetWidget();
  EXPECT_TRUE(page_info_bubble);

  views::View* settings_button = page_info_bubble->GetRootView()->GetViewByID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_SITE_SETTINGS);

  ui::AXActionData data;
  data.action = ax::mojom::Action::kDoDefault;
  settings_button->HandleAccessibleAction(data);

  // Wait for new web content to be created.
  nav_observer.GetWebContents();

  AfterStateChangeAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::OpenAppSettingsFromChromeApps(Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  ;

  content::TestWebUI test_web_ui;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  DCHECK(web_contents);
  test_web_ui.set_web_contents(web_contents);
  TestAppLauncherHandler handler(/*extension_service=*/nullptr, provider(),
                                 &test_web_ui);
  base::Value::List web_app_ids;
  web_app_ids.Append(app_id);
  content::WebContentsAddedObserver nav_observer;
  handler.HandleShowAppInfo(web_app_ids);
  // Wait for new web content to be created.
  nav_observer.GetWebContents();
  AfterStateChangeAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::CreateShortcutsFromList(Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  content::TestWebUI test_web_ui;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  DCHECK(web_contents);
  test_web_ui.set_web_contents(web_contents);
  TestAppLauncherHandler handler(/*extension_service=*/nullptr, provider(),
                                 &test_web_ui);
  base::Value::List web_app_ids;
  web_app_ids.Append(app_id);
#if BUILDFLAG(IS_MAC)
  base::RunLoop loop;
  handler.HandleCreateAppShortcut(loop.QuitClosure(), web_app_ids);
  loop.Run();
#else
  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       "CreateChromeApplicationShortcutView");
  handler.HandleCreateAppShortcut(base::DoNothing(), web_app_ids);
  FlushShortcutTasks();
  views::Widget* widget = waiter.WaitIfNeededAndGet();
  ASSERT_TRUE(widget != nullptr);
  views::test::AcceptDialog(widget);
#endif
  AfterStateChangeAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::DeletePlatformShortcut(Site site) {
  if (!before_state_change_action_state_ && !after_state_change_action_state_)
    return;
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  base::ScopedAllowBlockingForTesting allow_blocking;
  AppId app_id = GetAppIdBySiteMode(site);
  std::string app_name = provider()->registrar().GetAppShortName(app_id);
  if (app_name.empty()) {
    app_name = GetSiteConfiguration(site).app_name;
  }
#if BUILDFLAG(IS_WIN)
  base::FilePath desktop_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->desktop.GetPath(), app_name,
      app_id);
  ASSERT_TRUE(base::PathExists(desktop_shortcut_path));
  base::DeleteFile(desktop_shortcut_path);
  base::FilePath app_menu_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->application_menu.GetPath(),
      app_name, app_id);
  ASSERT_TRUE(base::PathExists(app_menu_shortcut_path));
  base::DeleteFile(app_menu_shortcut_path);
#elif BUILDFLAG(IS_MAC)
  base::FilePath app_folder_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->chrome_apps_folder.GetPath(),
      app_name, app_id);
  ASSERT_TRUE(base::PathExists(app_folder_shortcut_path));
  base::DeletePathRecursively(app_folder_shortcut_path);
#elif BUILDFLAG(IS_LINUX)
  base::FilePath desktop_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->desktop.GetPath(), app_name,
      app_id);
  LOG(INFO) << desktop_shortcut_path;
  ASSERT_TRUE(base::PathExists(desktop_shortcut_path));
  base::DeleteFile(desktop_shortcut_path);
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::CheckAppSettingsAppState(
    Profile* profile,
    const AppState& app_state) {
#if !BUILDFLAG(IS_CHROMEOS)
  auto app_management_page_handler = CreateAppManagementPageHandler(profile);

  app_management::mojom::AppPtr app;
  app_management_page_handler.GetApp(
      app_state.id,
      base::BindLambdaForTesting([&](app_management::mojom::AppPtr result) {
        app = std::move(result);
      }));

  EXPECT_EQ(app->id, app_state.id);
  EXPECT_EQ(app->title.value(), app_state.name);
  ASSERT_TRUE(app->run_on_os_login.has_value());
  EXPECT_EQ(app->run_on_os_login.value()->login_mode,
            app_state.run_on_os_login_mode);
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

base::FilePath WebAppIntegrationTestDriver::GetResourceFile(
    base::FilePath::StringPieceType relative_path) {
  base::FilePath base_dir;
  if (!base::PathService::Get(chrome::DIR_TEST_DATA, &base_dir))
    return base::FilePath();
  base::FilePath full_path = base_dir.Append(relative_path);
  {
    base::ScopedAllowBlockingForTesting scoped_allow_blocking;
    if (!PathExists(full_path))
      return base::FilePath();
  }
  return full_path;
}

std::vector<base::FilePath> WebAppIntegrationTestDriver::GetTestFilePaths(
    FilesOptions files_options) {
  std::vector<base::FilePath> file_paths;
  base::FilePath txt_file_path = GetResourceFile(
      FILE_PATH_LITERAL("webapps_integration/files/file_handler_test.txt"));
  base::FilePath png_file_path = GetResourceFile(
      FILE_PATH_LITERAL("webapps_integration/files/file_handler_test.png"));
  switch (files_options) {
    case FilesOptions::kOneTextFile:
      file_paths.push_back(txt_file_path);
      break;
    case FilesOptions::kMultipleTextFiles:
      file_paths.push_back(txt_file_path);
      file_paths.push_back(txt_file_path);
      break;
    case FilesOptions::kOnePngFile:
      file_paths.push_back(png_file_path);
      break;
    case FilesOptions::kMultiplePngFiles:
      file_paths.push_back(png_file_path);
      file_paths.push_back(png_file_path);
      break;
    case FilesOptions::kAllTextAndPngFiles:
      file_paths.push_back(txt_file_path);
      file_paths.push_back(png_file_path);
      break;
  }
  return file_paths;
}

void WebAppIntegrationTestDriver::NavigateBrowser(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  NavigateTabbedBrowserToSite(GetInScopeURL(site), NavigationMode::kCurrentTab);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::NavigatePwa(Site pwa, Site to) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  app_browser_ = GetAppBrowserForSite(pwa);
  NavigateToURLAndWait(app_browser(), GetAppStartURL(to), false);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::NavigateNotfoundUrl() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  NavigateTabbedBrowserToSite(
      delegate_->EmbeddedTestServer()->GetURL("/non-existant/index.html"),
      NavigationMode::kCurrentTab);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ManifestUpdateIcon(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_EQ(Site::kStandalone, site)
      << "Only site mode of 'Standalone' is supported";

  app_id_update_dialog_waiter_ =
      std::make_unique<views::NamedWidgetShownWaiter>(
          views::test::AnyWidgetTestPasskey{},
          "WebAppIdentityUpdateConfirmationView");

  // The kLauncherIcon size is used here, as it is guaranteed to be written to
  // the shortcut on all platforms, as opposed to kInstallIconSize, for example,
  // which, on ChromeOS, is not written to the shortcut because it is not within
  // the intersection between `kDesiredIconSizesForShortcut` (which is platform-
  // dependent) and `SizesToGenerate()` (which is fixed on all platforms).
  auto start_url_path = GetSiteConfiguration(site).relative_start_url;
  GURL url = GetTestServerForSiteMode(site).GetURL(base::StrCat(
      {start_url_path, base::StringPrintf("?manifest=manifest_icon_red_%u.json",
                                          kLauncherIconSize)}));

  ForceUpdateManifestContents(site, url);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ManifestUpdateTitle(Site site, Title title) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_EQ(Site::kStandalone, site)
      << "Only site mode of 'Standalone' is supported";
  ASSERT_EQ(Title::kStandaloneUpdated, title)
      << "Only site mode of 'kStandaloneUpdated' is supported";

  app_id_update_dialog_waiter_ =
      std::make_unique<views::NamedWidgetShownWaiter>(
          views::test::AnyWidgetTestPasskey{},
          "WebAppIdentityUpdateConfirmationView");

  auto start_url_path = GetSiteConfiguration(site).relative_start_url;
  GURL url = GetTestServerForSiteMode(site).GetURL(
      base::StrCat({start_url_path, "?manifest=manifest_title.json"}));
  ForceUpdateManifestContents(site, url);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ManifestUpdateDisplay(Site site,
                                                        Display display) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;

  std::string start_url_path = GetSiteConfiguration(site).relative_start_url;
  std::string manifest_url_param =
      GetDisplayUpdateConfiguration(display).manifest_url_param;
  GURL url = GetTestServerForSiteMode(site).GetURL(
      base::StrCat({start_url_path, manifest_url_param}));

  ForceUpdateManifestContents(site, url);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::ManifestUpdateScopeTo(Site app, Site scope) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  // The `scope_mode` would be changing the scope set in the manifest file. For
  // simplicity, right now only Standalone is supported, so that is just
  // hardcoded in manifest_scope_Standalone.json, which is specified in the URL.
  auto start_url_path = GetSiteConfiguration(app).relative_start_url;
  GURL url = GetTestServerForSiteMode(app).GetURL(base::StrCat(
      {start_url_path, GetScopeUpdateConfiguration(scope).manifest_url_param}));
  ForceUpdateManifestContents(app, url);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::OpenInChrome() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_TRUE(IsBrowserOpen(app_browser())) << "No current app browser.";
  AppId app_id = app_browser()->app_controller()->app_id();
  GURL app_url = GetCurrentTab(app_browser())->GetURL();
  ASSERT_TRUE(AppBrowserController::IsForWebApp(app_browser(), app_id));
  CHECK(chrome::ExecuteCommand(app_browser_, IDC_OPEN_IN_CHROME));
  ui_test_utils::WaitForBrowserToClose(app_browser());
  ASSERT_FALSE(IsBrowserOpen(app_browser())) << "App browser should be closed.";
  app_browser_ = nullptr;
  EXPECT_EQ(GetCurrentTab(browser())->GetURL(), app_url);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SetOpenInTab(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  ;
  // Will need to add feature flag based condition for web app settings page
#if BUILDFLAG(IS_CHROMEOS)
  auto& sync_bridge = WebAppProvider::GetForTest(profile())->sync_bridge();
  sync_bridge.SetAppUserDisplayMode(app_id, UserDisplayMode::kBrowser, true);
#else
  auto app_management_page_handler = CreateAppManagementPageHandler(profile());
  app_management_page_handler.SetWindowMode(app_id, apps::WindowMode::kBrowser);
#endif
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SetOpenInWindow(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  ;
  // Will need to add feature flag based condition for web app settings page.
#if BUILDFLAG(IS_CHROMEOS)
  auto& sync_bridge = WebAppProvider::GetForTest(profile())->sync_bridge();
  sync_bridge.SetAppUserDisplayMode(app_id, UserDisplayMode::kStandalone, true);
#else
  auto app_management_page_handler = CreateAppManagementPageHandler(profile());
  app_management_page_handler.SetWindowMode(app_id, apps::WindowMode::kWindow);
#endif
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SwitchIncognitoProfile() {
  BeforeStateChangeAction(__FUNCTION__);
  content::WebContentsAddedObserver nav_observer;
  CHECK(chrome::ExecuteCommand(browser(), IDC_NEW_INCOGNITO_WINDOW));
  ASSERT_EQ(1U, BrowserList::GetIncognitoBrowserCount());
  nav_observer.GetWebContents();
  active_browser_ = BrowserList::GetInstance()->GetLastActive();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SwitchProfileClients(ProfileClient client) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  std::vector<Profile*> profiles = delegate_->GetAllProfiles();
  ASSERT_EQ(2U, profiles.size())
      << "Cannot switch profile clients if delegate only supports one profile";
  DCHECK(active_profile_);
  switch (client) {
    case ProfileClient::kClient1:
      active_profile_ = profiles[0];
      break;
    case ProfileClient::kClient2:
      active_profile_ = profiles[1];
      break;
  }
  active_browser_ = chrome::FindTabbedBrowser(
      active_profile_, /*match_original_profiles=*/false);
  delegate_->AwaitWebAppQuiescence();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SyncTurnOff() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  delegate_->SyncTurnOff();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::SyncTurnOn() {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  delegate_->SyncTurnOn();
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallFromList(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  UninstallCompleteWaiter uninstall_waiter(profile(), app_id);
  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  apps::AppServiceProxy* app_service_proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile());
  base::RunLoop run_loop;
  app_service_proxy->UninstallForTesting(
      app_id, nullptr,
      base::BindLambdaForTesting([&](bool) { run_loop.Quit(); }));
  run_loop.Run();

  ASSERT_NE(nullptr, AppUninstallDialogView::GetActiveViewForTesting());
  AppUninstallDialogView::GetActiveViewForTesting()->AcceptDialog();
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  // The lacros implementation doesn't use a confirmation dialog so we can
  // call the normal method.
  apps::AppServiceProxy* app_service_proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile());
  app_service_proxy->Uninstall(app_id, apps::UninstallSource::kAppList,
                               nullptr);
#else
  content::TestWebUI test_web_ui;
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  DCHECK(web_contents);
  test_web_ui.set_web_contents(web_contents);
  TestAppLauncherHandler handler(/*extension_service=*/nullptr, provider(),
                                 &test_web_ui);
  base::Value::List web_app_ids;
  web_app_ids.Append(app_id);
  handler.HandleUninstallApp(web_app_ids);
#endif
  uninstall_waiter.Wait();
  site_remember_deny_open_file.erase(site);

  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallFromAppSettings(Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  UninstallCompleteWaiter uninstall_waiter(profile(), app_id);

  auto* web_contents = browser()->tab_strip_model()->GetActiveWebContents();
  if (web_contents->GetURL() !=
      GURL(chrome::kChromeUIWebAppSettingsURL + app_id)) {
    OpenAppSettingsFromChromeApps(site);
    CheckBrowserNavigationIsAppSettings(site);
  }

  web_contents = browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContentsDestroyedWatcher destroyed_watcher(web_contents);

  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  auto app_management_page_handler = CreateAppManagementPageHandler(profile());
  app_management_page_handler.Uninstall(app_id);

  uninstall_waiter.Wait();

  // Wait for app settings page to be closed.
  destroyed_watcher.Wait();

  site_remember_deny_open_file.erase(site);

  AfterStateChangeAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::UninstallFromMenu(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  UninstallCompleteWaiter uninstall_waiter(profile(), app_id);

  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  Browser* app_browser = GetAppBrowserForSite(site);
  ASSERT_TRUE(app_browser);
  auto app_menu_model =
      std::make_unique<WebAppMenuModel>(/*provider=*/nullptr, app_browser);
  app_menu_model->Init();
  ui::MenuModel* model = app_menu_model.get();
  size_t index = 0;
  const bool found = app_menu_model->GetModelAndIndexForCommandId(
      WebAppMenuModel::kUninstallAppCommandId, &model, &index);
  EXPECT_TRUE(found);
  EXPECT_TRUE(model->IsEnabledAt(index));

  app_menu_model->ExecuteCommand(WebAppMenuModel::kUninstallAppCommandId,
                                 /*event_flags=*/0);
  // The |app_menu_model| must be destroyed here, as the |observer| waits
  // until the app is fully uninstalled, which includes closing and deleting
  // the app_browser.
  app_menu_model.reset();
  uninstall_waiter.Wait();
  site_remember_deny_open_file.erase(site);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallPolicyApp(Site site) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  GURL url = GetAppStartURL(site);
  auto policy_app = GetAppBySiteMode(before_state_change_action_state_.get(),
                                     profile(), site);
  DCHECK(policy_app);
  base::RunLoop run_loop;

  UninstallCompleteWaiter uninstall_waiter(profile(), policy_app->id);
  WebAppInstallManagerObserverAdapter observer(profile());
  observer.SetWebAppUninstalledDelegate(
      base::BindLambdaForTesting([&](const AppId& app_id) {
        if (policy_app->id == app_id) {
          run_loop.Quit();
        }
      }));
  // If there are still install sources, the app might not be fully uninstalled,
  // so this will listen for the removal of the policy install source.
  provider()->install_finalizer().SetRemoveManagementTypeCallbackForTesting(
      base::BindLambdaForTesting([&](const AppId& app_id) {
        if (policy_app->id == app_id)
          run_loop.Quit();
      }));
  {
    ListPrefUpdate update(profile()->GetPrefs(),
                          prefs::kWebAppInstallForceList);
    size_t removed_count =
        update->GetList().EraseIf([&](const base::Value& item) {
          const base::Value* url_value = item.GetDict().Find(kUrlKey);
          return url_value && url_value->GetString() == url.spec();
        });
    ASSERT_GT(removed_count, 0U);
  }
  run_loop.Run();
  const WebApp* app = provider()->registrar().GetAppById(policy_app->id);
  // If the app was fully uninstalled, wait for the change to propagate through
  // App Service.
  if (app == nullptr)
    uninstall_waiter.Wait();
  site_remember_deny_open_file.erase(site);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::UninstallFromOs(Site site) {
#if BUILDFLAG(IS_WIN)
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);

  UninstallCompleteWaiter uninstall_waiter(profile(), app_id);

  // Trigger app uninstall via command line.
  extensions::ScopedTestDialogAutoConfirm auto_confirm(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kUninstallAppId, app_id);
  StartupBrowserCreator::ProcessCommandLineAlreadyRunning(
      command_line, {},
      {profile()->GetPath(), StartupProfileMode::kBrowserWindow});

  uninstall_waiter.Wait();
  site_remember_deny_open_file.erase(site);
  AfterStateChangeAction();
#else
  NOTREACHED() << "Not supported on non-Windows platforms";
#endif
}

void WebAppIntegrationTestDriver::CheckAppListEmpty() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<ProfileState> state =
      GetStateForProfile(after_state_change_action_state_.get(), profile());
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(state->apps.empty());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListIconCorrect(Site site) {
  BeforeStateCheckAction(__FUNCTION__);
  GURL icon_url =
      apps::AppIconSource::GetIconURL(active_app_id_, icon_size::k128);
  SkBitmap icon_bitmap;
  base::RunLoop run_loop;

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  NavigateTabbedBrowserToSite(icon_url, NavigationMode::kNewTab);

  web_contents->DownloadImage(
      icon_url, false, gfx::Size(), 0, false,
      base::BindLambdaForTesting([&](int id, int http_status_code,
                                     const GURL& image_url,
                                     const std::vector<SkBitmap>& bitmaps,
                                     const std::vector<gfx::Size>& sizes) {
        EXPECT_EQ(200, http_status_code);
        ASSERT_EQ(bitmaps.size(), 1u);
        icon_bitmap = bitmaps[0];
        run_loop.Quit();
      }));
  run_loop.Run();

  SkColor expected_color = GetSiteConfiguration(site).icon_color;
  // Compare the center pixel color instead of top left corner
  // The app list icon has a filter that changes the color at the corner.
  EXPECT_TRUE(expected_color ==
              icon_bitmap.getColor(icon_size::k128 / 2, icon_size::k128 / 2));
  chrome::CloseTab(browser());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListNotLocallyInstalled(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  // Note: This is a partially supported action.
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_FALSE(app_state->is_installed_locally);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListTabbed(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  // Note: This is a partially supported action.
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_EQ(app_state->user_display_mode, UserDisplayMode::kBrowser);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppInListWindowed(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  // Note: This is a partially supported action.
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_EQ(app_state->user_display_mode, UserDisplayMode::kStandalone);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppNavigationIsStartUrl() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  ASSERT_FALSE(active_app_id_.empty());
  ASSERT_TRUE(app_browser());
  GURL url =
      app_browser()->tab_strip_model()->GetActiveWebContents()->GetVisibleURL();
  EXPECT_EQ(url, provider()->registrar().GetAppStartUrl(active_app_id_));
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckBrowserNavigationIsAppSettings(
    Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  ;

  ASSERT_TRUE(browser());
  GURL url = browser()->tab_strip_model()->GetActiveWebContents()->GetURL();
  EXPECT_EQ(url, GURL(chrome::kChromeUIWebAppSettingsURL + app_id));
  AfterStateCheckAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::CheckAppNotInList(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  EXPECT_FALSE(app_state.has_value());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckPlatformShortcutAndIcon(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state);
  EXPECT_TRUE(app_state->is_shortcut_created);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckPlatformShortcutNotExists(Site site) {
  // This is to handle if the check happens at the very beginning of the test,
  // when no web app is installed (or any other action has happened yet).
  if (!before_state_change_action_state_ && !after_state_change_action_state_)
    return;
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  if (!app_state) {
    app_state = GetAppBySiteMode(before_state_change_action_state_.get(),
                                 profile(), site);
  }
  std::string app_name;
  AppId app_id;
  // If app_state is still nullptr, the site is manually mapped to get an
  // app_name and app_id remains empty.
  if (!app_state) {
    app_name = GetSiteConfiguration(site).app_name;
  } else {
    app_name = app_state->name;
    app_id = app_state->id;
  }
  EXPECT_FALSE(IsShortcutAndIconCreated(profile(), app_name, app_id));
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppIcon(Site site, Color color) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state);
  std::string color_str;
  switch (color) {
    case Color::kGreen:
      color_str = "green";
      break;
    case Color::kRed:
      color_str = "red";
      break;
  }
  EXPECT_EQ(app_state->manifest_launcher_icon_filename,
            base::StringPrintf("%ux%u-%s.png", kLauncherIconSize,
                               kLauncherIconSize, color_str.c_str()));

  // A mapping of image sizes to shortcut colors. Note that the top left
  // pixel color for each size is used as the representation color for that
  // size, even if the image is multi-colored.
  std::map<int, SkColor> shortcut_colors;

  base::RunLoop shortcut_run_loop;
  provider()->os_integration_manager().GetShortcutInfoForApp(
      active_app_id_, base::BindLambdaForTesting(
                          [&](std::unique_ptr<ShortcutInfo> shortcut_info) {
                            if (shortcut_info) {
                              gfx::ImageFamily::const_iterator it;
                              for (it = shortcut_info->favicon.begin();
                                   it != shortcut_info->favicon.end(); ++it) {
                                shortcut_colors[it->Size().width()] =
                                    it->AsBitmap().getColor(0, 0);
                              }
                            }

                            shortcut_run_loop.Quit();
                          }));
  shortcut_run_loop.Run();

  SkColor launcher_icon_color = shortcut_colors[kLauncherIconSize];
  SkColor expected_color;
  switch (color) {
    case Color::kGreen:
      expected_color = SK_ColorGREEN;
      break;
    case Color::kRed:
      expected_color = SK_ColorRED;
      break;
  }
  EXPECT_EQ(expected_color, launcher_icon_color)
      << "Size " << kLauncherIconSize << ": Expecting ARGB " << std::hex
      << expected_color << " but found " << std::hex << launcher_icon_color;

  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckAppTitle(Site site, Title title) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state);
  std::string expected;
  switch (title) {
    case Title::kStandaloneOriginal:
      expected = "Site A";
      break;
    case Title::kStandaloneUpdated:
      expected = "Site A - Updated name";
      break;
  }
  EXPECT_EQ(app_state->name, expected);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckCreateShortcutNotShown() {
  BeforeStateCheckAction(__FUNCTION__);
  EXPECT_EQ(GetAppMenuCommandState(IDC_CREATE_SHORTCUT, browser()), kDisabled);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckCreateShortcutShown() {
  BeforeStateCheckAction(__FUNCTION__);
  EXPECT_EQ(GetAppMenuCommandState(IDC_CREATE_SHORTCUT, browser()), kEnabled);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowModeIsNotVisibleInAppSettings(
    Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;

  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state.has_value());

  mojo::PendingReceiver<app_management::mojom::Page> page;
  mojo::Remote<app_management::mojom::PageHandler> handler;
  auto delegate =
      WebAppSettingsUI::CreateAppManagementPageHandlerDelegate(profile());
  auto app_management_page_handler = AppManagementPageHandler(
      handler.BindNewPipeAndPassReceiver(), page.InitWithNewPipeAndPassRemote(),
      profile(), *delegate);

  base::test::TestFuture<app_management::mojom::AppPtr> test_future;
  app_management_page_handler.GetApp(app_state->id, test_future.GetCallback());

  ASSERT_TRUE(test_future.Wait()) << "Failed to get app information.";

  const auto& app = test_future.Get();
  EXPECT_THAT(app->id, Eq(app_state->id));
  EXPECT_THAT(app->hide_window_mode, Eq(true));

  AfterStateCheckAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::CheckInstallIconShown() {
  // Currently this function does not support tests that check install icons
  // for sites that have a manifest but no service worker.
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  auto* app_banner_manager =
      webapps::TestAppBannerManagerDesktop::FromWebContents(
          GetCurrentTab(browser()));
  app_banner_manager->WaitForInstallableCheck();
  EXPECT_TRUE(pwa_install_view()->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckInstallIconNotShown() {
  // Currently this function does not support tests that check install icons
  // for sites that have a manifest but no service worker.
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  auto* app_banner_manager =
      webapps::TestAppBannerManagerDesktop::FromWebContents(
          GetCurrentTab(browser()));
  app_banner_manager->WaitForInstallableCheck();
  EXPECT_FALSE(pwa_install_view()->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckLaunchIconShown() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  EXPECT_TRUE(browser_state->launch_icon_shown);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckLaunchIconNotShown() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<BrowserState> browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(browser_state.has_value());
  EXPECT_FALSE(browser_state->launch_icon_shown);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckTabCreated() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  DCHECK(before_state_change_action_state_);
  absl::optional<BrowserState> most_recent_browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  absl::optional<BrowserState> previous_browser_state = GetStateForBrowser(
      before_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(most_recent_browser_state.has_value());
  ASSERT_TRUE(previous_browser_state.has_value());
  EXPECT_GT(most_recent_browser_state->tabs.size(),
            previous_browser_state->tabs.size());

  absl::optional<TabState> active_tab =
      GetStateForActiveTab(most_recent_browser_state.value());
  ASSERT_TRUE(active_tab.has_value());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckTabNotCreated() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  DCHECK(before_state_change_action_state_);
  absl::optional<BrowserState> most_recent_browser_state = GetStateForBrowser(
      after_state_change_action_state_.get(), profile(), browser());
  absl::optional<BrowserState> previous_browser_state = GetStateForBrowser(
      before_state_change_action_state_.get(), profile(), browser());
  ASSERT_TRUE(most_recent_browser_state.has_value());
  ASSERT_TRUE(previous_browser_state.has_value());
  EXPECT_EQ(most_recent_browser_state->tabs.size(),
            previous_browser_state->tabs.size());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckCustomToolbar() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser());
  EXPECT_TRUE(app_browser()->app_controller()->ShouldShowCustomTabBar());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());
  EXPECT_TRUE(app_view->toolbar()
                  ->custom_tab_bar()
                  ->close_button_for_testing()
                  ->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckNoToolbar() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser());
  EXPECT_FALSE(app_browser()->app_controller()->ShouldShowCustomTabBar());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());
  EXPECT_FALSE(app_view->toolbar()->custom_tab_bar()->GetVisible());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckRunOnOsLoginEnabled(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state);
  EXPECT_EQ(app_state->run_on_os_login_mode, apps::RunOnOsLoginMode::kWindowed);
  base::ScopedAllowBlockingForTesting allow_blocking;
#if BUILDFLAG(IS_LINUX)
  std::string shortcut_filename = "chrome-" + app_state->id + "-" +
                                  profile()->GetBaseName().value() + ".desktop";
  ASSERT_TRUE(base::PathExists(
      override_registration_->shortcut_override->startup.GetPath().Append(
          shortcut_filename)));
#elif BUILDFLAG(IS_WIN)
  SiteConfig site_config = GetSiteConfigurationFromAppName(app_state->name);
  SkColor color = site_config.icon_color;
  base::FilePath startup_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->startup.GetPath(),
      app_state->name, app_state->id);
  ASSERT_TRUE(base::PathExists(startup_shortcut_path));
  ASSERT_TRUE(GetIconTopLeftColor(startup_shortcut_path) == color);
#elif BUILDFLAG(IS_MAC)
  std::string shortcut_filename = app_state->name + ".app";
  base::FilePath app_shortcut_path =
      override_registration_->shortcut_override->chrome_apps_folder.GetPath()
          .Append(shortcut_filename);
  ASSERT_TRUE(override_registration_->shortcut_override
                  ->startup_enabled[app_shortcut_path]);
#endif
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckRunOnOsLoginDisabled(Site site) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state);
  base::ScopedAllowBlockingForTesting allow_blocking;
#if BUILDFLAG(IS_LINUX)
  std::string shortcut_filename = "chrome-" + app_state->id + "-" +
                                  profile()->GetBaseName().value() + ".desktop";
  ASSERT_FALSE(base::PathExists(
      override_registration_->shortcut_override->startup.GetPath().Append(
          shortcut_filename)));
#elif BUILDFLAG(IS_WIN)
  base::FilePath startup_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->startup.GetPath(),
      app_state->name, app_state->id);
  ASSERT_FALSE(base::PathExists(startup_shortcut_path));
#elif BUILDFLAG(IS_MAC)
  std::string shortcut_filename = app_state->name + ".app";
  base::FilePath app_shortcut_path =
      override_registration_->shortcut_override->chrome_apps_folder.GetPath()
          .Append(shortcut_filename);
  ASSERT_FALSE(override_registration_->shortcut_override
                   ->startup_enabled[app_shortcut_path]);
#endif
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckSiteHandlesFile(
    Site site,
    std::string file_extension) {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  ASSERT_TRUE(IsFileHandledBySite(site, file_extension));
  AfterStateCheckAction();
#endif
}

void WebAppIntegrationTestDriver::CheckSiteNotHandlesFile(
    Site site,
    std::string file_extension) {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  ASSERT_FALSE(IsFileHandledBySite(site, file_extension));
  AfterStateCheckAction();
#endif
}

void WebAppIntegrationTestDriver::CheckUserCannotSetRunOnOsLogin(Site site) {
#if !BUILDFLAG(IS_CHROMEOS)
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state =
      GetAppBySiteMode(after_state_change_action_state_.get(), profile(), site);
  ASSERT_TRUE(app_state);
  auto app_management_page_handler = CreateAppManagementPageHandler(profile());

  app_management::mojom::AppPtr app;
  app_management_page_handler.GetApp(
      app_state->id,
      base::BindLambdaForTesting([&](app_management::mojom::AppPtr result) {
        app = std::move(result);
      }));

  ASSERT_TRUE(app->run_on_os_login.has_value());
  ASSERT_TRUE(app->run_on_os_login.value()->is_managed);
  if (app_state->run_on_os_login_mode == apps::RunOnOsLoginMode::kWindowed) {
    DisableRunOnOsLogin(site);
    CheckRunOnOsLoginEnabled(site);
  } else {
    EnableRunOnOsLogin(site);
    CheckRunOnOsLoginDisabled(site);
  }
  AfterStateCheckAction();
#else
  NOTREACHED() << "Not implemented on Chrome OS.";
#endif
}

void WebAppIntegrationTestDriver::CheckUserDisplayModeInternal(
    UserDisplayMode user_display_mode) {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  absl::optional<AppState> app_state = GetStateForAppId(
      after_state_change_action_state_.get(), profile(), active_app_id_);
  ASSERT_TRUE(app_state.has_value());
  EXPECT_EQ(user_display_mode, app_state->user_display_mode);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowClosed() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  DCHECK(before_state_change_action_state_);
  absl::optional<ProfileState> after_action_profile =
      GetStateForProfile(after_state_change_action_state_.get(), profile());
  absl::optional<ProfileState> before_action_profile =
      GetStateForProfile(before_state_change_action_state_.get(), profile());
  ASSERT_TRUE(after_action_profile.has_value());
  ASSERT_TRUE(before_action_profile.has_value());
  EXPECT_LT(after_action_profile->browsers.size(),
            before_action_profile->browsers.size());
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowCreated() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  DCHECK(before_state_change_action_state_);
  absl::optional<ProfileState> after_action_profile =
      GetStateForProfile(after_state_change_action_state_.get(), profile());
  absl::optional<ProfileState> before_action_profile =
      GetStateForProfile(before_state_change_action_state_.get(), profile());
  ASSERT_TRUE(after_action_profile.has_value());
  ASSERT_TRUE(before_action_profile.has_value());
  EXPECT_GT(after_action_profile->browsers.size(),
            before_action_profile->browsers.size())
      << "Before: \n"
      << *before_state_change_action_state_ << "\nAfter:\n"
      << *after_state_change_action_state_;
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowControlsOverlayToggle(
    Site site,
    IsShown is_shown) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  if (!app_browser())
    app_browser_ = GetAppBrowserForSite(site);
  ASSERT_TRUE(app_browser());
  EXPECT_EQ(app_browser()->app_controller()->AppUsesWindowControlsOverlay(),
            is_shown == IsShown::kShown);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::CheckWindowControlsOverlay(Site site,
                                                             IsOn is_on) {
  if (!BeforeStateChangeAction(__FUNCTION__))
    return;
  ASSERT_TRUE(app_browser());
  BrowserView* app_view = BrowserView::GetBrowserViewForBrowser(app_browser());
  EXPECT_EQ(app_view->IsWindowControlsOverlayEnabled(), is_on == IsOn::kOn);
  AfterStateChangeAction();
}

void WebAppIntegrationTestDriver::CheckWindowDisplayMinimal() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  DCHECK(app_browser());
  DCHECK(app_browser()->app_controller()->AsWebAppBrowserController());
  absl::optional<AppState> app_state = GetStateForAppId(
      after_state_change_action_state_.get(), profile(), active_app_id_);
  ASSERT_TRUE(app_state.has_value());

  content::WebContents* web_contents =
      app_browser()->tab_strip_model()->GetActiveWebContents();
  DCHECK(web_contents);
  DisplayMode window_display_mode =
      web_contents->GetDelegate()->GetDisplayMode(web_contents);

  EXPECT_TRUE(app_browser()->app_controller()->HasMinimalUiButtons());
  EXPECT_EQ(app_state->effective_display_mode,
            blink::mojom::DisplayMode::kMinimalUi);
  EXPECT_EQ(window_display_mode, blink::mojom::DisplayMode::kMinimalUi);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::CheckWindowDisplayStandalone() {
  if (!BeforeStateCheckAction(__FUNCTION__))
    return;
  DCHECK(app_browser());
  DCHECK(app_browser()->app_controller()->AsWebAppBrowserController());
  absl::optional<AppState> app_state = GetStateForAppId(
      after_state_change_action_state_.get(), profile(), active_app_id_);
  ASSERT_TRUE(app_state.has_value());

  content::WebContents* web_contents =
      app_browser()->tab_strip_model()->GetActiveWebContents();
  DCHECK(web_contents);
  DisplayMode window_display_mode =
      web_contents->GetDelegate()->GetDisplayMode(web_contents);

  EXPECT_FALSE(app_browser()->app_controller()->HasMinimalUiButtons());
  EXPECT_EQ(app_state->effective_display_mode,
            blink::mojom::DisplayMode::kStandalone);
  EXPECT_EQ(window_display_mode, blink::mojom::DisplayMode::kStandalone);
  AfterStateCheckAction();
}

void WebAppIntegrationTestDriver::OnWebAppManifestUpdated(
    const AppId& app_id,
    base::StringPiece old_name) {
  LOG(INFO) << "Manifest update received for " << app_id << ".";
  DCHECK_EQ(1ul, delegate_->GetAllProfiles().size())
      << "Manifest update waiting only supported on single profile tests.";

  previous_manifest_updates_.insert(app_id);
  if (waiting_for_update_id_ == app_id) {
    DCHECK(waiting_for_update_run_loop_);
    waiting_for_update_run_loop_->Quit();
    waiting_for_update_id_ = absl::nullopt;
    // The `BeforeState*Action()` methods check that the
    // `after_state_change_action_state_` has not changed from the current
    // state. This is great, except for the manifest update edge case, which can
    // happen asynchronously outside of actions. In this case, re-grab the
    // snapshot after the update.
    if (executing_action_level_ == 0 && after_state_change_action_state_)
      after_state_change_action_state_ = ConstructStateSnapshot();
  }
}

bool WebAppIntegrationTestDriver::BeforeStateChangeAction(
    const char* function) {
  if (testing::Test::HasFatalFailure() && !in_tear_down_)
    return false;
  LOG(INFO) << "BeforeStateChangeAction: "
            << std::string(executing_action_level_, ' ') << function;
  ++executing_action_level_;
  std::unique_ptr<StateSnapshot> current_state = ConstructStateSnapshot();
  if (after_state_change_action_state_) {
    DCHECK_EQ(*after_state_change_action_state_, *current_state)
        << "State cannot be changed outside of state change actions.";
    before_state_change_action_state_ =
        std::move(after_state_change_action_state_);
  } else {
    before_state_change_action_state_ = std::move(current_state);
  }
  return true;
}

void WebAppIntegrationTestDriver::AfterStateChangeAction() {
  DCHECK(executing_action_level_ > 0);
  --executing_action_level_;
  provider()->command_manager().AwaitAllCommandsCompleteForTesting();
#if BUILDFLAG(IS_MAC)
  for (auto* profile : delegate_->GetAllProfiles()) {
    std::vector<AppId> app_ids = provider()->registrar().GetAppIds();
    for (auto& app_id : app_ids) {
      auto* app_shim_manager = apps::AppShimManager::Get();
      AppShimHost* app_shim_host = app_shim_manager->FindHost(profile, app_id);
      if (app_shim_host && !app_shim_host->HasBootstrapConnected()) {
        base::RunLoop loop;
        app_shim_host->SetOnShimConnectedForTesting(loop.QuitClosure());
        loop.Run();
      }
    }
  }
#endif
  if (delegate_->IsSyncTest())
    delegate_->AwaitWebAppQuiescence();
  FlushShortcutTasks();
  after_state_change_action_state_ = ConstructStateSnapshot();
}

bool WebAppIntegrationTestDriver::BeforeStateCheckAction(const char* function) {
  if (testing::Test::HasFatalFailure() && !in_tear_down_)
    return false;
  ++executing_action_level_;
  provider()->command_manager().AwaitAllCommandsCompleteForTesting();
  LOG(INFO) << "BeforeStateCheckAction: "
            << std::string(executing_action_level_, ' ') << function;
  DCHECK(after_state_change_action_state_);
  return true;
}

void WebAppIntegrationTestDriver::AfterStateCheckAction() {
  DCHECK(executing_action_level_ > 0);
  --executing_action_level_;
  if (!after_state_change_action_state_)
    return;
  DCHECK_EQ(*after_state_change_action_state_, *ConstructStateSnapshot());
}

AppId WebAppIntegrationTestDriver::GetAppIdBySiteMode(Site site) {
  auto site_config = GetSiteConfiguration(site);
  std::string manifest_id = site_config.relative_manifest_id;
  auto relative_start_url = site_config.relative_start_url;
  GURL start_url = GetTestServerForSiteMode(site).GetURL(relative_start_url);
  DCHECK(start_url.is_valid());

  return GenerateAppId(manifest_id, start_url);
}

GURL WebAppIntegrationTestDriver::GetAppStartURL(Site site) {
  auto start_url_path = GetSiteConfiguration(site).relative_start_url;
  return GetTestServerForSiteMode(site).GetURL(start_url_path);
}

absl::optional<AppState> WebAppIntegrationTestDriver::GetAppBySiteMode(
    StateSnapshot* state_snapshot,
    Profile* profile,
    Site site) {
  absl::optional<ProfileState> profile_state =
      GetStateForProfile(state_snapshot, profile);
  if (!profile_state) {
    return absl::nullopt;
  }

  AppId app_id = GetAppIdBySiteMode(site);
  auto it = profile_state->apps.find(app_id);
  return it == profile_state->apps.end()
             ? absl::nullopt
             : absl::make_optional<AppState>(it->second);
}

WebAppProvider* WebAppIntegrationTestDriver::GetProviderForProfile(
    Profile* profile) {
  return WebAppProvider::GetForTest(profile);
}

std::unique_ptr<StateSnapshot>
WebAppIntegrationTestDriver::ConstructStateSnapshot() {
  base::flat_map<Profile*, ProfileState> profile_state_map;
  for (Profile* profile : delegate_->GetAllProfiles()) {
    base::flat_map<Browser*, BrowserState> browser_state;
    auto* browser_list = BrowserList::GetInstance();
    for (Browser* browser : *browser_list) {
      if (browser->profile() != profile) {
        continue;
      }

      TabStripModel* tabs = browser->tab_strip_model();
      base::flat_map<content::WebContents*, TabState> tab_state_map;
      for (int i = 0; i < tabs->count(); ++i) {
        content::WebContents* tab = tabs->GetWebContentsAt(i);
        DCHECK(tab);
        GURL url = tab->GetURL();
        tab_state_map.emplace(tab, TabState(url));
      }
      content::WebContents* active_tab = tabs->GetActiveWebContents();
      bool launch_icon_shown = false;
      bool is_app_browser = AppBrowserController::IsWebApp(browser);
      if (!is_app_browser && active_tab != nullptr) {
        auto* tab_helper = IntentPickerTabHelper::FromWebContents(active_tab);
        base::RunLoop run_loop;
        tab_helper->SetIconUpdateCallbackForTesting(
            run_loop.QuitClosure(),
            /*include_latest_navigation*/ true);
        run_loop.Run();

        launch_icon_shown = intent_picker_view()->GetVisible();
      }
      AppId app_id;
      if (AppBrowserController::IsWebApp(browser))
        app_id = browser->app_controller()->app_id();

      browser_state.emplace(
          browser, BrowserState(browser, tab_state_map, active_tab, app_id,
                                launch_icon_shown));
    }

    WebAppRegistrar& registrar = GetProviderForProfile(profile)->registrar();
    auto app_ids = registrar.GetAppIds();
    base::flat_map<AppId, AppState> app_state;
    for (const auto& app_id : app_ids) {
      std::string manifest_launcher_icon_filename;
      std::vector<apps::IconInfo> icon_infos =
          provider()->registrar().GetAppIconInfos(app_id);
      for (const auto& info : icon_infos) {
        int icon_size = info.square_size_px.value_or(-1);
        if (icon_size == kLauncherIconSize) {
          manifest_launcher_icon_filename = info.url.ExtractFileName();
        }
      }
      auto state = AppState(
          app_id, registrar.GetAppShortName(app_id),
          registrar.GetAppScope(app_id),
          ConvertOsLoginMode(registrar.GetAppRunOnOsLoginMode(app_id).value),
          registrar.GetAppEffectiveDisplayMode(app_id),
          registrar.GetAppUserDisplayMode(app_id),
          manifest_launcher_icon_filename, registrar.IsLocallyInstalled(app_id),
          IsShortcutAndIconCreated(profile, registrar.GetAppShortName(app_id),
                                   app_id),
          registrar.IsIsolated(app_id));
#if !BUILDFLAG(IS_CHROMEOS)
      if (registrar.IsLocallyInstalled(app_id)) {
        CheckAppSettingsAppState(profile, state);
      }
#endif
      app_state.emplace(app_id, state);
    }

    profile_state_map.emplace(
        profile, ProfileState(std::move(browser_state), std::move(app_state)));
  }
  return std::make_unique<StateSnapshot>(std::move(profile_state_map));
}

std::string WebAppIntegrationTestDriver::GetBrowserWindowTitle(
    Browser* browser) {
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
  return convert.to_bytes(browser->GetWindowTitleForCurrentTab(false));
}

content::WebContents* WebAppIntegrationTestDriver::GetCurrentTab(
    Browser* browser) {
  return browser->tab_strip_model()->GetActiveWebContents();
}

GURL WebAppIntegrationTestDriver::GetInScopeURL(Site site) {
  return GetAppStartURL(site);
}

GURL WebAppIntegrationTestDriver::GetScopeForSiteMode(Site site) {
  auto scope_url_path = GetSiteConfiguration(site).relative_scope_url;
  return GetTestServerForSiteMode(site).GetURL(scope_url_path);
}

base::FilePath WebAppIntegrationTestDriver::GetShortcutPath(
    base::FilePath shortcut_dir,
    const std::string& app_name,
    const AppId& app_id) {
#if BUILDFLAG(IS_WIN)
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  base::FileEnumerator enumerator(shortcut_dir, false,
                                  base::FileEnumerator::FILES);
  while (!enumerator.Next().empty()) {
    std::wstring shortcut_filename = enumerator.GetInfo().GetName().value();
    if (re2::RE2::FullMatch(converter.to_bytes(shortcut_filename),
                            app_name + "(.*).lnk")) {
      base::FilePath shortcut_path = shortcut_dir.Append(shortcut_filename);
      if (GetShortcutProfile(shortcut_path) == profile()->GetBaseName())
        return shortcut_path;
    }
  }
#elif BUILDFLAG(IS_MAC)
  std::string shortcut_filename = app_name + ".app";
  base::FilePath shortcut_path = shortcut_dir.Append(shortcut_filename);
  // Exits early if the app id is empty because the verification won't work.
  // TODO(crbug.com/1289865): Figure a way to find the profile that has the app
  //                          installed without using app ID.
  if (app_id.empty())
    return shortcut_path;

  AppShimRegistry* registry = AppShimRegistry::Get();
  std::set<base::FilePath> app_installed_profiles =
      registry->GetInstalledProfilesForApp(app_id);
  if (app_installed_profiles.find(profile()->GetPath()) !=
      app_installed_profiles.end()) {
    return shortcut_path;
  }
#elif BUILDFLAG(IS_LINUX)
  std::string shortcut_filename =
      "chrome-" + app_id + "-" + profile()->GetBaseName().value() + ".desktop";
  base::FilePath shortcut_path = shortcut_dir.Append(shortcut_filename);
  if (base::PathExists(shortcut_path))
    return shortcut_path;
#endif
  return base::FilePath();
}

void WebAppIntegrationTestDriver::InstallPolicyAppInternal(
    Site site,
    base::Value default_launch_container,
    const bool create_shortcut) {
  GURL url = GetAppStartURL(site);
  WebAppTestInstallWithOsHooksObserver observer(profile());
  observer.BeginListening();
  {
    base::Value::Dict item;
    item.Set(kUrlKey, url.spec());
    item.Set(kDefaultLaunchContainerKey, std::move(default_launch_container));
    item.Set(kCreateDesktopShortcutKey, create_shortcut);
    ListPrefUpdate update(profile()->GetPrefs(),
                          prefs::kWebAppInstallForceList);
    update->GetList().Append(std::move(item));
  }
  active_app_id_ = observer.Wait();
  AppReadinessWaiter(profile(), active_app_id_).Await();
}

void WebAppIntegrationTestDriver::ApplyRunOnOsLoginPolicy(Site site,
                                                          const char* policy) {
  GURL url = GetAppStartURL(site);
  {
    ListPrefUpdate update(profile()->GetPrefs(), prefs::kWebAppSettings);
    base::Value::List& update_list = update->GetList();
    update_list.EraseIf([&](const base::Value& item) {
      return *item.GetDict().FindString(kManifestId) == url.spec();
    });

    base::Value::Dict dict_item;
    dict_item.Set(kManifestId, url.spec());
    dict_item.Set(kRunOnOsLogin, policy);

    update_list.Append(std::move(dict_item));
  }
}

void WebAppIntegrationTestDriver::UninstallPolicyAppById(const AppId& id) {
  base::RunLoop run_loop;
  AppReadinessWaiter app_registration_waiter(
      profile(), id, apps::Readiness::kUninstalledByUser);
  WebAppInstallManagerObserverAdapter observer(profile());
  observer.SetWebAppUninstalledDelegate(
      base::BindLambdaForTesting([&](const AppId& app_id) {
        if (id == app_id) {
          run_loop.Quit();
        }
      }));
  // If there are still install sources, the app might not be fully uninstalled,
  // so this will listen for the removal of the policy install source.
  provider()->install_finalizer().SetRemoveManagementTypeCallbackForTesting(
      base::BindLambdaForTesting([&](const AppId& app_id) {
        if (id == app_id)
          run_loop.Quit();
      }));
  std::string url_spec = provider()->registrar().GetAppStartUrl(id).spec();
  {
    ListPrefUpdate update(profile()->GetPrefs(),
                          prefs::kWebAppInstallForceList);
    size_t removed_count =
        update->GetList().EraseIf([&](const base::Value& item) {
          const base::Value* url_value = item.GetDict().Find(kUrlKey);
          return url_value && url_value->GetString() == url_spec;
        });
    ASSERT_GT(removed_count, 0U);
  }
  run_loop.Run();
  const WebApp* app = provider()->registrar().GetAppById(id);
  // If the app was fully uninstalled, wait for the change to propagate through
  // App Service.
  if (app == nullptr)
    app_registration_waiter.Await();
  if (app == nullptr && active_app_id_ == id)
    active_app_id_.clear();
}

void WebAppIntegrationTestDriver::ForceUpdateManifestContents(
    Site site,
    const GURL& app_url_with_manifest_param) {
  auto app_id = GetAppIdBySiteMode(site);
  active_app_id_ = app_id;
  // Manifest updates must occur as the first navigation after a webapp is
  // installed, otherwise the throttle is tripped.
  ASSERT_FALSE(provider()->manifest_update_manager().IsUpdateConsumed(app_id));
  ASSERT_FALSE(
      provider()->manifest_update_manager().IsUpdateTaskPending(app_id));
  NavigateTabbedBrowserToSite(app_url_with_manifest_param,
                              NavigationMode::kCurrentTab);
}

void WebAppIntegrationTestDriver::MaybeNavigateTabbedBrowserInScope(Site site) {
  auto browser_url = GetCurrentTab(browser())->GetURL();
  auto dest_url = GetInScopeURL(site);
  if (browser_url.is_empty() || browser_url != dest_url) {
    NavigateTabbedBrowserToSite(dest_url, NavigationMode::kCurrentTab);
  }
}

void WebAppIntegrationTestDriver::NavigateTabbedBrowserToSite(
    const GURL& url,
    NavigationMode mode) {
  DCHECK(browser());
  if (mode == NavigationMode::kNewTab) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB |
            ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  } else {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }
}

Browser* WebAppIntegrationTestDriver::GetAppBrowserForSite(
    Site site,
    bool launch_if_not_open) {
  StateSnapshot* state = after_state_change_action_state_
                             ? after_state_change_action_state_.get()
                             : before_state_change_action_state_.get();
  DCHECK(state);
  absl::optional<AppState> app_state = GetAppBySiteMode(state, profile(), site);
  DCHECK(app_state) << "Could not find installed app for site "
                    << static_cast<int>(site);

  auto profile_state = GetStateForProfile(state, profile());
  DCHECK(profile_state);
  for (const auto& browser_state_pair : profile_state->browsers) {
    if (browser_state_pair.second.app_id == app_state->id)
      return browser_state_pair.second.browser;
  }
  if (!launch_if_not_open)
    return nullptr;
  Browser* browser = LaunchWebAppBrowserAndWait(profile(), app_state->id);
  provider()->manifest_update_manager().ResetManifestThrottleForTesting(
      GetAppIdBySiteMode(site));
  return browser;
}

bool WebAppIntegrationTestDriver::IsShortcutAndIconCreated(
    Profile* profile,
    const std::string& name,
    const AppId& id) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  bool is_shortcut_and_icon_correct = false;
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)

  SkColor expected_icon_pixel_color =
      GetSiteConfigurationFromAppName(name).icon_color;
#endif
#if BUILDFLAG(IS_WIN)
  base::FilePath desktop_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->desktop.GetPath(), name, id);
  base::FilePath application_menu_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->application_menu.GetPath(),
      name, id);
  if (base::PathExists(desktop_shortcut_path) &&
      base::PathExists(application_menu_shortcut_path))
    is_shortcut_and_icon_correct =
        (GetIconTopLeftColor(desktop_shortcut_path) ==
             expected_icon_pixel_color &&
         GetIconTopLeftColor(application_menu_shortcut_path) ==
             expected_icon_pixel_color);
#elif BUILDFLAG(IS_MAC)
  base::FilePath app_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->chrome_apps_folder.GetPath(),
      name, id);
  if (base::PathExists(app_shortcut_path)) {
    SkColor icon_pixel_color = GetIconTopLeftColor(app_shortcut_path);
    is_shortcut_and_icon_correct =
        (icon_pixel_color == expected_icon_pixel_color);
  }
#elif BUILDFLAG(IS_LINUX)
  base::FilePath desktop_shortcut_path = GetShortcutPath(
      override_registration_->shortcut_override->desktop.GetPath(), name, id);
  if (base::PathExists(desktop_shortcut_path)) {
    is_shortcut_and_icon_correct = IconManagerCheckIconTopLeftColor(
        provider()->icon_manager(), id, {kLauncherIconSize, kInstallIconSize},
        expected_icon_pixel_color);
  }
#elif BUILDFLAG(IS_CHROMEOS)
  is_shortcut_and_icon_correct = IconManagerCheckIconTopLeftColor(
      provider()->icon_manager(), id, {kLauncherIconSize, kInstallIconSize},
      expected_icon_pixel_color);
#endif
  return is_shortcut_and_icon_correct;
}

bool WebAppIntegrationTestDriver::IsFileHandledBySite(
    Site site,
    std::string file_extension) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  bool is_file_handled = false;
#if BUILDFLAG(IS_WIN)
  AppId app_id = GetAppIdBySiteMode(site);
  const std::wstring prog_id =
      GetProgIdForApp(browser()->profile()->GetPath(), app_id);
  const std::vector<std::wstring> file_handler_prog_ids =
      ShellUtil::GetFileHandlerProgIdsForAppId(prog_id);

  base::win::RegKey key;
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
  for (const auto& file_handler_prog_id : file_handler_prog_ids) {
    const std::vector<std::wstring> supported_file_extensions =
        GetFileExtensionsForProgId(file_handler_prog_id);
    std::wstring extension = converter.from_bytes("." + file_extension);
    if (std::find(supported_file_extensions.begin(),
                  supported_file_extensions.end(),
                  extension) != supported_file_extensions.end()) {
      const std::wstring reg_key = std::wstring(ShellUtil::kRegClasses) +
                                   base::FilePath::kSeparators[0] + extension +
                                   base::FilePath::kSeparators[0] +
                                   ShellUtil::kRegOpenWithProgids;
      EXPECT_EQ(ERROR_SUCCESS,
                key.Open(HKEY_CURRENT_USER, reg_key.data(), KEY_READ));
      return key.HasValue(file_handler_prog_id.data());
    }
  }
#elif BUILDFLAG(IS_MAC)
  std::string app_name = GetSiteConfiguration(site).app_name;
  const base::FilePath test_file_path =
      override_registration_->shortcut_override->chrome_apps_folder.GetPath()
          .AppendASCII("test." + file_extension);
  const base::File test_file(
      test_file_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  const GURL test_file_url = net::FilePathToFileURL(test_file_path);
  is_file_handled =
      (base::UTF8ToUTF16(app_name) ==
       shell_integration::GetApplicationNameForProtocol(test_file_url));
#elif BUILDFLAG(IS_LINUX)
  AppId app_id = GetAppIdBySiteMode(site);
  for (const LinuxFileRegistration& command :
       override_registration_->shortcut_override->linux_file_registration) {
    if (base::Contains(command.xdg_command, app_id) &&
        base::Contains(command.file_contents, file_extension)) {
      is_file_handled = base::Contains(command.xdg_command, "install");
    }
  }
#endif
  return is_file_handled;
}

void WebAppIntegrationTestDriver::SetRunOnOsLoginMode(
    Site site,
    apps::RunOnOsLoginMode login_mode) {
#if !BUILDFLAG(IS_CHROMEOS)
  AppId app_id = GetAppIdBySiteMode(site);
  ASSERT_TRUE(provider()->registrar().GetAppById(app_id))
      << "No app installed for site: " << static_cast<int>(site);
  auto app_management_page_handler = CreateAppManagementPageHandler(profile());
  app_management_page_handler.SetRunOnOsLoginMode(app_id, login_mode);
#endif
}

void WebAppIntegrationTestDriver::LaunchAppStartupBrowserCreator(
    const AppId& app_id) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kAppId, app_id);
  command_line.AppendSwitchASCII(switches::kTestType, "browser");
  ASSERT_TRUE(StartupBrowserCreator().ProcessCmdLineImpl(
      command_line, base::FilePath(), chrome::startup::IsProcessStartup::kNo,
      {browser()->profile(), StartupProfileMode::kBrowserWindow}, {}));
  content::RunAllTasksUntilIdle();
}

Browser* WebAppIntegrationTestDriver::browser() {
  Browser* browser = active_browser_
                         ? active_browser_.get()
                         : chrome::FindTabbedBrowser(
                               profile(), /*match_original_profiles=*/false);
  DCHECK(browser);
  if (!browser->tab_strip_model()->count()) {
    delegate_->AddBlankTabAndShow(browser);
  }
  return browser;
}

PageActionIconView* WebAppIntegrationTestDriver::pwa_install_view() {
  PageActionIconView* pwa_install_view =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->toolbar_button_provider()
          ->GetPageActionIconView(PageActionIconType::kPwaInstall);
  DCHECK(pwa_install_view);
  return pwa_install_view;
}

PageActionIconView* WebAppIntegrationTestDriver::intent_picker_view() {
  PageActionIconView* intent_picker_view =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->toolbar_button_provider()
          ->GetPageActionIconView(PageActionIconType::kIntentPicker);
  DCHECK(intent_picker_view);
  return intent_picker_view;
}

const net::EmbeddedTestServer&
WebAppIntegrationTestDriver::GetTestServerForSiteMode(Site site) const {
  if (site == Site::kIsolated) {
    return *isolated_app_test_server_;
  }

  return *delegate_->EmbeddedTestServer();
}

WebAppIntegrationTest::WebAppIntegrationTest() : helper_(this) {
  std::vector<base::Feature> enabled_features;
  std::vector<base::Feature> disabled_features;
  enabled_features.push_back(features::kPwaUpdateDialogForIcon);
  enabled_features.push_back(features::kPwaUpdateDialogForName);
  enabled_features.push_back(features::kDesktopPWAsEnforceWebAppSettingsPolicy);
  enabled_features.push_back(features::kWebAppWindowControlsOverlay);
  enabled_features.push_back(features::kRecordWebAppDebugInfo);
  enabled_features.push_back(blink::features::kFileHandlingAPI);
#if BUILDFLAG(IS_CHROMEOS_ASH)
  disabled_features.push_back(features::kWebAppsCrosapi);
  disabled_features.push_back(chromeos::features::kLacrosPrimary);
#endif
#if BUILDFLAG(IS_CHROMEOS)
  // TODO(crbug.com/1357905): Update test driver to work with new UI.
  disabled_features.push_back(apps::features::kLinkCapturingUiUpdate);
#endif
  scoped_feature_list_.InitWithFeatures(enabled_features, disabled_features);
}

WebAppIntegrationTest::~WebAppIntegrationTest() = default;

void WebAppIntegrationTest::SetUp() {
  helper_.SetUp();
  InProcessBrowserTest::SetUp();
  chrome::SetAutoAcceptAppIdentityUpdateForTesting(false);
}

void WebAppIntegrationTest::SetUpOnMainThread() {
  helper_.SetUpOnMainThread();
}
void WebAppIntegrationTest::TearDownOnMainThread() {
  helper_.TearDownOnMainThread();
}

void WebAppIntegrationTest::SetUpCommandLine(base::CommandLine* command_line) {
  ASSERT_TRUE(embedded_test_server()->Start());
}

Browser* WebAppIntegrationTest::CreateBrowser(Profile* profile) {
  return InProcessBrowserTest::CreateBrowser(profile);
}

void WebAppIntegrationTest::CloseBrowserSynchronously(Browser* browser) {
  InProcessBrowserTest::CloseBrowserSynchronously(browser);
}

void WebAppIntegrationTest::AddBlankTabAndShow(Browser* browser) {
  InProcessBrowserTest::AddBlankTabAndShow(browser);
}

const net::EmbeddedTestServer* WebAppIntegrationTest::EmbeddedTestServer()
    const {
  return embedded_test_server();
}

std::vector<Profile*> WebAppIntegrationTest::GetAllProfiles() {
  return std::vector<Profile*>{browser()->profile()};
}

bool WebAppIntegrationTest::IsSyncTest() {
  return false;
}

void WebAppIntegrationTest::SyncTurnOff() {
  NOTREACHED();
}
void WebAppIntegrationTest::SyncTurnOn() {
  NOTREACHED();
}
void WebAppIntegrationTest::AwaitWebAppQuiescence() {
  NOTREACHED();
}

}  // namespace web_app::integration_tests
