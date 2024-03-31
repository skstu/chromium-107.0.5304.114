// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_DC_LAYER_OVERLAY_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_DC_LAYER_OVERLAY_H_

#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "components/viz/common/quads/aggregated_render_pass.h"
#include "components/viz/service/display/aggregated_frame.h"
#include "components/viz/service/viz_service_export.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/hdr_metadata.h"
#include "ui/gfx/video_types.h"
#include "ui/gl/direct_composition_support.h"

namespace viz {
struct DebugRendererSettings;
class DisplayResourceProvider;

// TODO(weiliangc): Eventually fold this into OverlayProcessorWin and
// OverlayCandidate class.
// Holds all information necessary to construct a
// DCLayer from a DrawQuad.
class VIZ_SERVICE_EXPORT DCLayerOverlay {
 public:
  DCLayerOverlay();
  DCLayerOverlay(const DCLayerOverlay& other);
  DCLayerOverlay& operator=(const DCLayerOverlay& other);
  ~DCLayerOverlay();

  // Resource ids for video Y and UV planes, a single NV12 image, or a swap
  // chain image. See DirectCompositionSurfaceWin for details.
  enum : size_t { kNumResources = 2 };
  ResourceId resources[kNumResources] = {kInvalidResourceId};

  // Mailboxes corresponding to |resources|. This is populated in SkiaRenderer
  // for accessing the textures on the GPU thread.
  gpu::Mailbox mailbox[kNumResources];

  // Stacking order relative to backbuffer which has z-order 0.
  int z_order = 1;

  // What part of the content to display in pixels.
  gfx::Rect content_rect;

  // Bounds of the overlay in pre-transform space.
  gfx::Rect quad_rect;

  // 2D flattened transform that maps |quad_rect| to root target space,
  // after applying the |quad_rect.origin()| as an offset.
  gfx::Transform transform;

  // If |clip_rect| is present, then clip to it in root target space.
  absl::optional<gfx::Rect> clip_rect;

  // This is the color-space the texture should be displayed as. If invalid,
  // then the default for the texture should be used. For YUV textures, that's
  // normally BT.709.
  gfx::ColorSpace color_space;

  gfx::ProtectedVideoType protected_video_type =
      gfx::ProtectedVideoType::kClear;

  gfx::HDRMetadata hdr_metadata;

