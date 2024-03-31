// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mirroring/service/media_remoter.h"

#include "base/bind.h"
#include "base/logging.h"
#include "components/mirroring/service/remoting_sender.h"
#include "components/mirroring/service/rpc_dispatcher.h"
#include "media/base/media_switches.h"
#include "media/cast/net/cast_transport.h"
#include "third_party/openscreen/src/cast/streaming/sender.h"

using media::cast::Codec;
using media::cast::FrameSenderConfig;

namespace mirroring {

MediaRemoter::MediaRemoter(
    Client& client,
    const media::mojom::RemotingSinkMetadata& sink_metadata,
    RpcDispatcher& rpc_dispatcher)
    : client_(client),
      sink_metadata_(sink_metadata),
      rpc_dispatcher_(rpc_dispatcher),
      state_(MIRRORING) {
  client_->ConnectToRemotingSource(
      receiver_.BindNewPipeAndPassRemote(),
      remoting_source_.BindNewPipeAndPassReceiver());
  remoting_source_->OnSinkAvailable(sink_metadata_.Clone());
}

MediaRemoter::~MediaRemoter() {
  // Stop this remoting session if mirroring is stopped during a remoting
  // session. For example, user stops mirroring through the cast dialog or
  // closes the tab.
  Stop(media::mojom::RemotingStopReason::ROUTE_TERMINATED);
}

void MediaRemoter::OnMessageFromSink(const std::vector<uint8_t>& response) {
  remoting_source_->OnMessageFromSink(response);
}

void MediaRemoter::StartRpcMessaging(
    scoped_refptr<media::cast::CastEnvironment> cast_environment,
    openscreen::cast::Sender* audio_sender,
    openscreen::cast::Sender* video_sender,
    const FrameSenderConfig& audio_config,
    const FrameSenderConfig& video_config) {
  DCHECK(audio_sender || video_sender);
  DCHECK(!openscreen_audio_sender_);
  DCHECK(!openscreen_video_sender_);
  DCHECK(!transport_);

  openscreen_audio_sender_ = audio_sender;
  openscreen_video_sender_ = video_sender;
  StartRpcMessagingInternal(std::move(cast_environment), audio_config,
                            video_config);
}

void MediaRemoter::StartRpcMessaging(
    scoped_refptr<media::cast::CastEnvironment> cast_environment,
    media::cast::CastTransport* transport,
    const FrameSenderConfig& audio_config,
    const FrameSenderConfig& video_config) {
  DCHECK(!openscreen_audio_sender_);
  DCHECK(!openscreen_video_sender_);
  DCHECK(!transport_);

  transport_ = transport;
  StartRpcMessagingInternal(cast_environment, audio_config, video_config);
}

void MediaRemoter::StartRpcMessagingInternal(
    scoped_refptr<media::cast::CastEnvironment> cast_environment,
    const FrameSenderConfig& audio_config,
    const FrameSenderConfig& video_config) {
  DCHECK(!cast_environment_);

  if (state_ != STARTING_REMOTING) {
    transport_ = nullptr;
    openscreen_audio_sender_ = nullptr;
    openscreen_video_sender_ = nullptr;
    return;  // Start operation was canceled.
  }

  // A remoting streaming session started. Start RPC message transport and
  // notify the remoting source to start data streaming.
  cast_environment_ = std::move(cast_environment);
  audio_config_ = audio_config;
  video_config_ = video_config;
  rpc_dispatcher_->Subscribe(base::BindRepeating(
      &MediaRemoter::OnMessageFromSink, weak_factory_.GetWeakPtr()));
  state_ = REMOTING_STARTED;
  remoting_source_->OnStarted();
}

void MediaRemoter::OnMirroringResumed() {
  if (state_ == REMOTING_DISABLED)
    return;
  DCHECK_EQ(STOPPING_REMOTING, state_);
  state_ = MIRRORING;
  // Notify the remoting source to enable starting media remoting again.
  remoting_source_->OnSinkAvailable(sink_metadata_.Clone());
}

void MediaRemoter::OnRemotingFailed() {
  DCHECK(state_ == STARTING_REMOTING || state_ == REMOTING_STARTED);
  if (state_ == STARTING_REMOTING) {
    remoting_source_->OnStartFailed(
        media::mojom::RemotingStartFailReason::INVALID_ANSWER_MESSAGE);
  }
  state_ = REMOTING_DISABLED;
  remoting_source_->OnSinkGone();
  // Fallback to mirroring.
  client_->RestartMirroringStreaming();
}

void MediaRemoter::Stop(media::mojom::RemotingStopReason reason) {
  if (state_ != STARTING_REMOTING && state_ != REMOTING_STARTED)
    return;
  if (state_ == REMOTING_STARTED) {
    rpc_dispatcher_->Unsubscribe();
    audio_sender_.reset();
    video_sender_.reset();
    cast_environment_ = nullptr;
    transport_ = nullptr;
    openscreen_audio_sender_ = nullptr;
    openscreen_video_sender_ = nullptr;
    audio_config_ = FrameSenderConfig();
    video_config_ = FrameSenderConfig();
  }
  state_ = STOPPING_REMOTING;
  remoting_source_->OnStopped(reason);
  // Prevent the start of remoting until switching completes.
  remoting_source_->OnSinkGone();
  // Switch to mirroring.
  client_->RestartMirroringStreaming();
}

void MediaRemoter::Start() {
  if (state_ != MIRRORING) {
    VLOG(2) << "Warning: Ignore start request. state=" << state_;
    return;
  }
  state_ = STARTING_REMOTING;
  client_->RequestRemotingStreaming();
}

void MediaRemoter::StartDataStreams(
    mojo::ScopedDataPipeConsumerHandle audio_pipe,
    mojo::ScopedDataPipeConsumerHandle video_pipe,
    mojo::PendingReceiver<media::mojom::RemotingDataStreamSender>
        audio_sender_receiver,
    mojo::PendingReceiver<media::mojom::RemotingDataStreamSender>
        video_sender_receiver) {
  if (state_ != REMOTING_STARTED)
    return;  // Stop() was called before.
  DCHECK(cast_environment_);
  if (base::FeatureList::IsEnabled(media::kOpenscreenCastStreamingSession)) {
    StartOpenscreenDataStreams(std::move(audio_pipe), std::move(video_pipe),
                               std::move(audio_sender_receiver),
                               std::move(video_sender_receiver));
  } else {
    StartLegacyDataStreams(std::move(audio_pipe), std::move(video_pipe),
                           std::move(audio_sender_receiver),
                           std::move(video_sender_receiver));
  }
}

void MediaRemoter::StartOpenscreenDataStreams(
    mojo::ScopedDataPipeConsumerHandle audio_pipe,
    mojo::ScopedDataPipeConsumerHandle video_pipe,
    mojo::PendingReceiver<media::mojom::RemotingDataStreamSender>
        audio_sender_receiver,
    mojo::PendingReceiver<media::mojom::RemotingDataStreamSender>
        video_sender_receiver) {
  DCHECK(openscreen_audio_sender_ || openscreen_video_sender_);

  if (audio_pipe.is_valid() &&
      audio_config_.codec == Codec::CODEC_AUDIO_REMOTE &&
      openscreen_audio_sender_) {
    // NOTE: use of base::Unretained is safe because we own the sender.
    audio_sender_ = std::make_unique<RemotingSender>(
        cast_environment_, openscreen_audio_sender_, audio_config_,
        std::move(audio_pipe), std::move(audio_sender_receiver),
        base::BindOnce(&MediaRemoter::OnRemotingDataStreamError,
                       base::Unretained(this)));
  }

  if (video_pipe.is_valid() &&
      video_config_.codec == Codec::CODEC_VIDEO_REMOTE &&
      openscreen_video_sender_) {
    // NOTE: use of base::Unretained is safe because we own the sender.
    video_sender_ = std::make_unique<RemotingSender>(
        cast_environment_, openscreen_video_sender_, video_config_,
        std::move(video_pipe), std::move(video_sender_receiver),
        base::BindOnce(&MediaRemoter::OnRemotingDataStreamError,
                       base::Unretained(this)));
  }
}

void MediaRemoter::StartLegacyDataStreams(
    mojo::ScopedDataPipeConsumerHandle audio_pipe,
    mojo::ScopedDataPipeConsumerHandle video_pipe,
    mojo::PendingReceiver<media::mojom::RemotingDataStreamSender>
        audio_sender_receiver,
    mojo::PendingReceiver<media::mojom::RemotingDataStreamSender>
        video_sender_receiver) {
  if (audio_pipe.is_valid() &&
      audio_config_.codec == Codec::CODEC_AUDIO_REMOTE) {
    DCHECK(transport_);
    audio_sender_ = std::make_unique<RemotingSender>(
        cast_environment_, transport_, audio_config_, std::move(audio_pipe),
        std::move(audio_sender_receiver),
        base::BindOnce(&MediaRemoter::OnRemotingDataStreamError,
                       base::Unretained(this)));
  }

  if (video_pipe.is_valid() &&
      video_config_.codec == Codec::CODEC_VIDEO_REMOTE) {
    DCHECK(transport_);
    video_sender_ = std::make_unique<RemotingSender>(
        cast_environment_, transport_, video_config_, std::move(video_pipe),
        std::move(video_sender_receiver),
        base::BindOnce(&MediaRemoter::OnRemotingDataStreamError,
                       base::Unretained(this)));
  }
}

void MediaRemoter::SendMessageToSink(const std::vector<uint8_t>& message) {
  if (state_ != REMOTING_STARTED)
    return;
  rpc_dispatcher_->SendOutboundMessage(message);
}

void MediaRemoter::EstimateTransmissionCapacity(
    media::mojom::Remoter::EstimateTransmissionCapacityCallback callback) {
  NOTIMPLEMENTED();
  std::move(callback).Run(0);
}

void MediaRemoter::OnRemotingDataStreamError() {
  if (state_ != REMOTING_STARTED)
    return;
  state_ = REMOTING_DISABLED;
  Stop(media::mojom::RemotingStopReason::DATA_SEND_FAILED);
}

}  // namespace mirroring
