// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/wayland/zcr_color_manager.h"

#include <chrome-color-management-server-protocol.h>
#include <wayland-server-core.h>
#include <cstdint>
#include <memory>

#include "ash/shell.h"
#include "base/containers/fixed_flat_map.h"
#include "base/notreached.h"
#include "base/strings/stringprintf.h"
#include "components/exo/surface.h"
#include "components/exo/surface_observer.h"
#include "components/exo/wayland/server.h"
#include "components/exo/wayland/server_util.h"
#include "components/exo/wayland/wayland_display_observer.h"
#include "components/exo/wayland/wayland_display_output.h"
#include "components/exo/wm_helper_chromeos.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/modules/skcms/skcms.h"
#include "ui/base/wayland/color_manager_util.h"
#include "ui/display/display.h"
#include "ui/display/display_observer.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/screen.h"
#include "ui/display/types/display_constants.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/display_color_spaces.h"
#include "ui/gfx/geometry/triangle_f.h"
#include "wayland-server-protocol.h"

namespace exo {
namespace wayland {

namespace {

#define PARAM_TO_FLOAT(x) (x / 10000.f)
#define FLOAT_TO_PARAM(x) (x * 10000)

constexpr auto kDefaultColorSpace = gfx::ColorSpace::CreateSRGB();

// Wrapper around a |gfx::ColorSpace| that tracks additional data useful to
// the protocol. These live as wayland resource data.
class ColorManagerColorSpace {
 public:
  explicit ColorManagerColorSpace(gfx::ColorSpace color_space)
      : color_space(color_space),
        eotf(ui::wayland::ToColorManagerEOTF(color_space.GetTransferID())),
        primaries(color_space.GetPrimaries()) {}

  ColorManagerColorSpace(gfx::ColorSpace color_space,
                         zcr_color_manager_v1_eotf_names eotf,
                         SkColorSpacePrimaries primaries)
      : color_space(color_space), eotf(eotf), primaries(primaries) {}

  virtual ~ColorManagerColorSpace() = default;

  const gfx::ColorSpace color_space;
  const zcr_color_manager_v1_eotf_names eotf;
  const SkColorSpacePrimaries primaries;

  void SendColorSpaceInfo(wl_resource* color_space_resource) {
    SendCustomColorSpaceInfo(color_space_resource);
    zcr_color_space_v1_send_params(
        color_space_resource, eotf,
        static_cast<int>(FLOAT_TO_PARAM(primaries.fRX)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fRY)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fGX)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fGY)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fBX)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fBY)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fWX)),
        static_cast<int>(FLOAT_TO_PARAM(primaries.fWY)));
    zcr_color_space_v1_send_done(color_space_resource);
  }

  virtual void SendCustomColorSpaceInfo(wl_resource* color_space_resource) {}
};

class NameBasedColorSpace final : public ColorManagerColorSpace {
 public:
  explicit NameBasedColorSpace(
      gfx::ColorSpace color_space,
      zcr_color_manager_v1_chromaticity_names chromaticity,
      zcr_color_manager_v1_eotf_names eotf,
      zcr_color_manager_v1_whitepoint_names whitepoint)
      : ColorManagerColorSpace(color_space, eotf, color_space.GetPrimaries()),
        chromaticity(ui::wayland::ToColorManagerChromaticity(
            color_space.GetPrimaryID())),
        whitepoint(whitepoint) {}

  const zcr_color_manager_v1_chromaticity_names chromaticity;
  const zcr_color_manager_v1_whitepoint_names whitepoint;

  void SendCustomColorSpaceInfo(wl_resource* color_space_resource) override {
    zcr_color_space_v1_send_names(color_space_resource, eotf, chromaticity,
                                  whitepoint);
  }
};

