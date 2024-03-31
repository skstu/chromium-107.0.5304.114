// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_COMMON_RESOURCES_TRANSFERABLE_RESOURCE_H_
#define COMPONENTS_VIZ_COMMON_RESOURCES_TRANSFERABLE_RESOURCE_H_

#include <stdint.h>

#include <vector>

#include "build/build_config.h"
#include "components/viz/common/resources/resource_format.h"
#include "components/viz/common/resources/resource_id.h"
#include "components/viz/common/resources/shared_bitmap.h"
#include "components/viz/common/viz_common_export.h"
#include "gpu/command_buffer/common/mailbox_holder.h"
#include "gpu/ipc/common/vulkan_ycbcr_info.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/gfx/buffer_types.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/hdr_metadata.h"

namespace viz {

struct ReturnedResource;

struct VIZ_COMMON_EXPORT TransferableResource {
  enum class SynchronizationType : uint8_t {
    // Commands issued (SyncToken) - a resource can be reused as soon as display
    // compositor issues the latest command on it and SyncToken will be signaled
    // when this happens.
    kSyncToken = 0,
    // Commands completed (aka read lock fence) - If a gpu resource is backed by
    // a GpuMemoryBuffer, then it will be accessed out-of-band, and a gpu fence
    // needs to be waited on before the resource is returned and reused. In
    // other words, the resource will be returned only when gpu commands are
    // completed.
    kGpuCommandsCompleted,
    // Commands submitted (release fence) - a resource will be returned after
    // gpu service submitted commands to the gpu and provide the fence.
    kReleaseFence,
  };

  TransferableResource();
  ~TransferableResource();

  TransferableResource(const TransferableResource& other);
  TransferableResource& operator=(const TransferableResource& other);

  ReturnedResource ToReturnedResource() const;
  static std::vector<ReturnedResource> ReturnResources(
      const std::vector<TransferableResource>& input);

  static TransferableResource MakeSoftware(const SharedBitmapId& id,
                                           const gfx::Size& size,
                                           ResourceFormat format) {
    TransferableResource r;
    r.is_software = true;
    r.mailbox_holder.mailbox = id;
    r.size = size;
    r.format = format;
    return r;
  }

  static TransferableResource MakeGpu(const gpu::Mailbox& mailbox,
                                      uint32_t filter,
                                      uint32_t texture_target,
                                      const gpu::SyncToken& sync_token,
                                      const gfx::Size& size,
                                      ResourceFormat format,
                                      bool is_overlay_candidate) {
    TransferableResource r;
    r.is_software = false;
    r.filter = filter;
    r.mailbox_holder.mailbox = mailbox;
    r.mailbox_holder.texture_target = texture_target;
    r.mailbox_holder.sync_token = sync_token;
    r.size = size;
    r.format = format;
    r.is_overlay_candidate = is_overlay_candidate;
    return r;
  }

  // TODO(danakj): Some of these fields are only GL, some are only Software,
  // some are both but used for different purposes (like the mailbox name).
  // It would be nice to group things together and make it more clear when
  // they will be used or not, and provide easier access to fields such as the
  // mailbox that also show the intent for software for GL.
  // An |id| field that can be unique to this resource. For resources
  // generated by compositor clients, this |id| may be used for their
  // own book-keeping but need not be set at all.
  ResourceId id = kInvalidResourceId;

  // Indicates if the resource is gpu or software backed. If gpu, the
  // mailbox field is a gpu::Mailbox, else it is a SharedBitmapId.
  bool is_software = false;

  // The number of pixels in the gpu mailbox/software bitmap.
  gfx::Size size;

  // The format of the pixels in the gpu mailbox/software bitmap. This should
  // almost always be RGBA_8888 for resources generated by compositor clients,
  // and must be RGBA_8888 always for software resources.
  ResourceFormat format = RGBA_8888;

  // The |mailbox| inside here holds the gpu::Mailbox when this is a gpu
  // resource, or the SharedBitmapId when it is a software resource.
  // The |texture_target| and sync_token| inside here only apply for gpu
  // resources.
  gpu::MailboxHolder mailbox_holder;

