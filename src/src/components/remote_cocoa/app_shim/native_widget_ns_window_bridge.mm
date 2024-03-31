// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/raw_ptr.h"

#import "components/remote_cocoa/app_shim/native_widget_ns_window_bridge.h"

#import <objc/runtime.h>
#include <stddef.h>
#include <stdint.h>

#include <cmath>
#include <memory>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#import "base/mac/foundation_util.h"
#include "base/mac/mac_util.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#import "components/remote_cocoa/app_shim/bridged_content_view.h"
#import "components/remote_cocoa/app_shim/browser_native_widget_window_mac.h"
#import "components/remote_cocoa/app_shim/certificate_viewer.h"
#import "components/remote_cocoa/app_shim/mouse_capture.h"
#import "components/remote_cocoa/app_shim/native_widget_mac_frameless_nswindow.h"
#import "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#import "components/remote_cocoa/app_shim/native_widget_ns_window_host_helper.h"
#include "components/remote_cocoa/app_shim/select_file_dialog_bridge.h"
#import "components/remote_cocoa/app_shim/views_nswindow_delegate.h"
#import "components/remote_cocoa/app_shim/window_controls_overlay_nsview.h"
#import "components/remote_cocoa/app_shim/window_move_loop.h"
#include "components/remote_cocoa/common/native_widget_ns_window_host.mojom.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "ui/accelerated_widget_mac/window_resize_helper_mac.h"
#include "ui/base/cocoa/cocoa_base_utils.h"
#import "ui/base/cocoa/constrained_window/constrained_window_animation.h"
#include "ui/base/cocoa/cursor_utils.h"
#include "ui/base/cocoa/remote_accessibility_api.h"
#import "ui/base/cocoa/window_size_constants.h"
#include "ui/base/emoji/emoji_panel_helper.h"
#include "ui/base/hit_test.h"
#include "ui/base/layout.h"
#include "ui/base/ui_base_switches.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/cocoa/cocoa_event_utils.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/geometry/size_conversions.h"
#import "ui/gfx/mac/coordinate_conversion.h"
#import "ui/gfx/mac/nswindow_frame_controls.h"

using remote_cocoa::mojom::VisibilityTransition;
using remote_cocoa::mojom::WindowVisibilityState;

namespace {
constexpr auto kUIPaintTimeout = base::Seconds(5);

// Returns the display that the specified window is on.
display::Display GetDisplayForWindow(NSWindow* window) {
  return display::Screen::GetScreen()->GetDisplayNearestWindow(window);
}

}  // namespace

// The NSView that hosts the composited CALayer drawing the UI. It fills the
// window but is not hittable so that accessibility hit tests always go to the
// BridgedContentView.
@interface ViewsCompositorSuperview : NSView
@end

@implementation ViewsCompositorSuperview
- (NSView*)hitTest:(NSPoint)aPoint {
  return nil;
}
@end

// Self-owning animation delegate that starts a hide animation, then calls
// -[NSWindow close] when the animation ends, releasing itself.
@interface ViewsNSWindowCloseAnimator : NSObject <NSAnimationDelegate> {
 @private
  base::scoped_nsobject<NSWindow> _window;
  base::scoped_nsobject<NSAnimation> _animation;
}
+ (void)closeWindowWithAnimation:(NSWindow*)window;
@end

@implementation ViewsNSWindowCloseAnimator

- (instancetype)initWithWindow:(NSWindow*)window {
  if ((self = [super init])) {
    _window.reset([window retain]);
    _animation.reset(
        [[ConstrainedWindowAnimationHide alloc] initWithWindow:window]);
    [_animation setDelegate:self];
    [_animation setAnimationBlockingMode:NSAnimationNonblocking];
    [_animation startAnimation];
  }
  return self;
}

+ (void)closeWindowWithAnimation:(NSWindow*)window {
  [[ViewsNSWindowCloseAnimator alloc] initWithWindow:window];
}

- (void)animationDidEnd:(NSAnimation*)animation {
  [_window close];
  [_animation setDelegate:nil];
  [self release];
}
@end

// This class overrides NSAnimation methods to invalidate the shadow for each
// frame. It is required because the show animation uses CGSSetWindowWarp()
// which is touchy about the consistency of the points it is given. The show
// animation includes a translate, which fails to apply properly to the window
// shadow, when that shadow is derived from a layer-hosting view. So invalidate
// it. This invalidation is only needed to cater for the translate. It is not
// required if CGSSetWindowWarp() is used in a way that keeps the center point
// of the window stationary (e.g. a scale). It's also not required for the hide
// animation: in that case, the shadow is never invalidated so retains the
// shadow calculated before a translate is applied.
@interface ModalShowAnimationWithLayer
    : ConstrainedWindowAnimationShow <NSAnimationDelegate>
@end

@implementation ModalShowAnimationWithLayer {
  // This is the "real" delegate, but this class acts as the NSAnimationDelegate
  // to avoid a separate object.
  raw_ptr<remote_cocoa::NativeWidgetNSWindowBridge> _bridgedNativeWidget;
}
- (instancetype)initWithBridgedNativeWidget:
    (remote_cocoa::NativeWidgetNSWindowBridge*)widget {
  if ((self = [super initWithWindow:widget->ns_window()])) {
    _bridgedNativeWidget = widget;
    [self setDelegate:self];
  }
  return self;
}
- (void)dealloc {
  DCHECK(!_bridgedNativeWidget);
  [super dealloc];
}
- (void)animationDidEnd:(NSAnimation*)animation {
  DCHECK(_bridgedNativeWidget);
  _bridgedNativeWidget->OnShowAnimationComplete();
  _bridgedNativeWidget = nullptr;
  [self setDelegate:nil];
}
- (void)stopAnimation {
  [super stopAnimation];
  [_window invalidateShadow];
}
- (void)setCurrentProgress:(NSAnimationProgress)progress {
  [super setCurrentProgress:progress];
  [_window invalidateShadow];
}
@end