// Wrap a surface pointer and handle relevant events.
// TODO(b/207031122): This class should also watch for display color space
// changes and update clients.
class ColorManagerSurface final : public SurfaceObserver {
 public:
  explicit ColorManagerSurface(Server* server,
                               wl_resource* color_manager_surface_resource,
                               Surface* surface)
      : server_(server),
        color_manager_surface_resource_(color_manager_surface_resource),
        scoped_surface_(std::make_unique<ScopedSurface>(surface, this)) {}
  ColorManagerSurface(ColorManagerSurface&) = delete;
  ColorManagerSurface(ColorManagerSurface&&) = delete;
  ~ColorManagerSurface() override = default;

  // Safely set the color space (doing nothing if the surface was destroyed).
  void SetColorSpace(gfx::ColorSpace color_space) {
    Surface* surface = scoped_surface_->get();
    if (!surface)
      return;

    surface->SetColorSpace(color_space);
  }

 private:
  // SurfaceObserver:
  void OnDisplayChanged(Surface* surface,
                        int64_t old_display,
                        int64_t new_display) override {
    wl_client* client = wl_resource_get_client(color_manager_surface_resource_);
    wl_resource* display_resource =
        server_->GetOutputResource(client, new_display);

    if (!display_resource)
      return;

    const auto* wm_helper = WMHelperChromeOS::GetInstance();

    if (old_display != display::kInvalidDisplayId) {
      const auto& old_display_info = wm_helper->GetDisplayInfo(old_display);
      const auto& new_display_info = wm_helper->GetDisplayInfo(new_display);

      if (old_display_info.display_color_spaces() ==
          new_display_info.display_color_spaces())
        return;
    }

    zcr_color_management_surface_v1_send_preferred_color_space(
        color_manager_surface_resource_, display_resource);
  }

  void OnSurfaceDestroying(Surface* surface) override {
    scoped_surface_.reset();
  }

  Server* server_;
  wl_resource* color_manager_surface_resource_;
  std::unique_ptr<ScopedSurface> scoped_surface_;
};

class ColorManagerObserver : public WaylandDisplayObserver {
 public:
  ColorManagerObserver(WaylandDisplayHandler* wayland_display_handler,
                       wl_resource* color_management_output_resource,
                       wl_resource* output_resource)
      : wayland_display_handler_(wayland_display_handler),
        color_management_output_resource_(color_management_output_resource),
        output_resource_(output_resource) {}

  ColorManagerObserver(const ColorManagerObserver&) = delete;
  ColorManagerObserver& operator=(const ColorManagerObserver&) = delete;

  ~ColorManagerObserver() = default;

  // Overridden from WaylandDisplayObserver:
  bool SendDisplayMetrics(const display::Display& display,
                          uint32_t changed_metrics) override {
    if (!(changed_metrics &
          display::DisplayObserver::DISPLAY_METRIC_COLOR_SPACE)) {
      return false;
    }

    zcr_color_management_output_v1_send_color_space_changed(
        color_management_output_resource_);
    return true;
  }

  gfx::ColorSpace GetColorSpace() const {
#if BUILDFLAG(IS_CHROMEOS_ASH)
    // Snapshot ColorSpace is only valid for ScreenAsh.
    return ash::Shell::Get()
        ->display_manager()
        ->GetDisplayInfo(wayland_display_handler_->id())
        .GetSnapshotColorSpace();
#else
    return gfx::ColorSpace::CreateSRGB();
#endif
  }

  WaylandDisplayHandler* wayland_display_handler() {
    return wayland_display_handler_;
  }

  wl_resource* GetOutputResource() { return output_resource_; }

 private:
  WaylandDisplayHandler* wayland_display_handler_;
  wl_resource* const color_management_output_resource_;
  wl_resource* output_resource_;
};

////////////////////////////////////////////////////////////////////////////////
// zcr_color_management_color_space_v1_interface:

void color_space_get_information(struct wl_client* client,
                                 struct wl_resource* color_space_resource) {
  GetUserDataAs<ColorManagerColorSpace>(color_space_resource)
      ->SendColorSpaceInfo(color_space_resource);
}

void color_space_destroy(struct wl_client* client,
                         struct wl_resource* color_space_resource) {
  wl_resource_destroy(color_space_resource);
}