  bool is_video_fullscreen_letterboxing;
};

typedef std::vector<DCLayerOverlay> DCLayerOverlayList;

class VIZ_SERVICE_EXPORT DCLayerOverlayProcessor final
    : public gl::DirectCompositionOverlayCapsObserver {
 public:
  using FilterOperationsMap =
      base::flat_map<AggregatedRenderPassId, cc::FilterOperations*>;
  // When |skip_initialization_for_testing| is true, object will be isolated
  // for unit tests.
  explicit DCLayerOverlayProcessor(
      const DebugRendererSettings* debug_settings,
      int allowed_yuv_overlay_count,
      bool skip_initialization_for_testing = false);

  DCLayerOverlayProcessor(const DCLayerOverlayProcessor&) = delete;
  DCLayerOverlayProcessor& operator=(const DCLayerOverlayProcessor&) = delete;

  ~DCLayerOverlayProcessor() override;

  // Virtual for testing.
  virtual void Process(DisplayResourceProvider* resource_provider,
                       const gfx::RectF& display_rect,
                       const FilterOperationsMap& render_pass_filters,
                       const FilterOperationsMap& render_pass_backdrop_filters,
                       AggregatedRenderPass* render_pass,
                       gfx::Rect* damage_rect,
                       SurfaceDamageRectList surface_damage_rect_list,
                       DCLayerOverlayList* dc_layer_overlays,
                       bool is_video_capture_enabled,
                       bool is_page_fullscreen_mode);
  void ClearOverlayState();
  // This is the damage contribution due to previous frame's overlays which can
  // be empty.
  gfx::Rect PreviousFrameOverlayDamageContribution();

  // DirectCompositionOverlayCapsObserver implementation.
  void OnOverlayCapsChanged() override;
  void UpdateHasHwOverlaySupport();

  void set_frames_since_last_qualified_multi_overlays_for_testing(int value) {
    frames_since_last_qualified_multi_overlays_ = value;
  }

 private:
  // Detects overlay processing skip inside |render_pass|.
  bool ShouldSkipOverlay(AggregatedRenderPass* render_pass,
                         bool is_video_capture_enabled);

  // UpdateDCLayerOverlays() adds the quad at |it| to the overlay list
  // |dc_layer_overlays|.
  void UpdateDCLayerOverlays(const gfx::RectF& display_rect,
                             AggregatedRenderPass* render_pass,
                             const QuadList::Iterator& it,
                             const gfx::Rect& quad_rectangle_in_target_space,
                             const gfx::Rect& occluding_damage_rect,
                             bool is_overlay,
                             QuadList::Iterator* new_it,
                             size_t* new_index,
                             gfx::Rect* damage_rect,
                             DCLayerOverlayList* dc_layer_overlays,
                             bool is_page_fullscreen_mode);

  // Returns an iterator to the element after |it|.
  QuadList::Iterator ProcessForOverlay(const gfx::RectF& display_rect,
                                       AggregatedRenderPass* render_pass,
                                       const gfx::Rect& quad_rectangle,
                                       const QuadList::Iterator& it,
                                       gfx::Rect* damage_rect);
  void ProcessForUnderlay(const gfx::RectF& display_rect,
                          AggregatedRenderPass* render_pass,
                          const gfx::Rect& quad_rectangle,
                          const gfx::Rect& occluding_damage_rect,
                          const QuadList::Iterator& it,
                          size_t processed_overlay_count,
                          gfx::Rect* damage_rect,
                          DCLayerOverlay* dc_layer);

  void UpdateRootDamageRect(const gfx::RectF& display_rect,
                            gfx::Rect* damage_rect);

  void RemoveOverlayDamageRect(const QuadList::Iterator& it,
                               const gfx::Rect& quad_rectangle,
                               const gfx::Rect& occluding_damage_rect,
                               gfx::Rect* damage_rect);

  void InsertDebugBorderDrawQuad(const DCLayerOverlayList* dc_layer_overlays,
                                 AggregatedRenderPass* render_pass,
                                 const gfx::RectF& display_rect,
                                 gfx::Rect* damage_rect);
  bool IsPreviousFrameUnderlayRect(const gfx::Rect& quad_rectangle,
                                   size_t index);

  // Remove all video overlay candidates from `candidate_index_list` if any of
  // them have moved in the last several frames.
  //
  // We do this because it could cause visible stuttering of playback on certain
  // older hardware. The stuttering does not occur if other overlay quads move
  // while a non-moving video is playing.
  //
  // This only tracks clear video quads because hardware-protected videos cannot
  // be accessed by the viz compositor, so they must be promoted to overlay,
  // even if they could cause stutter. Software-protected video aren't required
  // to be in overlay, but we also exclude them from de-promotion to keep the
  // protection benefits of being in an overlay.
  //
  // `transform_to_root_target` is needed to track the quad positions in a
  // uniform space. It should be in screen space (to handle the case when the
  // window itself moves), but we don't easily know of the position of the
  // window in screen space.
  //
  // `candidate_index_list` contains the indexes in `quad_list` of overlay
  // candidates.
  void RemoveClearVideoQuadCandidatesIfMoving(
      const QuadList* quad_list,
      std::vector<size_t>* candidate_index_list);

  bool has_overlay_support_;
  const int allowed_yuv_overlay_count_;
  int processed_yuv_overlay_count_ = 0;
  uint64_t frames_since_last_qualified_multi_overlays_ = 0;

  // Reference to the global viz singleton.
  const raw_ptr<const DebugRendererSettings> debug_settings_;

  bool previous_frame_underlay_is_opaque_ = true;
  bool allow_promotion_hinting_ = false;
  gfx::RectF previous_display_rect_;
  std::vector<size_t> damages_to_be_removed_;

  struct OverlayRect {
    gfx::Rect rect;
    bool is_overlay;  // If false, it's an underlay.
    bool operator==(const OverlayRect& b) const {
      return rect == b.rect && is_overlay == b.is_overlay;
    }
    bool operator!=(const OverlayRect& b) const { return !(*this == b); }
  };
  std::vector<OverlayRect> previous_frame_overlay_rects_;
  std::vector<OverlayRect> current_frame_overlay_rects_;
  SurfaceDamageRectList surface_damage_rect_list_;

  // Used in `RemoveClearVideoQuadCandidatesIfMoving`:
  // List of clear video content candidate bounds.
  std::vector<gfx::Rect> previous_frame_overlay_candidate_rects_{};
  int frames_since_last_overlay_candidate_rects_change_ = 0;

  THREAD_CHECKER(thread_checker_);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_DC_LAYER_OVERLAY_H_