namespace remote_cocoa {

namespace {

using RankMap = std::map<NSView*, int>;

// Return the content size for a minimum or maximum widget size.
gfx::Size GetClientSizeForWindowSize(NSWindow* window,
                                     const gfx::Size& window_size) {
  NSRect frame_rect =
      NSMakeRect(0, 0, window_size.width(), window_size.height());
  // Note gfx::Size will prevent dimensions going negative. They are allowed to
  // be zero at this point, because Widget::GetMinimumSize() may later increase
  // the size.
  return gfx::Size([window contentRectForFrameRect:frame_rect].size);
}

NSComparisonResult SubviewSorter(__kindof NSView* lhs,
                                 __kindof NSView* rhs,
                                 void* rank_as_void) {
  DCHECK_NE(lhs, rhs);

  if ([lhs isKindOfClass:[ViewsCompositorSuperview class]])
    return NSOrderedAscending;

  const RankMap* rank = static_cast<const RankMap*>(rank_as_void);
  auto left_rank = rank->find(lhs);
  auto right_rank = rank->find(rhs);
  bool left_found = left_rank != rank->end();
  bool right_found = right_rank != rank->end();

  // Sort unassociated views above associated views.
  if (left_found != right_found)
    return left_found ? NSOrderedAscending : NSOrderedDescending;

  if (left_found) {
    return left_rank->second < right_rank->second ? NSOrderedAscending
                                                  : NSOrderedDescending;
  }

  // If both are unassociated, consider that order is not important
  return NSOrderedSame;
}

// Counts windows managed by a NativeWidgetNSWindowBridge instance in the
// |child_windows| array ignoring the windows added by AppKit.
NSUInteger CountBridgedWindows(NSArray* child_windows) {
  NSUInteger count = 0;
  for (NSWindow* child in child_windows)
    if ([[child delegate] isKindOfClass:[ViewsNSWindowDelegate class]])
      ++count;

  return count;
}

std::map<uint64_t, NativeWidgetNSWindowBridge*>& GetIdToWidgetImplMap() {
  static base::NoDestructor<std::map<uint64_t, NativeWidgetNSWindowBridge*>>
      id_map;
  return *id_map;
}

std::map<NSWindow*, std::u16string>& GetPendingWindowTitleMap() {
  static base::NoDestructor<std::map<NSWindow*, std::u16string>> map;
  return *map;
}

}  // namespace

// static
gfx::Size NativeWidgetNSWindowBridge::GetWindowSizeForClientSize(
    NSWindow* window,
    const gfx::Size& content_size) {
  NSRect content_rect =
      NSMakeRect(0, 0, content_size.width(), content_size.height());
  NSRect frame_rect = [window frameRectForContentRect:content_rect];
  return gfx::Size(NSWidth(frame_rect), NSHeight(frame_rect));
}

// static
NativeWidgetNSWindowBridge* NativeWidgetNSWindowBridge::GetFromId(
    uint64_t bridged_native_widget_id) {
  auto found = GetIdToWidgetImplMap().find(bridged_native_widget_id);
  if (found == GetIdToWidgetImplMap().end())
    return nullptr;
  return found->second;
}

// static
NativeWidgetNSWindowBridge* NativeWidgetNSWindowBridge::GetFromNativeWindow(
    gfx::NativeWindow native_window) {
  NSWindow* window = native_window.GetNativeNSWindow();
  if (NativeWidgetMacNSWindow* widget_window =
          base::mac::ObjCCast<NativeWidgetMacNSWindow>(window)) {
    return GetFromId([widget_window bridgedNativeWidgetId]);
  }
  return nullptr;
}

// static
base::scoped_nsobject<NativeWidgetMacNSWindow>
NativeWidgetNSWindowBridge::CreateNSWindow(
    const mojom::CreateWindowParams* params) {
  base::scoped_nsobject<NativeWidgetMacNSWindow> ns_window;
  switch (params->window_class) {
    case mojom::WindowClass::kDefault:
      ns_window.reset([[NativeWidgetMacNSWindow alloc]
          initWithContentRect:ui::kWindowSizeDeterminedLater
                    styleMask:params->style_mask
                      backing:NSBackingStoreBuffered
                        defer:NO]);
      break;
    case mojom::WindowClass::kBrowser:
      ns_window.reset([[BrowserNativeWidgetWindow alloc]
          initWithContentRect:ui::kWindowSizeDeterminedLater
                    styleMask:params->style_mask
                      backing:NSBackingStoreBuffered
                        defer:NO]);
      break;
    case mojom::WindowClass::kFrameless:
      ns_window.reset([[NativeWidgetMacFramelessNSWindow alloc]
          initWithContentRect:ui::kWindowSizeDeterminedLater
                    styleMask:params->style_mask
                      backing:NSBackingStoreBuffered
                        defer:NO]);
      break;
  }

  if (params->titlebar_appears_transparent)
    [ns_window setTitlebarAppearsTransparent:YES];
  if (params->window_title_hidden)
    [ns_window setTitleVisibility:NSWindowTitleHidden];
  if (params->animation_enabled)
    [ns_window setAnimationBehavior:NSWindowAnimationBehaviorDocumentWindow];

  return ns_window;
}

NativeWidgetNSWindowBridge::NativeWidgetNSWindowBridge(
    uint64_t bridged_native_widget_id,
    NativeWidgetNSWindowHost* host,
    NativeWidgetNSWindowHostHelper* host_helper,
    mojom::TextInputHost* text_input_host)
    : id_(bridged_native_widget_id),
      host_(host),
      host_helper_(host_helper),
      text_input_host_(text_input_host),
      ns_weak_factory_(this) {
  DCHECK(GetIdToWidgetImplMap().find(id_) == GetIdToWidgetImplMap().end());
  GetIdToWidgetImplMap().insert(std::make_pair(id_, this));
}

NativeWidgetNSWindowBridge::~NativeWidgetNSWindowBridge() {
  SetLocalEventMonitorEnabled(false);
  DCHECK(!key_down_event_monitor_);
  GetPendingWindowTitleMap().erase(window_.get());
  // The delegate should be cleared already. Note this enforces the precondition
  // that -[NSWindow close] is invoked on the hosted window before the
  // destructor is called.
  DCHECK(![window_ delegate]);
  DCHECK(child_windows_.empty());
  DestroyContentView();
}

void NativeWidgetNSWindowBridge::BindReceiver(
    mojo::PendingAssociatedReceiver<mojom::NativeWidgetNSWindow> receiver,
    base::OnceClosure connection_closed_callback) {
  bridge_mojo_receiver_.Bind(std::move(receiver),
                             ui::WindowResizeHelperMac::Get()->task_runner());
  bridge_mojo_receiver_.set_disconnect_handler(
      std::move(connection_closed_callback));
}

void NativeWidgetNSWindowBridge::SetWindow(
    base::scoped_nsobject<NativeWidgetMacNSWindow> window) {
  DCHECK(!window_);
  window_delegate_.reset(
      [[ViewsNSWindowDelegate alloc] initWithBridgedNativeWidget:this]);
  window_ = std::move(window);
  [window_ setBridge:this];
  [window_ setBridgedNativeWidgetId:id_];
  [window_ setReleasedWhenClosed:NO];  // Owned by scoped_nsobject.
  [window_ setDelegate:window_delegate_];
  ui::CATransactionCoordinator::Get().AddPreCommitObserver(this);
}

void NativeWidgetNSWindowBridge::SetCommandDispatcher(
    NSObject<CommandDispatcherDelegate>* delegate,
    id<UserInterfaceItemCommandHandler> command_handler) {
  window_command_dispatcher_delegate_.reset([delegate retain]);
  [window_ setCommandDispatcherDelegate:delegate];
  [window_ setCommandHandler:command_handler];
}

void NativeWidgetNSWindowBridge::SetParent(uint64_t new_parent_id) {
  // Remove from the old parent.
  if (parent_) {
    parent_->RemoveChildWindow(this);
    parent_ = nullptr;
  }
  if (!new_parent_id)
    return;

  // It is only valid to have a NativeWidgetMac be the parent of another
  // NativeWidgetMac.
  NativeWidgetNSWindowBridge* new_parent =
      NativeWidgetNSWindowBridge::GetFromId(new_parent_id);
  DCHECK(new_parent);

  // If the parent is another NativeWidgetNSWindowBridge, just add to the
  // collection of child windows it owns and manages. Otherwise, create an
  // adapter to anchor the child widget and observe when the parent NSWindow is
  // closed.
  parent_ = new_parent;
  parent_->child_windows_.push_back(this);

  // Widget::ShowInactive() could result in a Space switch when the widget has a
  // parent, and we're calling -orderWindow:relativeTo:. Use Transient
  // collection behaviour to prevent that.
  // https://crbug.com/697829
  [window_ setCollectionBehavior:[window_ collectionBehavior] |
                                 NSWindowCollectionBehaviorTransient];

  if (wants_to_be_visible_)
    parent_->OrderChildren();
}

void NativeWidgetNSWindowBridge::CreateSelectFileDialog(
    mojo::PendingReceiver<mojom::SelectFileDialog> receiver) {
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<remote_cocoa::SelectFileDialogBridge>(window_),
      std::move(receiver));
}

void NativeWidgetNSWindowBridge::ShowCertificateViewer(
    const scoped_refptr<net::X509Certificate>& certificate) {
  ShowCertificateViewerForWindow(window_, certificate.get());
}

void NativeWidgetNSWindowBridge::StackAbove(uint64_t sibling_id) {
  NativeWidgetNSWindowBridge* sibling_bridge =
      NativeWidgetNSWindowBridge::GetFromId(sibling_id);
  DCHECK(sibling_bridge);

  NSInteger sibling = sibling_bridge->ns_window().windowNumber;
  [window_ reallyOrderWindow:NSWindowAbove relativeTo:sibling];
}

void NativeWidgetNSWindowBridge::StackAtTop() {
  [window_ reallyOrderWindow:NSWindowAbove relativeTo:0];
}

void NativeWidgetNSWindowBridge::ShowEmojiPanel() {
  ui::ShowEmojiPanel();
}

void NativeWidgetNSWindowBridge::CreateWindow(
    mojom::CreateWindowParamsPtr params) {
  SetWindow(CreateNSWindow(params.get()));
}