const struct zcr_color_space_v1_interface color_space_v1_implementation = {
    color_space_get_information, color_space_destroy};

////////////////////////////////////////////////////////////////////////////////
// zcr_color_management_output_v1_interface:

void color_management_output_get_color_space(
    struct wl_client* client,
    struct wl_resource* color_management_output_resource,
    uint32_t id) {
  auto* color_management_output_observer =
      GetUserDataAs<ColorManagerObserver>(color_management_output_resource);

  // create new zcr color space for the current color space of the output
  auto color_space = std::make_unique<ColorManagerColorSpace>(
      color_management_output_observer->GetColorSpace());

  wl_resource* color_space_resource =
      wl_resource_create(client, &zcr_color_space_v1_interface, 1, id);

  SetImplementation(color_space_resource, &color_space_v1_implementation,
                    std::move(color_space));
}

void color_management_output_destroy(
    struct wl_client* client,
    struct wl_resource* color_management_output_resource) {
  wl_resource_destroy(color_management_output_resource);
}

const struct zcr_color_management_output_v1_interface
    color_management_output_v1_implementation = {
        color_management_output_get_color_space,
        color_management_output_destroy};

////////////////////////////////////////////////////////////////////////////////
// zcr_color_management_surface_v1_interface:

void color_management_surface_set_alpha_mode(
    struct wl_client* client,
    struct wl_resource* color_management_surface_resource,
    uint32_t alpha_mode) {
  NOTIMPLEMENTED();
}

void color_management_surface_set_extended_dynamic_range(
    struct wl_client* client,
    struct wl_resource* color_management_surface_resource,
    uint32_t value) {
  NOTIMPLEMENTED();
}

void color_management_surface_set_color_space(
    struct wl_client* client,
    struct wl_resource* color_management_surface_resource,
    struct wl_resource* color_space_resource,
    uint32_t render_intent) {
  auto* color_manager_color_space =
      GetUserDataAs<ColorManagerColorSpace>(color_space_resource);
  GetUserDataAs<ColorManagerSurface>(color_management_surface_resource)
      ->SetColorSpace(color_manager_color_space->color_space);
}

void color_management_surface_set_default_color_space(
    struct wl_client* client,
    struct wl_resource* color_management_surface_resource) {
  GetUserDataAs<ColorManagerSurface>(color_management_surface_resource)
      ->SetColorSpace(kDefaultColorSpace);
}

void color_management_surface_destroy(
    struct wl_client* client,
    struct wl_resource* color_management_surface_resource) {
  GetUserDataAs<ColorManagerSurface>(color_management_surface_resource)
      ->SetColorSpace(kDefaultColorSpace);

  wl_resource_destroy(color_management_surface_resource);
}

const struct zcr_color_management_surface_v1_interface
    color_management_surface_v1_implementation = {
        color_management_surface_set_alpha_mode,
        color_management_surface_set_extended_dynamic_range,
        color_management_surface_set_color_space,
        color_management_surface_set_default_color_space,
        color_management_surface_destroy};

////////////////////////////////////////////////////////////////////////////////
// zcr_color_manager_v1_interface:

void CreateColorSpace(struct wl_client* client,
                      int32_t color_space_creator_id,
                      std::unique_ptr<ColorManagerColorSpace> color_space) {
  wl_resource* color_space_resource = wl_resource_create(
      client, &zcr_color_space_v1_interface, /*version=*/1, /*id=*/0);
  SetImplementation(color_space_resource, &color_space_v1_implementation,
                    std::move(color_space));

  wl_resource* color_space_creator_resource =
      wl_resource_create(client, &zcr_color_space_creator_v1_interface,
                         /*version=*/1, color_space_creator_id);
  zcr_color_space_creator_v1_send_created(color_space_creator_resource,
                                          color_space_resource);
  // The resource should be immediately destroyed once it's sent its event.
  wl_resource_destroy(color_space_creator_resource);
}

