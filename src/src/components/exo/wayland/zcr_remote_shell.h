// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_WAYLAND_ZCR_REMOTE_SHELL_H_
#define COMPONENTS_EXO_WAYLAND_ZCR_REMOTE_SHELL_H_

#include <stdint.h>

#include "base/callback.h"

struct wl_client;
struct wl_resource;

namespace gfx {
class Rect;
class Insets;
class Size;
}  // namespace gfx

namespace display {
class Display;
}  // namespace display

namespace exo {

class Display;

namespace wayland {

struct WaylandRemoteShellData {
  using OutputResourceProvider =
      base::RepeatingCallback<wl_resource*(wl_client*, int64_t)>;

  explicit WaylandRemoteShellData(Display* display,
                                  OutputResourceProvider output_provider);
  ~WaylandRemoteShellData();

  // Owned by WaylandServerController, which always outlives this.
  Display* const display;

  OutputResourceProvider const output_provider;

  WaylandRemoteShellData(const WaylandRemoteShellData&) = delete;
  WaylandRemoteShellData& operator=(const WaylandRemoteShellData&) = delete;
};

void bind_remote_shell(wl_client* client,
                       void* data,
                       uint32_t version,
                       uint32_t id);

// Create the insets in pixel coordinates in such way that
// work area will be within the chrome's work area.
gfx::Insets GetWorkAreaInsetsInPixel(const display::Display& display,
                                     float default_dsf,
                                     const gfx::Size& size_in_client_pixel,
                                     const gfx::Rect& work_area_in_dp);

// Returns a work area where the shelf is considered visible.
gfx::Rect GetStableWorkArea(const display::Display& display);

}  // namespace wayland
}  // namespace exo

#endif  // COMPONENTS_EXO_WAYLAND_ZCR_REMOTE_SHELL_H_