void NativeWidgetNSWindowBridge::InitWindow(
    mojom::NativeWidgetNSWindowInitParamsPtr params) {
  modal_type_ = params->modal_type;
  is_translucent_window_ = params->is_translucent;
  pending_restoration_data_ = params->state_restoration_data;

  if (params->is_headless_mode_window)
    headless_mode_window_ = absl::make_optional<HeadlessModeWindow>();

  // Register for application hide notifications so that visibility can be
  // properly tracked. This is not done in the delegate so that the lifetime is
  // tied to the C++ object, rather than the delegate (which may be reference
  // counted). This is required since the application hides do not send an
  // orderOut: to individual windows. Unhide, however, does send an order
  // message.
  [[NSNotificationCenter defaultCenter]
      addObserver:window_delegate_
         selector:@selector(onWindowOrderChanged:)
             name:NSApplicationDidHideNotification
           object:nil];

  [[NSNotificationCenter defaultCenter]
      addObserver:window_delegate_
         selector:@selector(onSystemControlTintChanged:)
             name:NSControlTintDidChangeNotification
           object:nil];

  // Validate the window's initial state, otherwise the bridge's initial
  // tracking state will be incorrect.
  DCHECK(![window_ isVisible]);
  DCHECK_EQ(0u, [window_ styleMask] & NSWindowStyleMaskFullScreen);

  // Include "regular" windows without the standard frame in the window cycle.
  // These use NSWindowStyleMaskBorderless so do not get it by default.
  if (params->force_into_collection_cycle) {
    [window_
        setCollectionBehavior:[window_ collectionBehavior] |
                              NSWindowCollectionBehaviorParticipatesInCycle];
  }

  [window_ setHasShadow:params->has_window_server_shadow];

  // Don't allow dragging sheets.
  if (params->modal_type == ui::MODAL_TYPE_WINDOW)
    [window_ setMovable:NO];
  [window_ setIsTooltip:params->is_tooltip];
}

void NativeWidgetNSWindowBridge::SetInitialBounds(
    const gfx::Rect& new_bounds,
    const gfx::Size& minimum_content_size) {
  gfx::Rect adjusted_bounds = new_bounds;
  if (new_bounds.IsEmpty()) {
    // If a position is set, but no size, complain. Otherwise, a 1x1 window
    // would appear there, which might be unexpected.
    DCHECK(new_bounds.origin().IsOrigin())
        << "Zero-sized windows not supported on Mac.";

    // Otherwise, bounds is all zeroes. Cocoa will currently have the window at
    // the bottom left of the screen. To support a client calling SetSize() only
    // (and for consistency across platforms) put it at the top-left instead.
    // Read back the current frame: it will be a 1x1 context rect but the frame
    // size also depends on the window style.
    NSRect frame_rect = [window_ frame];
    adjusted_bounds = gfx::Rect(
        gfx::Point(), gfx::Size(NSWidth(frame_rect), NSHeight(frame_rect)));
  }
  SetBounds(adjusted_bounds, minimum_content_size);
}

void NativeWidgetNSWindowBridge::SetBounds(
    const gfx::Rect& new_bounds,
    const gfx::Size& minimum_content_size) {
  // -[NSWindow contentMinSize] is only checked by Cocoa for user-initiated
  // resizes. This is not what toolkit-views expects, so clamp. Note there is
  // no check for maximum size (consistent with aura::Window::SetBounds()).
  gfx::Size clamped_content_size =
      GetClientSizeForWindowSize(window_, new_bounds.size());
  clamped_content_size.SetToMax(minimum_content_size);

  // A contentRect with zero width or height is a banned practice in ChromeMac,
  // due to unpredictable OSX treatment.
  DCHECK(!clamped_content_size.IsEmpty())
      << "Zero-sized windows not supported on Mac";

  if (!window_visible_ && IsWindowModalSheet()) {
    // Window-Modal dialogs (i.e. sheets) are positioned by Cocoa when shown for
    // the first time. They also have no frame, so just update the content size.
    [window_ setContentSize:NSMakeSize(clamped_content_size.width(),
                                       clamped_content_size.height())];
    return;
  }
  gfx::Rect actual_new_bounds(
      new_bounds.origin(),
      GetWindowSizeForClientSize(window_, clamped_content_size));

  NSScreen* previous_screen = [window_ screen];

  [window_ setFrame:gfx::ScreenRectToNSRect(actual_new_bounds)
            display:YES
            animate:NO];

  // If the window has focus but is not on the active space and the window was
  // moved to a different display, re-activate it to switch the space to the
  // active window. (crbug.com/1316543)
  if ([window_ isKeyWindow] && ![window_ isOnActiveSpace] &&
      [window_ screen] != previous_screen) {
    SetVisibilityState(WindowVisibilityState::kShowAndActivateWindow);
  }
}

void NativeWidgetNSWindowBridge::SetSizeAndCenter(
    const gfx::Size& content_size,
    const gfx::Size& minimum_content_size) {
  gfx::Rect new_window_bounds = gfx::ScreenRectFromNSRect([window_ frame]);
  new_window_bounds.set_size(GetWindowSizeForClientSize(window_, content_size));
  SetBounds(new_window_bounds, minimum_content_size);

  // Note that this is not the precise center of screen, but it is the standard
  // location for windows like dialogs to appear on screen for Mac.
  // TODO(tapted): If there is a parent window, center in that instead.
  [window_ center];
}

void NativeWidgetNSWindowBridge::DestroyContentView() {
  if (!bridged_view_)
    return;
  [bridged_view_ clearView];
  bridged_view_id_mapping_.reset();
  bridged_view_.reset();
  [window_ setContentView:nil];
}

void NativeWidgetNSWindowBridge::CreateContentView(uint64_t ns_view_id,
                                                   const gfx::Rect& bounds) {
  DCHECK(!bridged_view_);

  bridged_view_.reset([[BridgedContentView alloc] initWithBridge:this
                                                          bounds:bounds]);
  bridged_view_id_mapping_ =
      std::make_unique<ScopedNSViewIdMapping>(ns_view_id, bridged_view_.get());

  // Objective C initializers can return nil. However, if |view| is non-NULL
  // this should be treated as an error and caught early.
  CHECK(bridged_view_);

  // Send the accessibility tokens for the NSView now that it exists.
  host_->SetRemoteAccessibilityTokens(
      ui::RemoteAccessibility::GetTokenForLocalElement(window_),
      ui::RemoteAccessibility::GetTokenForLocalElement(bridged_view_));

  // Beware: This view was briefly removed (in favor of a bare CALayer) in
  // crrev/c/1236675. The ordering of unassociated layers relative to NSView
  // layers is undefined on macOS 10.12 and earlier, so the compositor layer
  // ended up covering up subviews (see crbug/899499).
  base::scoped_nsobject<NSView> compositor_view(
      [[ViewsCompositorSuperview alloc] initWithFrame:[bridged_view_ bounds]]);
  [compositor_view
      setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  auto* background_layer = [CALayer layer];
  display_ca_layer_tree_ =
      std::make_unique<ui::DisplayCALayerTree>(background_layer);
  [compositor_view setLayer:background_layer];
  [compositor_view setWantsLayer:YES];
  [bridged_view_ addSubview:compositor_view];

  [bridged_view_ setWantsLayer:YES];
  [window_ setContentView:bridged_view_];
}

void NativeWidgetNSWindowBridge::CloseWindow() {
  if (fullscreen_controller_.HasDeferredWindowClose())
    return;

  // Keep |window| on the stack so that the ObjectiveC block below can capture
  // it and properly increment the reference count bound to the posted task.
  NSWindow* window = ns_window();

  if (IsWindowModalSheet() && [ns_window() isSheet]) {
    // Sheets can't be closed normally. This starts the sheet closing. Once the
    // sheet has finished animating, it will call sheetDidEnd: on the parent
    // window's delegate. Note it still needs to be asynchronous, since code
    // calling Widget::Close() doesn't expect things to be deleted upon return.
    // Ensure |window| is retained by a block. Note in some cases during
    // teardown, [window sheetParent] may be nil.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(base::RetainBlock(^{
          [NSApp endSheet:window];
        })));
    return;
  }

  // For other modal types, animate the close.
  if (ShouldRunCustomAnimationFor(VisibilityTransition::kHide) &&
      [ns_window() isVisible]) {
    [ViewsNSWindowCloseAnimator closeWindowWithAnimation:window];
    return;
  }

  // Destroy the content view so that it won't call back into |host_| while
  // being torn down.
  DestroyContentView();

  // If the window wants to be visible and has a parent, then the parent may
  // order it back in (in the period between orderOut: and close).
  wants_to_be_visible_ = false;

  // Widget::Close() ensures [Non]ClientView::CanClose() returns true, so there
  // is no need to call the NSWindow or its delegate's -windowShouldClose:
  // implementation in the manner of -[NSWindow performClose:]. But,
  // like -performClose:, first remove the window from AppKit's display
  // list to avoid crashes like http://crbug.com/156101.
  [window orderOut:nil];

  // Defer closing windows until after fullscreen transitions complete.
  fullscreen_controller_.OnWindowWantsToClose();
  if (fullscreen_controller_.HasDeferredWindowClose())
    return;

  // Many tests assume that base::RunLoop().RunUntilIdle() is always sufficient
  // to execute a close. However, in rare cases, -performSelector:..afterDelay:0
  // does not do this. So post a regular task.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(base::RetainBlock(^{
        [window close];
      })));
}