void SendColorCreationError(struct wl_client* client,
                            int32_t color_space_creator_id,
                            int32_t error_flags) {
  wl_resource* color_space_creator_resource = wl_resource_create(
      client, &zcr_color_space_creator_v1_interface, 1, color_space_creator_id);

  zcr_color_space_creator_v1_send_error(color_space_creator_resource,
                                        error_flags);

  // The resource should be immediately destroyed once it's sent its event.
  wl_resource_destroy(color_space_creator_resource);
}

void color_manager_create_color_space_from_icc(
    struct wl_client* client,
    struct wl_resource* color_manager_resource,
    uint32_t id,
    int32_t icc) {
  NOTIMPLEMENTED();
}

// TODO(b/206971557): This doesn't handle the user-set white point yet.
void color_manager_create_color_space_from_names(
    struct wl_client* client,
    struct wl_resource* color_manager_resource,
    uint32_t id,
    uint32_t eotf,
    uint32_t chromaticity,
    uint32_t whitepoint) {
  uint32_t error_flags = 0;

  auto chromaticity_id = gfx::ColorSpace::PrimaryID::INVALID;
  const auto* maybe_primary = ui::wayland::kChromaticityMap.find(chromaticity);
  if (maybe_primary != std::end(ui::wayland::kChromaticityMap)) {
    chromaticity_id = maybe_primary->second;
  } else {
    DLOG(ERROR) << "Unable to find named chromaticity for id=" << chromaticity;
    error_flags |= ZCR_COLOR_SPACE_CREATOR_V1_CREATION_ERROR_BAD_PRIMARIES;
  }

  auto eotf_id = gfx::ColorSpace::TransferID::INVALID;
  const auto* maybe_eotf = ui::wayland::kEotfMap.find(eotf);
  if (maybe_eotf != std::end(ui::wayland::kEotfMap)) {
    eotf_id = maybe_eotf->second;
  } else {
    DLOG(ERROR) << "Unable to find named eotf for id=" << eotf;
    wl_resource_post_error(color_manager_resource,
                           ZCR_COLOR_MANAGER_V1_ERROR_BAD_ENUM,
                           "Unable to find an EOTF matching %d", eotf);
  }

  if (error_flags)
    SendColorCreationError(client, id, error_flags);

  CreateColorSpace(
      client, id,
      std::make_unique<NameBasedColorSpace>(
          gfx::ColorSpace(chromaticity_id, eotf_id),
          static_cast<zcr_color_manager_v1_chromaticity_names>(chromaticity),
          static_cast<zcr_color_manager_v1_eotf_names>(eotf),
          static_cast<zcr_color_manager_v1_whitepoint_names>(whitepoint)));
}

