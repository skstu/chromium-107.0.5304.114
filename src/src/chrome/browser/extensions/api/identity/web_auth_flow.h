// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_IDENTITY_WEB_AUTH_FLOW_H_
#define CHROME_BROWSER_EXTENSIONS_API_IDENTITY_WEB_AUTH_FLOW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

class Profile;
class WebAuthFlowTest;

namespace content {
class StoragePartition;
}

namespace extensions {

// Controller class for web based auth flows. The WebAuthFlow creates
// a dialog window in the scope approval component app by firing an
// event. A webview embedded in the dialog will navigate to the
// |provider_url| passed to the WebAuthFlow constructor.
//
// The WebAuthFlow monitors the WebContents of the webview, and
// notifies its delegate interface any time the WebContents navigates
// to a new URL or changes title. The delegate is expected to delete
// the flow when navigation reaches a known target location.
//
// The window is not displayed until the first page load
// completes. This allows the flow to complete without flashing a
// window on screen if the provider immediately redirects to the
// target URL.
//
// A WebAuthFlow can be started in Mode::SILENT, which never displays
// a window. If a window would be required, the flow fails.
class WebAuthFlow : public content::WebContentsObserver,
                    public AppWindowRegistry::Observer {
 public:
  enum Mode {
    INTERACTIVE,  // Show UI to the user if necessary.
    SILENT        // No UI should be shown.
  };

  enum Partition {
    GET_AUTH_TOKEN,       // Use the getAuthToken() partition.
    LAUNCH_WEB_AUTH_FLOW  // Use the launchWebAuthFlow() partition.
  };

  enum Failure {
    WINDOW_CLOSED,  // Window closed by user.
    INTERACTION_REQUIRED,  // Non-redirect page load in silent mode.
    LOAD_FAILED
  };

  class Delegate {
   public:
    // Called when the auth flow fails. This means that the flow did not result
    // in a successful redirect to a valid redirect URL.
    virtual void OnAuthFlowFailure(Failure failure) = 0;
    // Called on redirects and other navigations to see if the URL should stop
    // the flow.
    virtual void OnAuthFlowURLChange(const GURL& redirect_url) {}
    // Called when the title of the current page changes.
    virtual void OnAuthFlowTitleChange(const std::string& title) {}

   protected:
    virtual ~Delegate() {}
  };

  // Creates an instance with the given parameters.
  // Caller owns |delegate|.
  WebAuthFlow(Delegate* delegate,
              Profile* profile,
              const GURL& provider_url,
              Mode mode,
              Partition partition);

  WebAuthFlow(const WebAuthFlow&) = delete;
  WebAuthFlow& operator=(const WebAuthFlow&) = delete;

  ~WebAuthFlow() override;

  // Starts the flow.
  virtual void Start();

  // Prevents further calls to the delegate and deletes the flow.
  void DetachDelegateAndDelete();

  // Returns a StoragePartition of the guest webview. Used to inject cookies
  // into Gaia page. Can override for testing.
  virtual content::StoragePartition* GetGuestPartition();

  // Returns an ID string attached to the window. Can override for testing.
  virtual const std::string& GetAppWindowKey() const;

  // Returns the StoragePartitionConfig for a given |partition| used in the
  // WebAuthFlow.
  static content::StoragePartitionConfig GetWebViewPartitionConfig(
      Partition partition,
      content::BrowserContext* browser_context);

 private:
  friend class ::WebAuthFlowTest;

  // ::AppWindowRegistry::Observer implementation.
  void OnAppWindowAdded(AppWindow* app_window) override;
  void OnAppWindowRemoved(AppWindow* app_window) override;

  // WebContentsObserver implementation.
  void DidStopLoading() override;
  void InnerWebContentsCreated(
      content::WebContents* inner_web_contents) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidRedirectNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  void BeforeUrlLoaded(const GURL& url);
  void AfterUrlLoaded();

  raw_ptr<Delegate> delegate_;
  raw_ptr<Profile> profile_;
  GURL provider_url_;
  Mode mode_;
  Partition partition_;

  raw_ptr<AppWindow> app_window_;
  std::string app_window_key_;
  bool embedded_window_created_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_IDENTITY_WEB_AUTH_FLOW_H_