void NativeWidgetNSWindowBridge::CloseWindowNow() {
  // NSWindows must be retained until -[NSWindow close] returns.
  auto window_retain = window_;

  // If there's a bridge at this point, it means there must be a window as well.
  DCHECK(window_);
  [window_ close];
  // Note: |this| will be deleted here.
}

void NativeWidgetNSWindowBridge::SetVisibilityState(
    WindowVisibilityState new_state) {
  // In headless mode the platform window is always hidden, so instead of
  // changing its visibility state just maintain a local flag to track the
  // expected visibility state and lie to the upper layer pretending the
  // window did change its visibility state.
  if (headless_mode_window_) {
    headless_mode_window_->visibility_state =
        new_state != WindowVisibilityState::kHideWindow;
    host_->OnVisibilityChanged(headless_mode_window_->visibility_state);
    return;
  }

  // During session restore this method gets called from RestoreTabsToBrowser()
  // with new_state = kShowAndActivateWindow. We consume restoration data on our
  // first time through this method so we can use its existence as an
  // indication that session restoration is underway. We'll use this later to
  // decide whether or not to actually honor the WindowVisibilityState change
  // request. This window may live in the dock, for example, in which case we
  // don't really want to kShowAndActivateWindow. Even if the window is on the
  // desktop we still won't want to kShowAndActivateWindow because doing so
  // might trigger a transition to a different space (and we don't want to
  // switch spaces on start-up). When session restore determines the Active
  // window it will also call SetVisibilityState(), on that pass the window
  // can/will be activated.
  bool session_restore_in_progress = false;

  // Restore Cocoa window state.
  if (HasWindowRestorationData()) {
    NSData* restore_ns_data =
        [NSData dataWithBytes:pending_restoration_data_.data()
                       length:pending_restoration_data_.size()];
    base::scoped_nsobject<NSKeyedUnarchiver> decoder([[NSKeyedUnarchiver alloc]
        initForReadingFromData:restore_ns_data
                         error:nil]);
    [window_ restoreStateWithCoder:decoder];
    pending_restoration_data_.clear();

    session_restore_in_progress = true;
  }

  // Ensure that:
  //  - A window with an invisible parent is not made visible.
  //  - A parent changing visibility updates child window visibility.
  //    * But only when changed via this function - ignore changes via the
  //      NSWindow API, or changes propagating out from here.
  wants_to_be_visible_ = new_state != WindowVisibilityState::kHideWindow;

  [show_animation_ stopAnimation];
  DCHECK(!show_animation_);

  if (new_state == WindowVisibilityState::kHideWindow) {
    // Calling -orderOut: on a window with an attached sheet encounters broken
    // AppKit behavior. The sheet effectively becomes "lost".
    // See http://crbug.com/667602. Alternatives: call -setAlphaValue:0 and
    // -setIgnoresMouseEvents:YES on the NSWindow, or dismiss the sheet before
    // hiding.
    //
    // TODO(ellyjones): Sort this entire situation out. This DCHECK doesn't
    // trigger in shipped builds, but it does trigger when the browser exits
    // "abnormally" (not via one of the UI paths to exiting), such as in browser
    // tests, so this breaks a slew of browser tests in MacViews mode. See also
    // https://crbug.com/834926.
    // DCHECK(![window_ attachedSheet]);

    [window_ orderOut:nil];
    DCHECK(!window_visible_);
    return;
  }

  DCHECK(wants_to_be_visible_);

  if (!ca_transaction_sync_suppressed_)
    ui::CATransactionCoordinator::Get().Synchronize();

  // If the parent (or an ancestor) is hidden, return and wait for it to become
  // visible.
  for (auto* ancestor = parent_.get(); ancestor; ancestor = ancestor->parent_) {
    if (!ancestor->window_visible_)
      return;
  }

  // Don't activate a window during session restore, to avoid switching spaces
  // (or pulling it out of the dock) during startup.
  if (session_restore_in_progress &&
      new_state == WindowVisibilityState::kShowAndActivateWindow) {
    new_state = WindowVisibilityState::kShowInactive;
  }

  if (IsWindowModalSheet()) {
    ShowAsModalSheet();
    return;
  }

  if (parent_)
    parent_->OrderChildren();

  if (new_state == WindowVisibilityState::kShowAndActivateWindow) {
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  } else if (new_state == WindowVisibilityState::kShowInactive && !parent_ &&
             ![window_ isMiniaturized]) {
    if ([[NSApp mainWindow] screen] == [window_ screen] ||
        ![[NSApp mainWindow] isKeyWindow]) {
      // When the new window is on the same display as the main window or the
      // main window is inactive, order the window relative to the main window.
      // Avoid making it the front window (with e.g. orderFront:), which can
      // cause a space switch.
      [window_ orderWindow:NSWindowBelow
                relativeTo:NSApp.mainWindow.windowNumber];
    } else {
      // When opening an inactive window on another screen, put the window at
      // the front and trigger a space switch.
      [window_ orderFrontKeepWindowKeyState];
    }
  }

  // For non-sheet modal types, use the constrained window animations to make
  // the window appear.
  if (ShouldRunCustomAnimationFor(VisibilityTransition::kShow)) {
    show_animation_.reset(
        [[ModalShowAnimationWithLayer alloc] initWithBridgedNativeWidget:this]);

    // The default mode is blocking, which would block the UI thread for the
    // duration of the animation, but would keep it smooth. The window also
    // hasn't yet received a frame from the compositor at this stage, so it is
    // fully transparent until the GPU sends a frame swap IPC. For the blocking
    // option, the animation needs to wait until
    // AcceleratedWidgetCALayerParamsUpdated has been called at least once,
    // otherwise it will animate nothing.
    [show_animation_ setAnimationBlockingMode:NSAnimationNonblocking];
    [show_animation_ startAnimation];
  }
}

void NativeWidgetNSWindowBridge::SetTransitionsToAnimate(
    VisibilityTransition transitions) {
  // TODO(tapted): Use scoping to disable native animations at appropriate
  // times as well.
  transitions_to_animate_ = transitions;
}

void NativeWidgetNSWindowBridge::AcquireCapture() {
  if (HasCapture())
    return;
  if (!window_visible_)
    return;  // Capture on hidden windows is disallowed.

  mouse_capture_ = std::make_unique<CocoaMouseCapture>(this);
  host_->OnMouseCaptureActiveChanged(true);

  // Initiating global event capture with addGlobalMonitorForEventsMatchingMask:
  // will reset the mouse cursor to an arrow. Asking the window for an update
  // here will restore what we want. However, it can sometimes cause the cursor
  // to flicker, once, on the initial mouseDown.
  // TODO(tapted): Make this unnecessary by only asking for global mouse capture
  // for the cases that need it (e.g. menus, but not drag and drop).
  [window_ cursorUpdate:[NSApp currentEvent]];
}

void NativeWidgetNSWindowBridge::ReleaseCapture() {
  mouse_capture_.reset();
}

bool NativeWidgetNSWindowBridge::HasCapture() {
  return mouse_capture_ && mouse_capture_->IsActive();
}

void NativeWidgetNSWindowBridge::SetLocalEventMonitorEnabled(bool enabled) {
  if (enabled) {
    // Create the event montitor if it does not exist yet.
    if (key_down_event_monitor_)
      return;

    // Capture a WeakPtr via NSObject. This allows the block to detect another
    // event monitor for the same event deleting `this`.
    WeakPtrNSObject* handle = ns_weak_factory_.handle();
    auto block = ^NSEvent*(NSEvent* event) {
      auto* bridge =
          ui::WeakPtrNSObjectFactory<NativeWidgetNSWindowBridge>::Get(handle);
      if (!bridge)
        return event;
      std::unique_ptr<ui::Event> ui_event = ui::EventFromNative(event);
      bool event_handled = false;
      bridge->host_->DispatchMonitorEvent(std::move(ui_event), &event_handled);
      return event_handled ? nil : event;
    };
    key_down_event_monitor_ =
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                              handler:block];
  } else {
    // Destroy the event monitor if it exists.
    if (!key_down_event_monitor_)
      return;

    [NSEvent removeMonitor:key_down_event_monitor_];
    key_down_event_monitor_ = nil;
  }
}

