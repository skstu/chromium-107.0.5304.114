// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_WAYLAND_ZAURA_SHELL_H_
#define COMPONENTS_EXO_WAYLAND_ZAURA_SHELL_H_

#include <stdint.h>

#include "chromeos/ui/base/window_state_type.h"
#include "components/exo/surface.h"
#include "components/exo/surface_observer.h"
#include "components/exo/wayland/wayland_display_observer.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/wm/public/activation_change_observer.h"

struct wl_client;
struct wl_resource;

namespace exo {

class ShellSurface;
class ShellSurfaceBase;

namespace wayland {
class SerialTracker;

constexpr uint32_t kZAuraShellVersion = 40;

// Adds bindings to the Aura Shell. Normally this implies Ash on ChromeOS
// builds. On non-ChromeOS builds the protocol provides access to Aura windowing
// system.
void bind_aura_shell(wl_client* client,
                     void* data,
                     uint32_t version,
                     uint32_t id);

class AuraSurface : public SurfaceObserver,
                    public ::wm::ActivationChangeObserver {
 public:
  AuraSurface(Surface* surface, wl_resource* resource);

  AuraSurface(const AuraSurface&) = delete;
  AuraSurface& operator=(const AuraSurface&) = delete;

  ~AuraSurface() override;

  void SetFrame(SurfaceFrameType type);
  void SetServerStartResize();
  void SetFrameColors(SkColor active_frame_color, SkColor inactive_frame_color);
  void SetParent(AuraSurface* parent, const gfx::Point& position);
  void SetStartupId(const char* startup_id);
  void SetApplicationId(const char* application_id);
  void SetClientSurfaceId(const char* client_surface_id);
  void SetOcclusionTracking(bool tracking);
  void Activate();
  void DrawAttention();
  void SetFullscreenMode(uint32_t mode);
  void IntentToSnap(uint32_t snap_direction);
  void SetSnapPrimary();
  void SetSnapSecondary();
  void UnsetSnap();
  void SetWindowSessionId(int32_t window_session_id);
  void SetCanGoBack();
  void UnsetCanGoBack();
  void SetPip();
  void UnsetPip();
  void SetAspectRatio(const gfx::SizeF& aspect_ratio);
  void MoveToDesk(int desk_index);
  void SetInitialWorkspace(const char* initial_workspace);
  void Pin(bool trusted);
  void Unpin();
  void SetOrientationLock(uint32_t orientation_lock);

  // Overridden from SurfaceObserver:
  void OnSurfaceDestroying(Surface* surface) override;
  void OnWindowOcclusionChanged(Surface* surface) override;
  void OnFrameLockingChanged(Surface* surface, bool lock) override;
  void OnDeskChanged(Surface* surface, int state) override;
  void ThrottleFrameRate(bool on) override;

  // Overridden from ActivationChangeObserver:
  void OnWindowActivating(ActivationReason reason,
                          aura::Window* gaining_active,
                          aura::Window* losing_active) override;
  void OnWindowActivated(ActivationReason reason,
                         aura::Window* gained_active,
                         aura::Window* lost_active) override {}

 protected:
  virtual void SendOcclusionFraction(float occlusion_fraction);
  virtual void SendOcclusionState(
      const aura::Window::OcclusionState occlusion_state);

 private:
  Surface* surface_;
  wl_resource* const resource_;

  void ComputeAndSendOcclusion(
      const aura::Window::OcclusionState occlusion_state,
      const SkRegion& occluded_region);
};

// Provides an implementation for top level operations on the shell.
class AuraToplevel {
 public:
  AuraToplevel(ShellSurface* shell_surface,
               SerialTracker* const serial_tracker,
               wl_resource* aura_toplevel_resource,
               wl_resource* xdg_toplevel_resource);

  AuraToplevel(const AuraToplevel&) = delete;
  AuraToplevel& operator=(const AuraToplevel&) = delete;

  virtual ~AuraToplevel();

  void SetOrientationLock(uint32_t lock_type);
  void SetClientSubmitsSurfacesInPixelCoordinates(bool enable);
  void SetClientUsesScreenCoordinates();
  void SetWindowBounds(int32_t x, int32_t y, int32_t width, int32_t height);
  void SetRestoreInfo(int32_t restore_session_id, int32_t restore_window_id);
  void SetRestoreInfoWithWindowIdSource(
      int32_t restore_session_id,
      const std::string& restore_window_id_source);
  void SetSystemModal(bool modal);
  void SetFloat();
  void UnsetFloat();

  void OnConfigure(const gfx::Rect& bounds,
                   chromeos::WindowStateType state_type,
                   bool resizing,
                   bool activated);
  virtual void OnOriginChange(const gfx::Point& origin);
  void SetDecoration(SurfaceFrameType type);
  void SetZOrder(ui::ZOrderLevel z_order);

  ShellSurface* shell_surface_;
  SerialTracker* const serial_tracker_;
  wl_resource* xdg_toplevel_resource_;
  wl_resource* aura_toplevel_resource_;
  bool supports_window_bounds_ = false;

  base::WeakPtrFactory<AuraToplevel> weak_ptr_factory_{this};
};

class AuraPopup {
 public:
  AuraPopup(ShellSurfaceBase* shell_surface);
  AuraPopup(const AuraPopup&) = delete;
  AuraPopup& operator=(const AuraPopup&) = delete;
  ~AuraPopup();

  void SetClientSubmitsSurfacesInPixelCoordinates(bool enable);
  void SetDecoration(SurfaceFrameType type);
  void SetMenu();

 private:
  ShellSurfaceBase* shell_surface_;
};

class AuraOutput : public WaylandDisplayObserver {
 public:
  AuraOutput(wl_resource* resource, WaylandDisplayHandler* display_handler);

  AuraOutput(const AuraOutput&) = delete;
  AuraOutput& operator=(const AuraOutput&) = delete;

  ~AuraOutput() override;

  // Overridden from WaylandDisplayObserver:
  bool SendDisplayMetrics(const display::Display& display,
                          uint32_t changed_metrics) override;
  void OnOutputDestroyed() override;

  bool HasDisplayHandlerForTesting() const;

 protected:
  virtual void SendInsets(const gfx::Insets& insets);
  virtual void SendLogicalTransform(int32_t transform);

 private:
  wl_resource* const resource_;
  WaylandDisplayHandler* display_handler_;
};

}  // namespace wayland
}  // namespace exo

#endif  // COMPONENTS_EXO_WAYLAND_ZAURA_SHELL_H_