  // The color space that is used for pixel path operations (e.g, TexImage,
  // CopyTexImage, DrawPixels) and when displaying as an overlay.
  //
  // TODO(b/220336463): On ChromeOS, the color space for hardware decoded video
  // frames is currently specified at the time of creating the SharedImage.
  // Therefore, for the purposes of that use case and compositing, the
  // |color_space| field here is ignored. We should consider using it.
  //
  // TODO(b/233667677): For ChromeOS NV12 hardware overlays, |color_space| is
  // only used for deciding if an NV12 resource should be promoted to a hardware
  // overlay. Instead, we should plumb this information to DRM/KMS so that if
  // the resource does get promoted to overlay, the display controller knows how
  // to perform the YUV-to-RGB conversion.
  //
  // TODO(b/246974264): Consider using |color_space| to replace |ycbcr_info|
  // since the former is more general and not specific to Vulkan.
  gfx::ColorSpace color_space;
  // The color space in which the resource is sampled, if different from
  // |color_space|. If absl::nullopt, then sampling will occur in the same color
  // space as |color_space|.
  //
  // TODO(crbug.com/1230619): Use this to implement support for WebGL sRGB
  // framebuffers.
  absl::optional<gfx::ColorSpace> color_space_when_sampled;
  absl::optional<gfx::HDRMetadata> hdr_metadata;

  // A gpu resource may be possible to use directly in an overlay if this is
  // true.
  bool is_overlay_candidate = false;
  // For a gpu resource, the filter to use when scaling the resource when
  // drawing it. Typically GL_LINEAR, or GL_NEAREST if no anti-aliasing
  // during scaling is desired.
  uint32_t filter = 0;

  // This defines when the display compositor returns resources. Clients may use
  // different synchronization types based on their needs.
  SynchronizationType synchronization_type = SynchronizationType::kSyncToken;

  // YCbCr info for resources backed by YCbCr Vulkan images.
  absl::optional<gpu::VulkanYCbCrInfo> ycbcr_info;

#if BUILDFLAG(IS_ANDROID)
  // Indicates whether this resource may not be overlayed on Android, since
  // it's not backed by a SurfaceView.  This may be set in combination with
  // |is_overlay_candidate|, to find out if switching the resource to a
  // a SurfaceView would result in overlay promotion.  It's good to find this
  // out in advance, since one has no fallback path for displaying a
  // SurfaceView except via promoting it to an overlay.  Ideally, one _could_
  // promote SurfaceTexture via the overlay path, even if one ended up just
  // drawing a quad in the compositor.  However, for now, we use this flag to
  // refuse to promote so that the compositor will draw the quad.
  bool is_backed_by_surface_texture = false;
#endif

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_WIN)
  // Indicates that this resource would like a promotion hint.
  bool wants_promotion_hint = false;
#endif

  bool operator==(const TransferableResource& o) const {
    return id == o.id && is_software == o.is_software && size == o.size &&
           format == o.format &&
           mailbox_holder.mailbox == o.mailbox_holder.mailbox &&
           mailbox_holder.sync_token == o.mailbox_holder.sync_token &&
           mailbox_holder.texture_target == o.mailbox_holder.texture_target &&
           color_space == o.color_space && hdr_metadata == o.hdr_metadata &&
           is_overlay_candidate == o.is_overlay_candidate &&
           filter == o.filter &&
#if BUILDFLAG(IS_ANDROID)
           is_backed_by_surface_texture == o.is_backed_by_surface_texture &&
           wants_promotion_hint == o.wants_promotion_hint &&
#elif BUILDFLAG(IS_WIN)
           wants_promotion_hint == o.wants_promotion_hint &&
#endif
           synchronization_type == o.synchronization_type;
  }
  bool operator!=(const TransferableResource& o) const { return !(*this == o); }
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_COMMON_RESOURCES_TRANSFERABLE_RESOURCE_H_