bool NativeWidgetNSWindowBridge::HasWindowRestorationData() {
  return !pending_restoration_data_.empty();
}

bool NativeWidgetNSWindowBridge::RunMoveLoop(const gfx::Vector2d& drag_offset) {
  // https://crbug.com/876493
  CHECK(!HasCapture());
  // Does some *other* widget have capture?
  CHECK(!CocoaMouseCapture::GetGlobalCaptureWindow());
  CHECK(!window_move_loop_);

  // RunMoveLoop caller is responsible for updating the window to be under the
  // mouse, but it does this using possibly outdated coordinate from the mouse
  // event, and mouse is very likely moved beyound that point.

  // Compensate for mouse drift by shifting the initial mouse position we pass
  // to CocoaWindowMoveLoop, so as it handles incoming move events the window's
  // top left corner will be |drag_offset| from the current mouse position.

  const gfx::Rect frame = gfx::ScreenRectFromNSRect([window_ frame]);
  const gfx::Point mouse_in_screen(frame.x() + drag_offset.x(),
                                   frame.y() + drag_offset.y());
  window_move_loop_ = std::make_unique<CocoaWindowMoveLoop>(
      this, gfx::ScreenPointToNSPoint(mouse_in_screen));

  return window_move_loop_->Run();

  // |this| may be destroyed during the RunLoop, causing it to exit early.
  // Even if that doesn't happen, CocoaWindowMoveLoop will clean itself up by
  // calling EndMoveLoop(). So window_move_loop_ will always be null before the
  // function returns. But don't DCHECK since |this| might not be valid.
}

void NativeWidgetNSWindowBridge::EndMoveLoop() {
  DCHECK(window_move_loop_);
  window_move_loop_->End();
  window_move_loop_.reset();
}

void NativeWidgetNSWindowBridge::SetCursor(NSCursor* cursor) {
  [window_delegate_ setCursor:cursor];
}

void NativeWidgetNSWindowBridge::SetCursor(const ui::Cursor& cursor) {
  SetCursor(ui::GetNativeCursor(cursor));
}

void NativeWidgetNSWindowBridge::EnableImmersiveFullscreen(
    uint64_t fullscreen_overlay_widget_id,
    EnableImmersiveFullscreenCallback callback) {
  immersive_mode_controller_ = std::make_unique<ImmersiveModeController>(
      ns_window(), GetFromId(fullscreen_overlay_widget_id)->ns_window(),
      std::move(callback));
  immersive_mode_controller_->Enable();
}

void NativeWidgetNSWindowBridge::DisableImmersiveFullscreen() {
  immersive_mode_controller_.reset();
}

void NativeWidgetNSWindowBridge::UpdateToolbarVisibility(bool always_show) {
  immersive_mode_controller_->UpdateToolbarVisibility(always_show);
}

void NativeWidgetNSWindowBridge::OnTopContainerViewBoundsChanged(
    const gfx::Rect& bounds) {
  immersive_mode_controller_->OnTopViewBoundsChanged(bounds);
}

void NativeWidgetNSWindowBridge::SetCanGoBack(bool can_go_back) {
  can_go_back_ = can_go_back;
}

void NativeWidgetNSWindowBridge::SetCanGoForward(bool can_go_forward) {
  can_go_forward_ = can_go_forward;
}

void NativeWidgetNSWindowBridge::OnWindowWillClose() {
  fullscreen_controller_.OnWindowWillClose();

  [window_ setCommandHandler:nil];
  [window_ setCommandDispatcherDelegate:nil];

  ui::CATransactionCoordinator::Get().RemovePreCommitObserver(this);
  host_->OnWindowWillClose();

  // Ensure NativeWidgetNSWindowBridge does not have capture, otherwise
  // OnMouseCaptureLost() may reference a deleted |host_| when called via
  // ~CocoaMouseCapture() upon the destruction of |mouse_capture_|. See
  // https://crbug.com/622201. Also we do this before setting the delegate to
  // nil, because this may lead to callbacks to bridge which rely on a valid
  // delegate.
  ReleaseCapture();

  if (parent_) {
    parent_->RemoveChildWindow(this);
    parent_ = nullptr;
  }
  [[NSNotificationCenter defaultCenter] removeObserver:window_delegate_];

  [show_animation_ stopAnimation];  // If set, calls OnShowAnimationComplete().
  DCHECK(!show_animation_);

  [window_ setDelegate:nil];
  [window_ setBridge:nullptr];

  // Ensure that |this| cannot be reached by its id while it is being destroyed.
  size_t erased = GetIdToWidgetImplMap().erase(id_);
  DCHECK_EQ(1u, erased);

  RemoveOrDestroyChildren();
  DCHECK(child_windows_.empty());

  host_->OnWindowHasClosed();
  // Note: |this| and its host will be deleted here.
}

void NativeWidgetNSWindowBridge::OnSizeChanged() {
  UpdateWindowGeometry();
}

void NativeWidgetNSWindowBridge::OnPositionChanged() {
  UpdateWindowGeometry();
}

void NativeWidgetNSWindowBridge::OnVisibilityChanged() {
  const bool window_visible = [window_ isVisible];
  if (window_visible_ == window_visible)
    return;

  window_visible_ = window_visible;

  // If arriving via SetVisible(), |wants_to_be_visible_| should already be set.
  // If made visible externally (e.g. Cmd+H), just roll with it. Don't try (yet)
  // to distinguish being *hidden* externally from being hidden by a parent
  // window - we might not need that.
  if (window_visible_) {
    wants_to_be_visible_ = true;
    if (parent_ && !window_visible_)
      parent_->OrderChildren();
  } else {
    ReleaseCapture();  // Capture on hidden windows is not permitted.

    // When becoming invisible, remove the entry in any parent's childWindow
    // list. Cocoa's childWindow management breaks down when child windows are
    // hidden.
    if (parent_)
      [parent_->ns_window() removeChildWindow:window_];
  }

  // Showing a translucent window after hiding it should trigger shadow
  // invalidation.
  if (window_visible && ![window_ isOpaque])
    invalidate_shadow_on_frame_swap_ = true;

  NotifyVisibilityChangeDown();
  host_->OnVisibilityChanged(window_visible_);

  // Toolkit-views suppresses redraws while not visible. To prevent Cocoa asking
  // for an "empty" draw, disable auto-display while hidden. For example, this
  // prevents Cocoa drawing just *after* a minimize, resulting in a blank window
  // represented in the deminiaturize animation.
  [window_ setAutodisplay:window_visible_];
}

void NativeWidgetNSWindowBridge::OnSystemControlTintChanged() {
  host_->OnWindowNativeThemeChanged();
}

void NativeWidgetNSWindowBridge::OnBackingPropertiesChanged() {
  UpdateWindowDisplay();
}

void NativeWidgetNSWindowBridge::OnWindowKeyStatusChangedTo(bool is_key) {
  host_->OnWindowKeyStatusChanged(
      is_key, [window_ contentView] == [window_ firstResponder],
      [NSApp isFullKeyboardAccessEnabled]);
  // If the window just became key, this is a good chance to add child windows
  // from when the window wasn't on the current space.
  if (is_key)
    OrderChildren();
}

void NativeWidgetNSWindowBridge::SetSizeConstraints(const gfx::Size& min_size,
                                                    const gfx::Size& max_size,
                                                    bool is_resizable,
                                                    bool is_maximizable) {
  // Don't modify the size constraints or fullscreen collection behavior while
  // in fullscreen or during a transition.
  if (!fullscreen_controller_.CanResize())
    return;

  bool shows_resize_controls =
      is_resizable && (min_size.IsEmpty() || min_size != max_size);
  bool shows_fullscreen_controls = is_resizable && is_maximizable;

  gfx::ApplyNSWindowSizeConstraints(window_, min_size, max_size,
                                    shows_resize_controls,
                                    shows_fullscreen_controls);
}

