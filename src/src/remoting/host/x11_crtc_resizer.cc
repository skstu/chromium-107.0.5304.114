// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/x11_crtc_resizer.h"

#include <iterator>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/contains.h"
#include "base/ranges/algorithm.h"
#include "base/stl_util.h"
#include "ui/gfx/x/future.h"

namespace {

constexpr auto kInvalidMode = static_cast<x11::RandR::Mode>(0);
constexpr auto kDisabledCrtc = static_cast<x11::RandR::Crtc>(0);

}  // namespace

namespace remoting {

X11CrtcResizer::CrtcInfo::CrtcInfo() = default;
X11CrtcResizer::CrtcInfo::CrtcInfo(
    x11::RandR::Crtc crtc,
    int16_t x,
    int16_t y,
    uint16_t width,
    uint16_t height,
    x11::RandR::Mode mode,
    x11::RandR::Rotation rotation,
    const std::vector<x11::RandR::Output>& outputs)
    : crtc(crtc),
      old_x(x),
      x(x),
      old_y(y),
      y(y),
      width(width),
      height(height),
      mode(mode),
      rotation(rotation),
      outputs(outputs) {}
X11CrtcResizer::CrtcInfo::CrtcInfo(const X11CrtcResizer::CrtcInfo&) = default;
X11CrtcResizer::CrtcInfo::CrtcInfo(X11CrtcResizer::CrtcInfo&&) = default;
X11CrtcResizer::CrtcInfo& X11CrtcResizer::CrtcInfo::operator=(
    const X11CrtcResizer::CrtcInfo&) = default;
X11CrtcResizer::CrtcInfo& X11CrtcResizer::CrtcInfo::operator=(
    X11CrtcResizer::CrtcInfo&&) = default;
X11CrtcResizer::CrtcInfo::~CrtcInfo() = default;

bool X11CrtcResizer::CrtcInfo::OffsetsChanged() const {
  return old_x != x || old_y != y;
}

X11CrtcResizer::X11CrtcResizer(
    x11::RandR::GetScreenResourcesCurrentReply* resources,
    x11::Connection* connection)
    : resources_(resources), connection_(connection) {
  // Unittests provide nullptr, and do not exercise code-paths that talk to the
  // X server.
  if (connection_) {
    randr_ = &connection_->randr();
  }
}

X11CrtcResizer::~X11CrtcResizer() = default;

void X11CrtcResizer::FetchActiveCrtcs() {
  active_crtcs_.clear();
  x11::Time config_timestamp = resources_->config_timestamp;
  for (const auto& crtc : resources_->crtcs) {
    auto response = randr_->GetCrtcInfo({crtc, config_timestamp}).Sync();
    if (!response)
      continue;
    if (response->outputs.empty())
      continue;

    AddCrtcFromReply(crtc, *response.reply);
  }
}

x11::RandR::Crtc X11CrtcResizer::GetCrtcForOutput(
    x11::RandR::Output output) const {
  // This implementation assumes an output is attached to only one CRTC. If
  // there are multiple CRTCs for the output, only the first will be returned,
  // but this should never occur with Xorg+video-dummy.
  auto iter =
      base::ranges::find_if(active_crtcs_, [output](const CrtcInfo& crtc_info) {
        return base::Contains(crtc_info.outputs, output);
      });
  if (iter == active_crtcs_.end()) {
    return kDisabledCrtc;
  }
  return iter->crtc;
}

void X11CrtcResizer::DisableCrtc(x11::RandR::Crtc crtc) {
  x11::Time config_timestamp = resources_->config_timestamp;
  randr_->SetCrtcConfig({
      .crtc = crtc,
      .timestamp = x11::Time::CurrentTime,
      .config_timestamp = config_timestamp,
      .x = 0,
      .y = 0,
      .mode = kInvalidMode,
      .rotation = x11::RandR::Rotation::Rotate_0,
      .outputs = {},
  });
}

void X11CrtcResizer::UpdateActiveCrtcs(x11::RandR::Crtc crtc,
                                       x11::RandR::Mode mode,
                                       const webrtc::DesktopSize& new_size) {
  updated_crtcs_.insert(crtc);

  // Find |crtc| in |active_crtcs_| and adjust its mode and size.
  auto iter = base::ranges::find(active_crtcs_, crtc, &CrtcInfo::crtc);

  // |crtc| was returned by GetCrtcForOutput() so it should definitely be in
  // the list.
  DCHECK(iter != active_crtcs_.end());

  iter->mode = mode;
  RelayoutCrtcs(iter->crtc, new_size);
  NormalizeCrtcs();
}

void X11CrtcResizer::UpdateActiveCrtc(x11::RandR::Crtc crtc,
                                      x11::RandR::Mode mode,
                                      const webrtc::DesktopRect& new_rect) {
  updated_crtcs_.insert(crtc);

  // Find |crtc| in |active_crtcs_| and adjust its mode and size.
  auto iter = base::ranges::find(active_crtcs_, crtc, &CrtcInfo::crtc);

  // |crtc| was returned by GetCrtcForOutput() so it should definitely be in
  // the list.
  DCHECK(iter != active_crtcs_.end());

  iter->mode = mode;
  iter->x = new_rect.left();
  iter->y = new_rect.top();
  iter->width = new_rect.width();
  iter->height = new_rect.height();
}

void X11CrtcResizer::AddActiveCrtc(
    x11::RandR::Crtc crtc,
    x11::RandR::Mode mode,
    const std::vector<x11::RandR::Output>& outputs,
    const webrtc::DesktopRect& new_rect) {
  // |crtc| is not active so it must not be in |active_crtcs_|.
  DCHECK(base::ranges::find(active_crtcs_, crtc, &CrtcInfo::crtc) ==
         active_crtcs_.end());

  active_crtcs_.emplace_back(crtc, new_rect.left(), new_rect.top(),
                             new_rect.width(), new_rect.height(), mode,
                             x11::RandR::Rotation::Rotate_0, outputs);
  updated_crtcs_.insert(crtc);
}

void X11CrtcResizer::RemoveActiveCrtc(x11::RandR::Crtc crtc) {
  auto iter = base::ranges::remove(active_crtcs_, crtc, &CrtcInfo::crtc);
  DCHECK(iter != active_crtcs_.end());
  active_crtcs_.erase(iter, active_crtcs_.end());
}

void X11CrtcResizer::RelayoutCrtcs(x11::RandR::Crtc crtc_to_resize,
                                   const webrtc::DesktopSize& new_size) {
  if (LayoutIsVertical()) {
    PackVertically(new_size, crtc_to_resize);
  } else {
    PackHorizontally(new_size, crtc_to_resize);
  }
}

void X11CrtcResizer::DisableChangedCrtcs() {
  for (const auto& crtc_info : active_crtcs_) {
    // Updated CRTCs are expected to be disabled by the caller.
    if (crtc_info.OffsetsChanged() &&
        !updated_crtcs_.contains(crtc_info.crtc)) {
      DisableCrtc(crtc_info.crtc);
    }
  }
}

void X11CrtcResizer::NormalizeCrtcs() {
  webrtc::DesktopRect bounding_box;
  for (const auto& crtc : active_crtcs_) {
    bounding_box.UnionWith(
        webrtc::DesktopRect::MakeXYWH(crtc.x, crtc.y, crtc.width, crtc.height));
  }
  bounding_box_size_ = bounding_box.size();
  webrtc::DesktopVector adjustment = -bounding_box.top_left();
  if (adjustment.is_zero()) {
    return;
  }
  for (auto& crtc : active_crtcs_) {
    crtc.x += adjustment.x();
    crtc.y += adjustment.y();
  }
}

void X11CrtcResizer::MoveApplicationWindows() {
  if (!connection_) {
    // connection_ is nullptr in unittests.
    return;
  }

  // Only direct descendants of the root window should be moved. Child
  // windows automatically track the location of their parents, and can only
  // be moved within their parent window.
  auto query_response =
      connection_->QueryTree({connection_->default_root()}).Sync();
  if (!query_response) {
    return;
  }
  for (const auto& window : query_response->children) {
    auto attributes_response =
        connection_->GetWindowAttributes({window}).Sync();
    if (!attributes_response) {
      continue;
    }
    if (attributes_response->map_state != x11::MapState::Viewable) {
      // Unmapped or hidden windows can be left alone - their geometries
      // might not be meaningful. If the window later becomes mapped, the
      // window-manager will be responsible for its placement.
      continue;
    }
    auto geometry_response = connection_->GetGeometry(window).Sync();
    if (!geometry_response) {
      continue;
    }

    // Look for any CRTC which contains the window's top-left corner. If the
    // CRTC is being moved, request the window to be moved the same amount.
    for (const auto& crtc_info : active_crtcs_) {
      if (!crtc_info.OffsetsChanged()) {
        continue;
      }

      auto old_rect = webrtc::DesktopRect::MakeXYWH(
          crtc_info.old_x, crtc_info.old_y, crtc_info.width, crtc_info.height);
      webrtc::DesktopVector window_top_left(geometry_response->x,
                                            geometry_response->y);
      if (!old_rect.Contains(window_top_left)) {
        continue;
      }

      webrtc::DesktopVector adjustment(crtc_info.x - crtc_info.old_x,
                                       crtc_info.y - crtc_info.old_y);
      window_top_left = window_top_left.add(adjustment);

      connection_->ConfigureWindow({
          .window = window,
          .x = window_top_left.x(),
          .y = window_top_left.y(),
      });
      break;
    }
  }
}

webrtc::DesktopSize X11CrtcResizer::GetBoundingBox() const {
  DCHECK(!bounding_box_size_.is_empty());
  return bounding_box_size_;
}

void X11CrtcResizer::ApplyActiveCrtcs() {
  for (const auto& crtc_info : active_crtcs_) {
    if (crtc_info.OffsetsChanged() || updated_crtcs_.contains(crtc_info.crtc)) {
      x11::Time config_timestamp = resources_->config_timestamp;
      randr_->SetCrtcConfig({
          .crtc = crtc_info.crtc,
          .timestamp = x11::Time::CurrentTime,
          .config_timestamp = config_timestamp,
          .x = crtc_info.x,
          .y = crtc_info.y,
          .mode = crtc_info.mode,
          .rotation = crtc_info.rotation,
          .outputs = crtc_info.outputs,
      });
    }
  }
  updated_crtcs_.clear();
}

void X11CrtcResizer::SetCrtcsForTest(
    std::vector<x11::RandR::GetCrtcInfoReply> crtcs) {
  int id = 1;
  for (const auto& crtc : crtcs) {
    AddCrtcFromReply(static_cast<x11::RandR::Crtc>(id), crtc);
    id++;
  }
}

std::vector<webrtc::DesktopRect> X11CrtcResizer::GetCrtcsForTest() const {
  std::vector<webrtc::DesktopRect> result;
  for (const auto& crtc_info : active_crtcs_) {
    result.push_back(webrtc::DesktopRect::MakeXYWH(
        crtc_info.x, crtc_info.y, crtc_info.width, crtc_info.height));
  }
  return result;
}

void X11CrtcResizer::AddCrtcFromReply(
    x11::RandR::Crtc crtc,
    const x11::RandR::GetCrtcInfoReply& reply) {
  active_crtcs_.emplace_back(crtc, reply.x, reply.y, reply.width, reply.height,
                             reply.mode, reply.rotation, reply.outputs);
}

bool X11CrtcResizer::LayoutIsVertical() const {
  if (active_crtcs_.size() <= 1)
    return false;

  // For simplicity, just pick 2 CRTCs arbitrarily.
  auto iter1 = active_crtcs_.begin();
  auto iter2 = std::next(iter1);

  // The cases:
  // --[---]--------
  // --------[---]--
  // and:
  // --------[---]--
  // --[---]--------
  // are not vertically stacked. The case where the CRTCs are exactly touching
  // is also not vertically stacked, because it comes from a horizontal
  // packing of CRTCs:
  // --[---]-------
  // -------[---]--
  // All other cases have overlapping projections so they are considered
  // vertically stacked.
  auto left1 = iter1->x;
  auto right1 = iter1->x + iter1->width;
  auto left2 = iter2->x;
  auto right2 = iter2->x + iter2->width;

  return right1 > left2 && right2 > left1;
}

void X11CrtcResizer::PackVertically(const webrtc::DesktopSize& new_size,
                                    x11::RandR::Crtc resized_crtc) {
  // Before applying the new size, test if right-alignment should be
  // preserved.
  DCHECK(!active_crtcs_.empty());
  bool is_left_aligned = true;
  bool is_right_aligned = true;
  auto first_crtc_left = active_crtcs_.front().x;
  auto first_crtc_right = first_crtc_left + active_crtcs_.front().width;
  for (const auto& crtc_info : active_crtcs_) {
    if (crtc_info.x != first_crtc_left) {
      is_left_aligned = false;
    }
    if (crtc_info.x + crtc_info.width != first_crtc_right) {
      is_right_aligned = false;
    }
  }

  bool keep_right_alignment = is_right_aligned && !is_left_aligned;

  // Apply the new size.
  for (auto& crtc_info : active_crtcs_) {
    if (crtc_info.crtc == resized_crtc) {
      crtc_info.width = new_size.width();
      crtc_info.height = new_size.height();
      break;
    }
  }

  // Sort vertically before packing.
  base::ranges::sort(active_crtcs_, std::less<>(), &CrtcInfo::y);

  // Pack the CRTCs by setting their y-offsets. If necessary, change the
  // x-offset for right-alignment.
  int current_y = 0;
  for (auto& crtc_info : active_crtcs_) {
    crtc_info.y = current_y;
    current_y += crtc_info.height;

    // Place all monitors left-aligned or right-aligned.
    // TODO(crbug.com/1326339): Implement a more sophisticated algorithm that
    // tries to preserve pairwise alignment. It is not enough to leave the
    // x-offsets unchanged here - this tends to result in the monitors being
    // arranged roughly diagonally, wasting lots of space. Some amount of
    // horizontal compression is needed to prevent this from happening.
    crtc_info.x = keep_right_alignment ? -crtc_info.width : 0;
  }
}

void X11CrtcResizer::PackHorizontally(const webrtc::DesktopSize& new_size,
                                      x11::RandR::Crtc resized_crtc) {
  webrtc::DesktopSize new_size_transposed(new_size.height(), new_size.width());
  Transpose();
  PackVertically(new_size_transposed, resized_crtc);
  Transpose();
}

void X11CrtcResizer::Transpose() {
  for (auto& crtc_info : active_crtcs_) {
    std::swap(crtc_info.x, crtc_info.y);
    std::swap(crtc_info.width, crtc_info.height);
  }
}

}  // namespace remoting