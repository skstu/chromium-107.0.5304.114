// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CAST_STREAMING_RENDERER_PLAYBACK_COMMAND_FORWARDING_RENDERER_FACTORY_H_
#define COMPONENTS_CAST_STREAMING_RENDERER_PLAYBACK_COMMAND_FORWARDING_RENDERER_FACTORY_H_

#include <memory>

#include "media/base/renderer_factory.h"
#include "media/mojo/mojom/renderer.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace cast_streaming {

// This class defines a RendererFactory used to create a
// PlaybackCommandForwardingRenderer, for use with Cast streaming. This Renderer
// type is intended to be used for both the Cast Mirroring and Cast Remoting
// scenarios, specifically to streams generated by desktop or tab mirroring /
// remoting. The Initialize call is delegated to the |real_renderer_| while all
// other calls are no-ops. Instead, only in the case of remoting, these commands
// are sent from the user device and communicated here over the ctor-provided
// |pending_rederer_controls|.
//
// The mirroring can be summarized as being that of a user is trying to take
// what's currently displayed on their device, and send those bits to a larger
// screen. So it should be an exact duplicate of what they see locally - a
// faithful copy. Streams generated by mirroring screen contents from the sender
// device consist of demuxed media frames, and are simply played out in real
// time on the delegated Renderer. Remoting, by comparison, is simply an
// optimization on top of mirroring - in practice, the receiver here cannot
// (and does not need to) distinguish between the two. Mirroring does not accept
// media commands because, by definition, it is mirroring some content, so there
// is no notion of starting playback, seeking around, et cetera. For remoting,
// commands sent by the user over mojo are used to control playback. That being
// said, as this Renderer does not differentiate between its use for Mirroring
// and Remoting (and that a streaming session may change between the two without
// re-creating the Renderer), playback commands sent over Mojo will be respected
// regardless of which Cast Streaming type is being used.
//
// Therefore, the |pending_rederer_controls| serves two purposes:
// - Playback control during a Remoting session.
// - Starting playback of any Cast Streaming session once the browser process
//   has begun streaming end-user provided data.
class PlaybackCommandForwardingRendererFactory : public media::RendererFactory {
 public:
  // |renderer_factory| is the RendererFactory to be used as described below.
  explicit PlaybackCommandForwardingRendererFactory(
      mojo::PendingReceiver<media::mojom::Renderer> pending_renderer_controls);
  PlaybackCommandForwardingRendererFactory(
      const PlaybackCommandForwardingRendererFactory& other) = delete;
  PlaybackCommandForwardingRendererFactory(
      PlaybackCommandForwardingRendererFactory&& other) = delete;

  ~PlaybackCommandForwardingRendererFactory() override;

  PlaybackCommandForwardingRendererFactory& operator=(
      const PlaybackCommandForwardingRendererFactory& other) = delete;
  PlaybackCommandForwardingRendererFactory& operator=(
      PlaybackCommandForwardingRendererFactory&& other) = delete;

  // Sets the RendererFactory which will be used in CreateRenderer(). May only
  // be called prior to any call to CreateRenderer(). |wrapped_factory| must
  // persist for the duration of this class's lifetime.
  void SetWrappedRendererFactory(media::RendererFactory* wrapped_factory);

  // RendererFactory overrides.
  //
  // Wraps |real_renderer_factory_->CreateRenderer()|'s results with a
  // PlaybackCommandForwardingRenderer instance.
  std::unique_ptr<media::Renderer> CreateRenderer(
      const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
      const scoped_refptr<base::TaskRunner>& worker_task_runner,
      media::AudioRendererSink* audio_renderer_sink,
      media::VideoRendererSink* video_renderer_sink,
      media::RequestOverlayInfoCB request_overlay_info_cb,
      const gfx::ColorSpace& target_color_space) override;

 private:
  mojo::PendingReceiver<media::mojom::Renderer> pending_renderer_controls_;

  media::RendererFactory* real_renderer_factory_;
  bool has_create_been_called_ = false;
};

}  // namespace cast_streaming

#endif  // COMPONENTS_CAST_STREAMING_RENDERER_PLAYBACK_COMMAND_FORWARDING_RENDERER_FACTORY_H_