void NativeWidgetNSWindowBridge::OnShowAnimationComplete() {
  show_animation_.reset();
}

void NativeWidgetNSWindowBridge::InitCompositorView(
    InitCompositorViewCallback callback) {
  // Use the regular window background for window modal sheets. The layer will
  // still paint over most of it, but the native -[NSApp beginSheet:] animation
  // blocks the UI thread, so there's no way to invalidate the shadow to match
  // the composited layer. This assumes the native window shape is a good match
  // for the composited NonClientFrameView, which should be the case since the
  // native shape is what's most appropriate for displaying sheets on Mac.
  if (is_translucent_window_ && !IsWindowModalSheet()) {
    [window_ setOpaque:NO];
    [window_ setBackgroundColor:[NSColor clearColor]];

    // Don't block waiting for the initial frame of completely transparent
    // windows. This allows us to avoid blocking on the UI thread e.g, while
    // typing in the omnibox. Note window modal sheets _must_ wait: there is no
    // way for a frame to arrive during AppKit's sheet animation.
    // https://crbug.com/712268
    ca_transaction_sync_suppressed_ = true;
  } else {
    DCHECK(!ca_transaction_sync_suppressed_);
  }

  // Send the initial window geometry and screen properties. Any future changes
  // will be forwarded.
  UpdateWindowDisplay();
  UpdateWindowGeometry();

  // Inform the browser of the CGWindowID for this NSWindow.
  std::move(callback).Run([window_ windowNumber]);
}

void NativeWidgetNSWindowBridge::SortSubviews(
    const std::vector<uint64_t>& attached_subview_ids) {
  // Ignore layer manipulation during a Close(). This can be reached during the
  // orderOut: in Close(), which notifies visibility changes to Views.
  if (!bridged_view_)
    return;
  RankMap rank;
  for (uint64_t subview_id : attached_subview_ids) {
    if (NSView* subview = remote_cocoa::GetNSViewFromId(subview_id))
      rank[subview] = rank.size() + 1;
  }
  [bridged_view_ sortSubviewsUsingFunction:&SubviewSorter context:&rank];
}

void NativeWidgetNSWindowBridge::SetAnimationEnabled(bool animate) {
  [window_
      setAnimationBehavior:(animate ? NSWindowAnimationBehaviorDocumentWindow
                                    : NSWindowAnimationBehaviorNone)];
}

bool NativeWidgetNSWindowBridge::ShouldRunCustomAnimationFor(
    VisibilityTransition transition) const {
  // The logic around this needs to change if new transition types are set.
  // E.g. it would be nice to distinguish "hide" from "close". Mac currently
  // treats "hide" only as "close". Hide (e.g. Cmd+h) should not animate on Mac.
  if (transitions_to_animate_ != transition &&
      transitions_to_animate_ != VisibilityTransition::kBoth) {
    return false;
  }

  // Custom animations are only used for tab-modals.
  bool widget_is_modal = false;
  host_->GetWidgetIsModal(&widget_is_modal);
  if (!widget_is_modal)
    return false;

  // Note this also checks the native animation property. Clearing that will
  // also disable custom animations to ensure that the views::Widget API
  // behaves consistently.
  if ([window_ animationBehavior] == NSWindowAnimationBehaviorNone)
    return false;

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableModalAnimations)) {
    return false;
  }

  return true;
}

bool NativeWidgetNSWindowBridge::RedispatchKeyEvent(NSEvent* event) {
  return [[window_ commandDispatcher] redispatchKeyEvent:event];
}

void NativeWidgetNSWindowBridge::CreateWindowControlsOverlayNSView(
    const mojom::WindowControlsOverlayNSViewType overlay_type) {
  switch (overlay_type) {
    case mojom::WindowControlsOverlayNSViewType::kCaptionButtonContainer:
      caption_buttons_overlay_nsview_.reset(
          [[WindowControlsOverlayNSView alloc] initWithBridge:this]);
      [bridged_view_ addSubview:caption_buttons_overlay_nsview_];
      break;
    case mojom::WindowControlsOverlayNSViewType::kWebAppFrameToolbar:
      web_app_frame_toolbar_overlay_nsview_.reset(
          [[WindowControlsOverlayNSView alloc] initWithBridge:this]);
      [bridged_view_ addSubview:web_app_frame_toolbar_overlay_nsview_];
      break;
  }
}

void NativeWidgetNSWindowBridge::UpdateWindowControlsOverlayNSView(
    const gfx::Rect& bounds,
    const mojom::WindowControlsOverlayNSViewType overlay_type) {
  switch (overlay_type) {
    case mojom::WindowControlsOverlayNSViewType::kCaptionButtonContainer:
      [caption_buttons_overlay_nsview_ updateBounds:bounds];
      break;
    case mojom::WindowControlsOverlayNSViewType::kWebAppFrameToolbar:
      [web_app_frame_toolbar_overlay_nsview_ updateBounds:bounds];
      break;
  }
}

void NativeWidgetNSWindowBridge::RemoveWindowControlsOverlayNSView(
    const mojom::WindowControlsOverlayNSViewType overlay_type) {
  switch (overlay_type) {
    case mojom::WindowControlsOverlayNSViewType::kCaptionButtonContainer:
      [caption_buttons_overlay_nsview_ removeFromSuperview];
      caption_buttons_overlay_nsview_.reset();
      break;
    case mojom::WindowControlsOverlayNSViewType::kWebAppFrameToolbar:
      [web_app_frame_toolbar_overlay_nsview_ removeFromSuperview];
      web_app_frame_toolbar_overlay_nsview_.reset();
      break;
  }
}

NSWindow* NativeWidgetNSWindowBridge::ns_window() {
  return window_.get();
}