void color_manager_create_color_space_from_params(
    struct wl_client* client,
    struct wl_resource* color_manager_resource,
    uint32_t id,
    uint32_t eotf,
    uint32_t primary_r_x,
    uint32_t primary_r_y,
    uint32_t primary_g_x,
    uint32_t primary_g_y,
    uint32_t primary_b_x,
    uint32_t primary_b_y,
    uint32_t white_point_x,
    uint32_t white_point_y) {
  SkColorSpacePrimaries primaries = {
      PARAM_TO_FLOAT(primary_r_x),   PARAM_TO_FLOAT(primary_r_y),
      PARAM_TO_FLOAT(primary_g_x),   PARAM_TO_FLOAT(primary_g_y),
      PARAM_TO_FLOAT(primary_b_x),   PARAM_TO_FLOAT(primary_b_y),
      PARAM_TO_FLOAT(white_point_x), PARAM_TO_FLOAT(white_point_y)};

  gfx::PointF r(primaries.fRX, primaries.fRY);
  gfx::PointF g(primaries.fGX, primaries.fGY);
  gfx::PointF b(primaries.fBX, primaries.fBY);
  gfx::PointF w(primaries.fWX, primaries.fWY);
  if (!gfx::PointIsInTriangle(w, r, g, b)) {
    auto error_message = base::StringPrintf(
        "White point %s must be inside of the triangle r=%s g=%s b=%s",
        w.ToString().c_str(), r.ToString().c_str(), g.ToString().c_str(),
        b.ToString().c_str());
    DLOG(ERROR) << error_message;
    wl_resource_post_error(color_manager_resource,
                           ZCR_COLOR_MANAGER_V1_ERROR_BAD_PARAM, "%s",
                           error_message.c_str());
    return;
  }

  auto eotf_id = gfx::ColorSpace::TransferID::INVALID;
  const auto* maybe_eotf = ui::wayland::kEotfMap.find(eotf);
  if (maybe_eotf != std::end(ui::wayland::kEotfMap)) {
    eotf_id = maybe_eotf->second;
  } else {
    DLOG(ERROR) << "Unable to find named transfer function for id=" << eotf;
    wl_resource_post_error(color_manager_resource,
                           ZCR_COLOR_MANAGER_V1_ERROR_BAD_ENUM,
                           "Unable to find an EOTF matching %d", eotf);
    return;
  }

  skcms_Matrix3x3 xyzd50 = {};
  if (!primaries.toXYZD50(&xyzd50)) {
    DLOG(ERROR) << base::StringPrintf(
        "Unable to translate color space primaries to XYZD50: "
        "{%f, %f, %f, %f, %f, %f, %f, %f}",
        primaries.fRX, primaries.fRY, primaries.fGX, primaries.fGY,
        primaries.fBX, primaries.fBY, primaries.fWX, primaries.fWY);

    SendColorCreationError(
        client, id, ZCR_COLOR_SPACE_CREATOR_V1_CREATION_ERROR_BAD_PRIMARIES);
    return;
  }

  CreateColorSpace(client, id,
                   std::make_unique<ColorManagerColorSpace>(
                       gfx::ColorSpace::CreateCustom(xyzd50, eotf_id)));
}

void color_manager_get_color_management_output(
    struct wl_client* client,
    struct wl_resource* color_manager_resource,
    uint32_t id,
    struct wl_resource* output) {
  wl_resource* color_management_output_resource = wl_resource_create(
      client, &zcr_color_management_output_v1_interface, 1, id);
  auto* display_handler = GetUserDataAs<WaylandDisplayHandler>(output);
  auto color_management_output_observer =
      std::make_unique<ColorManagerObserver>(
          display_handler, color_management_output_resource, output);

  display_handler->AddObserver(color_management_output_observer.get());
  SetImplementation(color_management_output_resource,
                    &color_management_output_v1_implementation,
                    std::move(color_management_output_observer));
}

void color_manager_get_color_management_surface(
    struct wl_client* client,
    struct wl_resource* color_manager_resource,
    uint32_t id,
    struct wl_resource* surface_resource) {
  wl_resource* color_management_surface_resource = wl_resource_create(
      client, &zcr_color_management_surface_v1_interface, 1, id);

  SetImplementation(color_management_surface_resource,
                    &color_management_surface_v1_implementation,
                    std::make_unique<ColorManagerSurface>(
                        GetUserDataAs<Server>(color_manager_resource),
                        color_management_surface_resource,
                        GetUserDataAs<Surface>(surface_resource)));
}

void color_manager_destroy(struct wl_client* client,
                           struct wl_resource* color_manager_resource) {
  wl_resource_destroy(color_manager_resource);
}

const struct zcr_color_manager_v1_interface color_manager_v1_implementation = {
    color_manager_create_color_space_from_icc,
    color_manager_create_color_space_from_names,
    color_manager_create_color_space_from_params,
    color_manager_get_color_management_output,
    color_manager_get_color_management_surface,
    color_manager_destroy};

}  // namespace

void bind_zcr_color_manager(wl_client* client,
                            void* data,
                            uint32_t version,
                            uint32_t id) {
  wl_resource* color_manager_resource =
      wl_resource_create(client, &zcr_color_manager_v1_interface, version, id);

  wl_resource_set_implementation(color_manager_resource,
                                 &color_manager_v1_implementation, data,
                                 /*destroy=*/nullptr);
}

}  // namespace wayland
}  // namespace exo
