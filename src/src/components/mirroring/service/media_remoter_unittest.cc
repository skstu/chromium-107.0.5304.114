// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mirroring/service/media_remoter.h"

#include "base/run_loop.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "base/time/default_tick_clock.h"
#include "components/mirroring/service/message_dispatcher.h"
#include "components/mirroring/service/mirror_settings.h"
#include "components/mirroring/service/rpc_dispatcher_impl.h"
#include "components/openscreen_platform/task_runner.h"
#include "media/cast/cast_environment.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/openscreen/src/cast/streaming/environment.h"
#include "third_party/openscreen/src/cast/streaming/sender.h"
#include "third_party/openscreen/src/cast/streaming/sender_packet_router.h"
#include "third_party/openscreen/src/platform/api/time.h"
#include "third_party/openscreen/src/platform/base/trivial_clock_traits.h"

using ::testing::InvokeWithoutArgs;
using ::testing::_;
using ::testing::Mock;
using media::mojom::RemotingSinkMetadata;
using media::mojom::RemotingStopReason;
using media::cast::RtpPayloadType;
using media::cast::Codec;

namespace mirroring {

namespace {

constexpr uint32_t kFirstSsrc = 35535;
constexpr int kRtpTimebase = 9000;

constexpr std::array<uint8_t, 16> kAesSecretKey{1, 2,  3,  4,  5,  6,  7, 8,
                                                9, 10, 11, 12, 13, 14, 15};

constexpr std::array<uint8_t, 16> kAesIvMask{2,  3,  4,  5,  6,  7,  8, 9,
                                             10, 11, 12, 13, 14, 15, 16};

constexpr auto kDefaultPlayoutDelay = std::chrono::milliseconds(400);

// Set of simply initialized remoting openscreen::cast::Senders for use with the
// media remoter.
// TODO(https://crbug.com/1363719): openscreen::cast::Sender should be easier to
// initialize for tests.
struct OpenscreenTestSenders {
  OpenscreenTestSenders()
      : task_runner(base::SequencedTaskRunnerHandle::Get()),
        environment(openscreen::Clock::now,
                    &task_runner,
                    openscreen::IPEndpoint::kAnyV4()),
        sender_packet_router(&environment, 20, std::chrono::milliseconds(10)),

        audio_sender(&environment,
                     &sender_packet_router,
                     openscreen::cast::SessionConfig{
                         kFirstSsrc, kFirstSsrc + 1, kRtpTimebase,
                         2 /* channels */, kDefaultPlayoutDelay, kAesSecretKey,
                         kAesIvMask, true /* is_pli_enabled */},
                     openscreen::cast::RtpPayloadType::kAudioVarious),
        video_sender(&environment,
                     &sender_packet_router,
                     openscreen::cast::SessionConfig{
                         kFirstSsrc + 2, kFirstSsrc + 3, kRtpTimebase,
                         1 /* channels */, kDefaultPlayoutDelay, kAesSecretKey,
                         kAesIvMask, true /* is_pli_enabled */},
                     openscreen::cast::RtpPayloadType::kVideoVarious) {}

  openscreen_platform::TaskRunner task_runner;
  openscreen::cast::Environment environment;
  openscreen::cast::SenderPacketRouter sender_packet_router;
  openscreen::cast::Sender audio_sender;
  openscreen::cast::Sender video_sender;
};

class MockRemotingSource : public media::mojom::RemotingSource {
 public:
  MockRemotingSource() = default;
  ~MockRemotingSource() override = default;

  void Bind(mojo::PendingReceiver<media::mojom::RemotingSource> receiver) {
    receiver_.Bind(std::move(receiver));
  }

  MOCK_METHOD0(OnSinkGone, void());
  MOCK_METHOD0(OnStarted, void());
  MOCK_METHOD1(OnStartFailed, void(media::mojom::RemotingStartFailReason));
  MOCK_METHOD1(OnMessageFromSink, void(const std::vector<uint8_t>&));
  MOCK_METHOD1(OnStopped, void(RemotingStopReason));
  MOCK_METHOD1(OnSinkAvailable, void(const RemotingSinkMetadata&));
  void OnSinkAvailable(
      media::mojom::RemotingSinkMetadataPtr metadata) override {
    OnSinkAvailable(*metadata);
  }