DragDropClient* NativeWidgetNSWindowBridge::drag_drop_client() {
  return host_helper_->GetDragDropClient();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetNSWindowBridge, display::DisplayObserver:

void NativeWidgetNSWindowBridge::OnDisplayAdded(
    const display::Display& display) {
  UpdateWindowDisplay();
}

void NativeWidgetNSWindowBridge::OnDisplayRemoved(
    const display::Display& display) {
  UpdateWindowDisplay();
}

void NativeWidgetNSWindowBridge::OnDisplayMetricsChanged(
    const display::Display& display,
    uint32_t metrics) {
  UpdateWindowDisplay();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetNSWindowBridge, NativeWidgetNSWindowFullscreenController::Client:

void NativeWidgetNSWindowBridge::FullscreenControllerTransitionStart(
    bool is_target_fullscreen) {
  host_->OnWindowFullscreenTransitionStart(is_target_fullscreen);
}

void NativeWidgetNSWindowBridge::FullscreenControllerTransitionComplete(
    bool is_fullscreen) {
  DCHECK(!fullscreen_controller_.IsInFullscreenTransition());
  UpdateWindowGeometry();
  UpdateWindowDisplay();

  // Add any children that were skipped during the fullscreen transition.
  OrderChildren();
  host_->OnWindowFullscreenTransitionComplete(is_fullscreen);
}

void NativeWidgetNSWindowBridge::FullscreenControllerSetFrame(
    const gfx::Rect& frame,
    bool animate,
    base::OnceCallback<void()> completion_callback) {
  NSRect ns_frame = gfx::ScreenRectToNSRect(frame);
  base::TimeDelta transition_time = base::Seconds(0);
  if (animate)
    transition_time = base::Seconds([window_ animationResizeTime:ns_frame]);

  __block base::OnceCallback<void()> complete = std::move(completion_callback);
  [NSAnimationContext
      runAnimationGroup:^(NSAnimationContext* context) {
        [context setDuration:transition_time.InSecondsF()];
        [[window_ animator] setFrame:ns_frame display:YES animate:animate];
      }
      completionHandler:^{
        std::move(complete).Run();
      }];
}

void NativeWidgetNSWindowBridge::FullscreenControllerToggleFullscreen() {
  // AppKit implicitly makes the fullscreen window visible, so avoid going
  // fullscreen in headless mode. Instead, toggle the expected fullscreen state
  // and fake the relevant callbacks for the fullscreen controller to
  // believe the fullscreen state was toggled.
  if (headless_mode_window_) {
    headless_mode_window_->fullscreen_state =
        !headless_mode_window_->fullscreen_state;
    if (headless_mode_window_->fullscreen_state) {
      fullscreen_controller_.OnWindowWillEnterFullscreen();
      fullscreen_controller_.OnWindowDidEnterFullscreen();
    } else {
      fullscreen_controller_.OnWindowWillExitFullscreen();
      fullscreen_controller_.OnWindowDidExitFullscreen();
    }
    return;
  }

  bool is_key_window = [window_ isKeyWindow];
  [window_ toggleFullScreen:nil];
  // Ensure the transitioning window maintains focus (crbug.com/1338659).
  if (is_key_window)
    [window_ makeKeyAndOrderFront:nil];
}

void NativeWidgetNSWindowBridge::FullscreenControllerCloseWindow() {
  [window_ close];
}

int64_t NativeWidgetNSWindowBridge::FullscreenControllerGetDisplayId() const {
  return GetDisplayForWindow(window_.get()).id();
}

gfx::Rect NativeWidgetNSWindowBridge::FullscreenControllerGetFrameForDisplay(
    int64_t display_id) const {
  display::Display display;
  if (display::Screen::GetScreen()->GetDisplayWithDisplayId(display_id,
                                                            &display)) {
    // Use the current window size to avoid unexpected window resizes on
    // subsequent cross-screen window drag and drops; see crbug.com/1338664
    return gfx::Rect(display.work_area().origin(),
                     FullscreenControllerGetFrame().size());
  }
  return gfx::Rect();
}

gfx::Rect NativeWidgetNSWindowBridge::FullscreenControllerGetFrame() const {
  return gfx::ScreenRectFromNSRect([window_ frame]);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetNSWindowBridge, ui::CATransactionObserver

bool NativeWidgetNSWindowBridge::ShouldWaitInPreCommit() {
  if (!window_visible_)
    return false;
  if (ca_transaction_sync_suppressed_)
    return false;
  if (!bridged_view_)
    return false;
  // Suppress synchronous CA transactions during AppKit fullscreen transition
  // since there is no need for updates during such transition.
  // Re-layout and re-paint will be done after the transition. See
  // https://crbug.com/875707 for potiential problems if we don't suppress.
  if (fullscreen_controller_.IsInFullscreenTransition())
    return false;
  return content_dip_size_ != compositor_frame_dip_size_;
}

base::TimeDelta NativeWidgetNSWindowBridge::PreCommitTimeout() {
  return kUIPaintTimeout;
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetNSWindowBridge, CocoaMouseCaptureDelegate:

bool NativeWidgetNSWindowBridge::PostCapturedEvent(NSEvent* event) {
  [bridged_view_ processCapturedMouseEvent:event];
  return true;
}

void NativeWidgetNSWindowBridge::OnMouseCaptureLost() {
  host_->OnMouseCaptureActiveChanged(false);
}

NSWindow* NativeWidgetNSWindowBridge::GetWindow() const {
  return window_;
}

////////////////////////////////////////////////////////////////////////////////
// TODO(ccameron): Update class names to:
// NativeWidgetNSWindowBridge, NativeWidgetNSWindowBridge:

void NativeWidgetNSWindowBridge::SetVisibleOnAllSpaces(bool always_visible) {
  gfx::SetNSWindowVisibleOnAllWorkspaces(window_, always_visible);
}

void NativeWidgetNSWindowBridge::EnterFullscreen(int64_t target_display_id) {
  // Going fullscreen implicitly makes the window visible. AppKit does this.
  // That is, -[NSWindow isVisible] is always true after a call to -[NSWindow
  // toggleFullScreen:]. Unfortunately, this change happens after AppKit calls
  // -[NSWindowDelegate windowWillEnterFullScreen:], and AppKit doesn't send
  // an orderWindow message. So intercepting the implicit change is hard.
  // Luckily, to trigger externally, the window typically needs to be visible
  // in the first place. So we can just ensure the window is visible here
  // instead of relying on AppKit to do it, and not worry that
  // OnVisibilityChanged() won't be called for externally triggered fullscreen
  // requests.
  if (!window_visible_)
    SetVisibilityState(WindowVisibilityState::kShowInactive);

  // Enable fullscreen collection behavior because:
  // 1: -[NSWindow toggleFullscreen:] would otherwise be ignored,
  // 2: the fullscreen button must be enabled so the user can leave
  // fullscreen. This will be reset when a transition out of fullscreen
  // completes.
  gfx::SetNSWindowCanFullscreen(window_, true);

  fullscreen_controller_.EnterFullscreen(target_display_id);
}

void NativeWidgetNSWindowBridge::ExitFullscreen() {
  fullscreen_controller_.ExitFullscreen();
}

void NativeWidgetNSWindowBridge::SetCanAppearInExistingFullscreenSpaces(
    bool can_appear_in_existing_fullscreen_spaces) {
  NSWindow* window = window_.get();
  NSWindowCollectionBehavior collectionBehavior = window.collectionBehavior;
  if (can_appear_in_existing_fullscreen_spaces) {
    collectionBehavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
    collectionBehavior &= ~NSWindowCollectionBehaviorFullScreenPrimary;
  } else {
    collectionBehavior &= ~NSWindowCollectionBehaviorFullScreenAuxiliary;
  }
  window.collectionBehavior = collectionBehavior;
}

void NativeWidgetNSWindowBridge::SetMiniaturized(bool miniaturized) {
  // In headless mode the platform window is always hidden and WebKit
  // will not deminiaturize hidden windows. So instead of changing the window
  // miniaturization state just lie to the upper layer pretending the window did
  // change its state. We don't need to keep track of the requested state here
  // because the host will do this.
  if (headless_mode_window_) {
    host_->OnWindowMiniaturizedChanged(miniaturized);
    return;
  }

  if (miniaturized) {
    // Calling performMiniaturize: will momentarily highlight the button, but
    // AppKit will reject it if there is no miniaturize button.
    if ([window_ styleMask] & NSWindowStyleMaskMiniaturizable)
      [window_ performMiniaturize:nil];
    else
      [window_ miniaturize:nil];
  } else {
    [window_ deminiaturize:nil];
  }
}

void NativeWidgetNSWindowBridge::SetOpacity(float opacity) {
  [window_ setAlphaValue:opacity];
}

void NativeWidgetNSWindowBridge::SetWindowLevel(int32_t level) {
  [window_ setLevel:level];
  [bridged_view_ updateCursorTrackingArea];

  // Windows that have a higher window level than NSNormalWindowLevel default to
  // NSWindowCollectionBehaviorTransient. Set the value explicitly here to match
  // normal windows.
  NSWindowCollectionBehavior behavior =
      [window_ collectionBehavior] | NSWindowCollectionBehaviorManaged;
  [window_ setCollectionBehavior:behavior];
}

void NativeWidgetNSWindowBridge::SetAspectRatio(
    const gfx::SizeF& aspect_ratio) {
  DCHECK(!aspect_ratio.IsEmpty());
  [window_delegate_
      setAspectRatio:aspect_ratio.width() / aspect_ratio.height()];
}

void NativeWidgetNSWindowBridge::SetCALayerParams(
    const gfx::CALayerParams& ca_layer_params) {
  // Ignore frames arriving "late" for an old size. A frame at the new size
  // should arrive soon.
  // TODO(danakj): We should avoid lossy conversions to integer DIPs.
  gfx::Size frame_dip_size = gfx::ToFlooredSize(gfx::ConvertSizeToDips(
      ca_layer_params.pixel_size, ca_layer_params.scale_factor));
  if (content_dip_size_ != frame_dip_size)
    return;
  compositor_frame_dip_size_ = frame_dip_size;

  // Update the DisplayCALayerTree with the most recent CALayerParams, to make
  // the content display on-screen.
  display_ca_layer_tree_->UpdateCALayerTree(ca_layer_params);

  if (ca_transaction_sync_suppressed_)
    ca_transaction_sync_suppressed_ = false;

  if (invalidate_shadow_on_frame_swap_) {
    invalidate_shadow_on_frame_swap_ = false;
    [window_ invalidateShadow];
  }
}

void NativeWidgetNSWindowBridge::SetIgnoresMouseEvents(
    bool ignores_mouse_events) {
  [window_ setIgnoresMouseEvents:ignores_mouse_events];
}

void NativeWidgetNSWindowBridge::MakeFirstResponder() {
  [window_ makeFirstResponder:bridged_view_];
}

void NativeWidgetNSWindowBridge::SetWindowTitle(const std::u16string& title) {
  // Delay setting the window title until after any menu tracking is complete.
  if (NSRunLoop.currentRunLoop.currentMode == NSEventTrackingRunLoopMode) {
    // Install one run loop trigger to handle all the pending titles.
    if (GetPendingWindowTitleMap().empty()) {
      CFRunLoopPerformBlock(
          [NSRunLoop.currentRunLoop getCFRunLoop], kCFRunLoopDefaultMode, ^{
            for (const auto& pending_title : GetPendingWindowTitleMap()) {
              pending_title.first.title =
                  base::SysUTF16ToNSString(pending_title.second);
            }

            GetPendingWindowTitleMap().clear();
          });
    }

    GetPendingWindowTitleMap()[window_.get()] = title;
  } else {
    window_.get().title = base::SysUTF16ToNSString(title);

    // In case there is an unfired run loop trigger, erase any pending title so
    // that the new title now being set doesn't get smashed.
    GetPendingWindowTitleMap().erase(window_.get());
  }
}

void NativeWidgetNSWindowBridge::ClearTouchBar() {
  [bridged_view_ setTouchBar:nil];
}

void NativeWidgetNSWindowBridge::UpdateTooltip() {
  NSPoint nspoint =
      ui::ConvertPointFromScreenToWindow(window_, [NSEvent mouseLocation]);
  // Note: flip in the view's frame, which matches the window's contentRect.
  gfx::Point point(nspoint.x, NSHeight([bridged_view_ frame]) - nspoint.y);
  [bridged_view_ updateTooltipIfRequiredAt:point];
}

bool NativeWidgetNSWindowBridge::NeedsUpdateWindows() {
  return [bridged_view_ needsUpdateWindows];
}

void NativeWidgetNSWindowBridge::RedispatchKeyEvent(
    const std::vector<uint8_t>& native_event_data) {
  RedispatchKeyEvent(ui::EventFromData(native_event_data));
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetNSWindowBridge, former BridgedNativeWidgetOwner:

void NativeWidgetNSWindowBridge::RemoveChildWindow(
    NativeWidgetNSWindowBridge* child) {
  auto location =
      std::find(child_windows_.begin(), child_windows_.end(), child);
  DCHECK(location != child_windows_.end());
  child_windows_.erase(location);

  // Note the child is sometimes removed already by AppKit. This depends on OS
  // version, and possibly some unpredictable reference counting. Removing it
  // here should be safe regardless.
  [window_ removeChildWindow:child->window_];
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetNSWindowBridge, private:

void NativeWidgetNSWindowBridge::OrderChildren() {
  // Adding a child to a window that isn't visible on the active space will
  // switch to that space (https://crbug.com/783521, https://crbug.com/798792).
  // Bail here (and call OrderChildren() in a few places) to defer adding
  // children until the window is visible.
  NSWindow* window = window_.get();
  if (!window.isVisible || !window.isOnActiveSpace)
    return;
  for (auto* child : child_windows_) {
    if (!child->wants_to_be_visible())
      continue;
    NSWindow* child_window = child->window_.get();
    if (child->IsWindowModalSheet()) {
      if (!child->window_visible_)
        child->ShowAsModalSheet();
      // Sheets don't need a parentWindow set, and setting one causes graphical
      // glitches (http://crbug.com/605098).
    } else {
      if (child_window.parentWindow == window)
        continue;
      [window addChildWindow:child_window ordered:NSWindowAbove];
    }
  }
}

void NativeWidgetNSWindowBridge::RemoveOrDestroyChildren() {
  // TODO(tapted): Implement unowned child windows if required.
  while (!child_windows_.empty()) {
    // The NSWindow can only be destroyed after -[NSWindow close] is complete.
    // Retain the window, otherwise the reference count can reach zero when the
    // child calls back into RemoveChildWindow() via its OnWindowWillClose().
    base::scoped_nsobject<NSWindow> child(
        [child_windows_.back()->ns_window() retain]);
    [child close];
  }
}

void NativeWidgetNSWindowBridge::NotifyVisibilityChangeDown() {
  // Child windows sometimes like to close themselves in response to visibility
  // changes. That's supported, but only with the asynchronous Widget::Close().
  // Perform a heuristic to detect child removal that would break these loops.
  const size_t child_count = child_windows_.size();
  if (!window_visible_) {
    for (NativeWidgetNSWindowBridge* child : child_windows_) {
      if (child->window_visible_)
        [child->ns_window() orderOut:nil];

      DCHECK(!child->window_visible_);
      CHECK_EQ(child_count, child_windows_.size());
    }
    // The orderOut calls above should result in a call to OnVisibilityChanged()
    // in each child. There, children will remove themselves from the NSWindow
    // childWindow list as well as propagate NotifyVisibilityChangeDown() calls
    // to any children of their own. However this is only true for windows
    // managed by the NativeWidgetNSWindowBridge i.e. windows which have
    // ViewsNSWindowDelegate as the delegate.
    DCHECK_EQ(0u, CountBridgedWindows([window_ childWindows]));
    return;
  }

  OrderChildren();
}

void NativeWidgetNSWindowBridge::UpdateWindowGeometry() {
  gfx::Rect window_in_screen = gfx::ScreenRectFromNSRect([window_ frame]);
  gfx::Rect content_in_screen = gfx::ScreenRectFromNSRect(
      [window_ contentRectForFrameRect:[window_ frame]]);
  bool content_resized = content_dip_size_ != content_in_screen.size();
  content_dip_size_ = content_in_screen.size();

  host_->OnWindowGeometryChanged(window_in_screen, content_in_screen);

  if (content_resized && !ca_transaction_sync_suppressed_)
    ui::CATransactionCoordinator::Get().Synchronize();

  // For a translucent window, the shadow calculation needs to be carried out
  // after the frame from the compositor arrives.
  if (content_resized && ![window_ isOpaque])
    invalidate_shadow_on_frame_swap_ = true;
}

void NativeWidgetNSWindowBridge::UpdateWindowDisplay() {
  if (fullscreen_controller_.IsInFullscreenTransition())
    return;

  host_->OnWindowDisplayChanged(GetDisplayForWindow(window_.get()));
}

bool NativeWidgetNSWindowBridge::IsWindowModalSheet() const {
  return parent_ && modal_type_ == ui::MODAL_TYPE_WINDOW;
}

void NativeWidgetNSWindowBridge::ShowAsModalSheet() {
  // -[NSApp beginSheet:] will block the UI thread while the animation runs.
  // So that it doesn't animate a fully transparent window, first wait for a
  // frame. The first step is to pretend that the window is already visible.
  window_visible_ = true;
  host_->OnVisibilityChanged(window_visible_);

  NSWindow* parent_window = parent_->ns_window();
  DCHECK(parent_window);

  // -beginSheet: does not retain |modalDelegate| (and we would not want it to).
  // Since |this| may destroy [window_ delegate], use |window_| itself as the
  // delegate, which will forward to ViewsNSWindowDelegate if |this| is still
  // alive (i.e. it has not set the window delegate to nil).
  // TODO(crbug.com/841631): Migrate to `[NSWindow
  // beginSheet:completionHandler:]` instead of this method.
  auto begin_sheet_closure = base::BindOnce(base::RetainBlock(^{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp beginSheet:window_
        modalForWindow:parent_window
         modalDelegate:window_
        didEndSelector:@selector(sheetDidEnd:returnCode:contextInfo:)
           contextInfo:nullptr];
#pragma clang diagnostic pop
  }));

  if (host_helper_->MustPostTaskToRunModalSheetAnimation()) {
    // This function is called via mojo when using remote cocoa. Inside the
    // nested run loop, we will wait for a message providing the correctly-sized
    // frame for the new sheet. This message will not be processed until we
    // return from handling this message, because it will coming on the same
    // pipe. Avoid the resulting hang by posting a task to show the modal
    // sheet (which will be executed on a fresh stack, which will not block
    // the message).
    // https://crbug.com/1234509
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, std::move(begin_sheet_closure));
  } else {
    std::move(begin_sheet_closure).Run();
  }
}

bool NativeWidgetNSWindowBridge::window_visible() const {
  // In headless mode the platform window is always hidden, so instead of
  // returning the actual platform window visibility state tracked by
  // OnVisibilityChanged() callback, return the expected visibility state
  // maintained by SetVisibilityState() call.
  return headless_mode_window_ ? headless_mode_window_->visibility_state
                               : window_visible_;
}

}  // namespace remote_cocoa