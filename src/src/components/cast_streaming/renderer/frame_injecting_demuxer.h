// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CAST_STREAMING_RENDERER_FRAME_INJECTING_DEMUXER_H_
#define COMPONENTS_CAST_STREAMING_RENDERER_FRAME_INJECTING_DEMUXER_H_

#include "components/cast_streaming/public/mojom/demuxer_connector.mojom.h"
#include "media/base/demuxer.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace cast_streaming {

class FrameInjectingAudioDemuxerStream;
class FrameInjectingVideoDemuxerStream;
class DemuxerConnector;

// media::Demuxer implementation for a Cast Streaming Receiver.
// This object is instantiated on the main thread, whose task runner is stored
// as |original_task_runner_|. OnStreamsInitialized() is the only method called
// on the main thread. Every other method is called on the media thread, whose
// task runner is |media_task_runner_|.
// TODO(crbug.com/1082821): Simplify the FrameInjectingDemuxer initialization
// sequence when the DemuxerConnector Component has been implemented.
class FrameInjectingDemuxer final : public media::Demuxer {
 public:
  FrameInjectingDemuxer(
      DemuxerConnector* demuxer_connector,
      scoped_refptr<base::SingleThreadTaskRunner> media_task_runner);
  ~FrameInjectingDemuxer() override;

  FrameInjectingDemuxer(const FrameInjectingDemuxer&) = delete;
  FrameInjectingDemuxer& operator=(const FrameInjectingDemuxer&) = delete;

  void OnStreamsInitialized(
      mojom::AudioStreamInitializationInfoPtr audio_stream_info,
      mojom::VideoStreamInitializationInfoPtr video_stream_info);

 private:
  void OnStreamsInitializedOnMediaThread(
      mojom::AudioStreamInitializationInfoPtr audio_stream_info,
      mojom::VideoStreamInitializationInfoPtr video_stream_info);
  void OnStreamInitializationComplete();

  // media::Demuxer implementation.
  std::vector<media::DemuxerStream*> GetAllStreams() override;
  std::string GetDisplayName() const override;
  void Initialize(media::DemuxerHost* host,
                  media::PipelineStatusCallback status_cb) override;
  void AbortPendingReads() override;
  void StartWaitingForSeek(base::TimeDelta seek_time) override;
  void CancelPendingSeek(base::TimeDelta seek_time) override;
  void Seek(base::TimeDelta time,
            media::PipelineStatusCallback status_cb) override;
  void Stop() override;
  base::TimeDelta GetStartTime() const override;
  base::Time GetTimelineOffset() const override;
  int64_t GetMemoryUsage() const override;
  absl::optional<media::container_names::MediaContainerName>
  GetContainerForMetrics() const override;
  void OnEnabledAudioTracksChanged(
      const std::vector<media::MediaTrack::Id>& track_ids,
      base::TimeDelta curr_time,
      TrackChangeCB change_completed_cb) override;
  void OnSelectedVideoTrackChanged(
      const std::vector<media::MediaTrack::Id>& track_ids,
      base::TimeDelta curr_time,
      TrackChangeCB change_completed_cb) override;

  // The number of initialized streams that have yet to call
  // OnStreamInitializationComplete().
  int pending_stream_initialization_callbacks_ = 0;

  scoped_refptr<base::SingleThreadTaskRunner> media_task_runner_;
  scoped_refptr<base::SequencedTaskRunner> original_task_runner_;
  media::DemuxerHost* host_ = nullptr;
  std::unique_ptr<FrameInjectingAudioDemuxerStream> audio_stream_;
  std::unique_ptr<FrameInjectingVideoDemuxerStream> video_stream_;

  // Set to true if the Demuxer was successfully initialized.
  bool was_initialization_successful_ = false;
  media::PipelineStatusCallback initialized_cb_;
  DemuxerConnector* const demuxer_connector_;

  base::WeakPtrFactory<FrameInjectingDemuxer> weak_factory_;
};

}  // namespace cast_streaming

#endif  // COMPONENTS_CAST_STREAMING_RENDERER_FRAME_INJECTING_DEMUXER_H_