 private:
  mojo::Receiver<media::mojom::RemotingSource> receiver_{this};
};

RemotingSinkMetadata DefaultSinkMetadata() {
  RemotingSinkMetadata metadata;
  metadata.features.push_back(media::mojom::RemotingSinkFeature::RENDERING);
  metadata.video_capabilities.push_back(
      media::mojom::RemotingSinkVideoCapability::CODEC_VP8);
  metadata.audio_capabilities.push_back(
      media::mojom::RemotingSinkAudioCapability::CODEC_BASELINE_SET);
  metadata.friendly_name = "Test";
  return metadata;
}

}  // namespace

class MediaRemoterTest : public mojom::CastMessageChannel,
                         public MediaRemoter::Client,
                         public ::testing::TestWithParam<bool> {
 public:
  MediaRemoterTest()
      : message_dispatcher_(receiver_.BindNewPipeAndPassRemote(),
                            inbound_channel_.BindNewPipeAndPassReceiver(),
                            error_callback_.Get()),
        rpc_dispatcher_(message_dispatcher_),
        sink_metadata_(DefaultSinkMetadata()) {}
  MediaRemoterTest(const MediaRemoterTest&) = delete;
  MediaRemoterTest& operator=(const MediaRemoterTest&) = delete;
  ~MediaRemoterTest() override { task_environment_.RunUntilIdle(); }

 protected:
  MOCK_METHOD1(Send, void(mojom::CastMessagePtr));
  MOCK_METHOD0(OnConnectToRemotingSource, void());
  MOCK_METHOD0(RequestRemotingStreaming, void());
  MOCK_METHOD0(RestartMirroringStreaming, void());

  // MediaRemoter::Client implementation.
  void ConnectToRemotingSource(
      mojo::PendingRemote<media::mojom::Remoter> remoter,
      mojo::PendingReceiver<media::mojom::RemotingSource> source_receiver)
      override {
    remoter_.Bind(std::move(remoter));
    remoting_source_.Bind(std::move(source_receiver));
    OnConnectToRemotingSource();
  }

  void CreateRemoter() {
    EXPECT_FALSE(media_remoter_);
    EXPECT_CALL(*this, OnConnectToRemotingSource());
    EXPECT_CALL(remoting_source_, OnSinkAvailable(_));
    media_remoter_ =
        std::make_unique<MediaRemoter>(*this, sink_metadata_, rpc_dispatcher_);
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  // Requests to start a remoting session.
  void StartRemoting() {
    ASSERT_TRUE(remoter_);
    EXPECT_CALL(*this, RequestRemotingStreaming());
    remoter_->Start();
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
  }

  // Stops the current remoting session.
  void StopRemoting() {
    ASSERT_TRUE(remoter_);
    EXPECT_CALL(remoting_source_, OnStopped(RemotingStopReason::USER_DISABLED));
    EXPECT_CALL(remoting_source_, OnSinkGone());
    EXPECT_CALL(*this, RestartMirroringStreaming());
    remoter_->Stop(media::mojom::RemotingStopReason::USER_DISABLED);
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  // Signals that a remoting streaming session starts successfully.
  void RemotingStreamingStarted(bool should_use_openscreen_senders) {
    ASSERT_TRUE(media_remoter_);
    scoped_refptr<media::cast::CastEnvironment> cast_environment =
        new media::cast::CastEnvironment(
            base::DefaultTickClock::GetInstance(),
            task_environment_.GetMainThreadTaskRunner(),
            task_environment_.GetMainThreadTaskRunner(),
            task_environment_.GetMainThreadTaskRunner());

    if (should_use_openscreen_senders) {
      openscreen_test_senders_ = std::make_unique<OpenscreenTestSenders>();
      media_remoter_->StartRpcMessaging(
          cast_environment, &openscreen_test_senders_->audio_sender,
          &openscreen_test_senders_->video_sender,
          MirrorSettings::GetDefaultAudioConfig(RtpPayloadType::REMOTE_AUDIO,
                                                Codec::CODEC_AUDIO_REMOTE),
          MirrorSettings::GetDefaultVideoConfig(RtpPayloadType::REMOTE_VIDEO,
                                                Codec::CODEC_VIDEO_REMOTE));
    } else {
      media_remoter_->StartRpcMessaging(
          cast_environment, nullptr, media::cast::FrameSenderConfig(),
          MirrorSettings::GetDefaultVideoConfig(RtpPayloadType::REMOTE_VIDEO,
                                                Codec::CODEC_VIDEO_REMOTE));
    }
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(&remoting_source_);
  }

  // Signals that mirroring is resumed successfully.
  void MirroringResumed() {
    EXPECT_CALL(remoting_source_, OnSinkAvailable(_));
    media_remoter_->OnMirroringResumed();
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(&remoting_source_);
  }

  // Signals that remoting session failed to start.
  void RemotingStartFailed() {
    ASSERT_TRUE(media_remoter_);
    EXPECT_CALL(remoting_source_, OnStartFailed(_));
    EXPECT_CALL(remoting_source_, OnSinkGone());
    EXPECT_CALL(*this, RestartMirroringStreaming());
    media_remoter_->OnRemotingFailed();
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  testing::StrictMock<MockRemotingSource>& remoting_source() {
    return remoting_source_;
  }

 private:
  base::test::TaskEnvironment task_environment_;
  mojo::Receiver<mojom::CastMessageChannel> receiver_{this};
  base::MockCallback<MessageDispatcher::ErrorCallback> error_callback_;
  mojo::Remote<mojom::CastMessageChannel> inbound_channel_;
  MessageDispatcher message_dispatcher_;
  RpcDispatcherImpl rpc_dispatcher_;
  const media::mojom::RemotingSinkMetadata sink_metadata_;
  testing::StrictMock<MockRemotingSource> remoting_source_;
  mojo::Remote<media::mojom::Remoter> remoter_;

  // Configured for use by the media remoter.
  std::unique_ptr<OpenscreenTestSenders> openscreen_test_senders_;
  std::unique_ptr<MediaRemoter> media_remoter_;
};

TEST_P(MediaRemoterTest, StartAndStopRemoting) {
  CreateRemoter();
  StartRemoting();
  EXPECT_CALL(remoting_source(), OnStarted());
  RemotingStreamingStarted(GetParam());
  StopRemoting();
}

TEST_P(MediaRemoterTest, StartRemotingWithoutCallingStart) {
  CreateRemoter();
  // Should fail since we didn't call `StartRemoting().`
  EXPECT_CALL(remoting_source(), OnStarted()).Times(0);
  RemotingStreamingStarted(GetParam());
}

TEST_P(MediaRemoterTest, StopRemotingWhileStarting) {
  CreateRemoter();
  // Starts a remoting session.
  StartRemoting();
  // Immediately stops the remoting session while not started yet.
  StopRemoting();

  // Signals that successfully switch to mirroring.
  MirroringResumed();
  // Now remoting can be started again.
  StartRemoting();
}

TEST_P(MediaRemoterTest, RemotingStartFailed) {
  CreateRemoter();
  StartRemoting();
  RemotingStartFailed();
}

TEST_P(MediaRemoterTest, SwitchBetweenMultipleSessions) {
  CreateRemoter();

  // Start a remoting session.
  StartRemoting();
  EXPECT_CALL(remoting_source(), OnStarted());
  RemotingStreamingStarted(GetParam());

  // Stop the remoting session and switch to mirroring.
  StopRemoting();
  MirroringResumed();

  // Switch to remoting again.
  StartRemoting();
  EXPECT_CALL(remoting_source(), OnStarted());
  RemotingStreamingStarted(GetParam());

  // Switch to mirroring again.
  StopRemoting();
  MirroringResumed();
}

INSTANTIATE_TEST_SUITE_P(MediaRemoter,
                         MediaRemoterTest,
                         ::testing::Values(true, false));

}  // namespace mirroring
