// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/encoding/fake_software_video_encoder.h"

#include <stddef.h>

#include "base/json/json_writer.h"
#include "base/values.h"
#include "media/base/video_frame.h"
#include "media/cast/common/encoded_frame.h"
#include "media/cast/common/frame_id.h"
#include "media/cast/common/openscreen_conversion_helpers.h"
#include "media/cast/common/rtp_time.h"
#include "media/cast/common/sender_encoded_frame.h"
#include "media/cast/constants.h"

namespace media {
namespace cast {

FakeSoftwareVideoEncoder::FakeSoftwareVideoEncoder(
    const FrameSenderConfig& video_config)
    : video_config_(video_config),
      next_frame_is_key_(true),
      frame_id_(FrameId::first()),
      frame_size_(0) {
  DCHECK_GT(video_config_.max_frame_rate, 0);
}

FakeSoftwareVideoEncoder::~FakeSoftwareVideoEncoder() = default;

void FakeSoftwareVideoEncoder::Initialize() {}

void FakeSoftwareVideoEncoder::Encode(
    scoped_refptr<media::VideoFrame> video_frame,
    base::TimeTicks reference_time,
    SenderEncodedFrame* encoded_frame) {
  DCHECK(encoded_frame);

  if (video_frame->visible_rect().size() != last_frame_size_) {
    next_frame_is_key_ = true;
    last_frame_size_ = video_frame->visible_rect().size();
  }

  encoded_frame->frame_id = frame_id_++;
  if (next_frame_is_key_) {
    encoded_frame->dependency = EncodedFrame::KEY;
    encoded_frame->referenced_frame_id = encoded_frame->frame_id;
    next_frame_is_key_ = false;
  } else {
    encoded_frame->dependency = EncodedFrame::DEPENDENT;
    encoded_frame->referenced_frame_id = encoded_frame->frame_id - 1;
  }
  encoded_frame->rtp_timestamp =
      ToRtpTimeTicks(video_frame->timestamp(), kVideoFrequency);
  encoded_frame->reference_time = reference_time;

  base::Value values(base::Value::Type::DICTIONARY);
  values.SetBoolKey("key", encoded_frame->dependency == EncodedFrame::KEY);
  values.SetIntKey("ref", encoded_frame->referenced_frame_id.lower_32_bits());
  values.SetIntKey("id", encoded_frame->frame_id.lower_32_bits());
  values.SetIntKey("size", frame_size_);
  base::JSONWriter::Write(values, &encoded_frame->data);
  encoded_frame->data.resize(
      std::max<size_t>(encoded_frame->data.size(), frame_size_), ' ');

  if (encoded_frame->dependency == EncodedFrame::KEY) {
    encoded_frame->encoder_utilization = 1.0;
    encoded_frame->lossiness = 6.0;
  } else {
    encoded_frame->encoder_utilization = 0.8;
    encoded_frame->lossiness = 0.8;
  }
}

void FakeSoftwareVideoEncoder::UpdateRates(uint32_t new_bitrate) {
  frame_size_ = new_bitrate / video_config_.max_frame_rate / 8;
}

void FakeSoftwareVideoEncoder::GenerateKeyFrame() {
  next_frame_is_key_ = true;
}

}  // namespace cast
}  // namespace media
