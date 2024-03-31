// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mirroring/service/openscreen_session_host.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/mirroring/service/fake_network_service.h"
#include "components/mirroring/service/fake_video_capture_host.h"
#include "components/mirroring/service/mirror_settings.h"
#include "components/mirroring/service/mirroring_features.h"
#include "components/mirroring/service/receiver_response.h"
#include "components/mirroring/service/value_util.h"
#include "media/base/media_switches.h"
#include "media/cast/test/utility/default_config.h"
#include "media/cast/test/utility/net_utility.h"
#include "media/video/video_decode_accelerator.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/ip_address.h"
#include "services/viz/public/cpp/gpu/gpu.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/jsoncpp/source/include/json/reader.h"
#include "third_party/jsoncpp/source/include/json/writer.h"
#include "third_party/openscreen/src/cast/streaming/message_fields.h"
#include "third_party/openscreen/src/cast/streaming/offer_messages.h"
#include "third_party/openscreen/src/cast/streaming/remoting_capabilities.h"
#include "third_party/openscreen/src/cast/streaming/sender_message.h"
#include "third_party/openscreen/src/cast/streaming/ssrc.h"

using media::cast::FrameSenderConfig;
using media::cast::Packet;
using media::mojom::RemotingSinkMetadata;
using media::mojom::RemotingSinkMetadataPtr;
using media::mojom::RemotingStartFailReason;
using media::mojom::RemotingStopReason;
using mirroring::mojom::SessionError;
using mirroring::mojom::SessionType;
using openscreen::ErrorOr;
using openscreen::cast::SenderMessage;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;

namespace mirroring {

namespace {

constexpr int kDefaultPlayoutDelay = 400;  // ms

const openscreen::cast::Answer kAnswerWithConstraints{
    1234,
    // Send indexes and SSRCs are set later.
    {},
    {},
    openscreen::cast::Constraints{
        openscreen::cast::AudioConstraints{44100, 2, 32000, 960000,
                                           std::chrono::milliseconds(4000)},
        openscreen::cast::VideoConstraints{
            40000.0, openscreen::cast::Dimensions{320, 480, {30, 1}},
            openscreen::cast::Dimensions{1920, 1080, {60, 1}}, 300000,
            144000000, std::chrono::milliseconds(4000)}},
    openscreen::cast::DisplayDescription{
        openscreen::cast::Dimensions{1280, 720, {60, 1}},
        openscreen::cast::AspectRatio{16, 9},
        openscreen::cast::AspectRatioConstraint::kFixed,
    },
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
  MOCK_METHOD1(OnStartFailed, void(RemotingStartFailReason));
  MOCK_METHOD1(OnMessageFromSink, void(const std::vector<uint8_t>&));
  MOCK_METHOD1(OnStopped, void(RemotingStopReason));
  MOCK_METHOD1(OnSinkAvailable, void(const RemotingSinkMetadata&));
  void OnSinkAvailable(RemotingSinkMetadataPtr metadata) override {
    OnSinkAvailable(*metadata);
  }

 private:
  mojo::Receiver<media::mojom::RemotingSource> receiver_{this};
};

Json::Value ParseAsJsoncppValue(absl::string_view document) {
  Json::CharReaderBuilder builder;
  Json::CharReaderBuilder::strictMode(&builder.settings_);
  EXPECT_FALSE(document.empty());

  Json::Value root_node;
  std::string error_msg;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  EXPECT_TRUE(
      reader->parse(document.begin(), document.end(), &root_node, &error_msg));

  return root_node;
}

std::string Stringify(const Json::Value& value) {
  EXPECT_FALSE(value.empty());
  Json::StreamWriterBuilder factory;
  factory["indentation"] = "";

  std::unique_ptr<Json::StreamWriter> const writer(factory.newStreamWriter());
  std::ostringstream stream;
  writer->write(value, &stream);

  EXPECT_TRUE(stream);
  return stream.str();
}

}  // namespace

class OpenscreenSessionHostTest : public mojom::ResourceProvider,
                                  public mojom::SessionObserver,
                                  public mojom::CastMessageChannel,
                                  public ::testing::Test {
 public:
  OpenscreenSessionHostTest()
      : feature_list_(media::kOpenscreenCastStreamingSession) {}

  OpenscreenSessionHostTest(const OpenscreenSessionHostTest&) = delete;
  OpenscreenSessionHostTest& operator=(const OpenscreenSessionHostTest&) =
      delete;

  ~OpenscreenSessionHostTest() override { task_environment_.RunUntilIdle(); }

 protected:
  // mojom::SessionObserver implementation.
  MOCK_METHOD1(OnError, void(SessionError));
  MOCK_METHOD0(DidStart, void());
  MOCK_METHOD0(DidStop, void());
  MOCK_METHOD1(LogInfoMessage, void(const std::string&));
  MOCK_METHOD1(LogErrorMessage, void(const std::string&));

  MOCK_METHOD0(OnGetVideoCaptureHost, void());
  MOCK_METHOD0(OnGetNetworkContext, void());
  MOCK_METHOD0(OnCreateAudioStream, void());
  MOCK_METHOD0(OnConnectToRemotingSource, void());

  // Called when an outbound message is sent.
  MOCK_METHOD1(OnOutboundMessage, void(SenderMessage::Type type));

  MOCK_METHOD0(OnInitialized, void());

  // mojom::CastMessageHandler overrides.
  void Send(mojom::CastMessagePtr message) override {
    EXPECT_TRUE(message->message_namespace == mojom::kWebRtcNamespace ||
                message->message_namespace == mojom::kRemotingNamespace);

    const Json::Value json_value =
        ParseAsJsoncppValue(message->json_format_data);

    ErrorOr<SenderMessage> parsed_message = SenderMessage::Parse(json_value);
    EXPECT_TRUE(parsed_message);
    last_sent_offer_ = parsed_message.value();
    if (parsed_message.value().type == SenderMessage::Type::kOffer) {
      EXPECT_GT(parsed_message.value().sequence_number, 0);
      const auto offer =
          absl::get<openscreen::cast::Offer>(parsed_message.value().body);

      for (const openscreen::cast::AudioStream& stream : offer.audio_streams) {
        EXPECT_EQ(std::chrono::milliseconds(stream.stream.target_delay).count(),
                  target_playout_delay_ms_);
      }
      for (const openscreen::cast::VideoStream& stream : offer.video_streams) {
        EXPECT_EQ(std::chrono::milliseconds(stream.stream.target_delay).count(),
                  target_playout_delay_ms_);
      }
    } else if (parsed_message.value().type ==
               SenderMessage::Type::kGetCapabilities) {
      EXPECT_GT(parsed_message.value().sequence_number, 0);
    }

    OnOutboundMessage(parsed_message.value().type);
  }

  // mojom::ResourceProvider overrides.
  void BindGpu(mojo::PendingReceiver<viz::mojom::Gpu> receiver) override {}
  void GetVideoCaptureHost(
      mojo::PendingReceiver<media::mojom::VideoCaptureHost> receiver) override {
    video_host_ =
        std::make_unique<NiceMock<FakeVideoCaptureHost>>(std::move(receiver));
    OnGetVideoCaptureHost();
  }

  void GetNetworkContext(
      mojo::PendingReceiver<network::mojom::NetworkContext> receiver) override {
    network_context_ =
        std::make_unique<NiceMock<MockNetworkContext>>(std::move(receiver));
    OnGetNetworkContext();
  }

  void CreateAudioStream(
      mojo::PendingRemote<mojom::AudioStreamCreatorClient> client,
      const media::AudioParameters& params,
      uint32_t total_segments) override {
    OnCreateAudioStream();
  }

  void ConnectToRemotingSource(
      mojo::PendingRemote<media::mojom::Remoter> remoter,
      mojo::PendingReceiver<media::mojom::RemotingSource> receiver) override {
    remoter_.Bind(std::move(remoter));
    remoting_source_.Bind(std::move(receiver));
    OnConnectToRemotingSource();
  }

  void GenerateAndReplyWithAnswer() {
    ASSERT_TRUE(session_host_);
    ASSERT_TRUE(last_sent_offer_);

    const openscreen::cast::Offer& offer =
        absl::get<openscreen::cast::Offer>(last_sent_offer_.value().body);
    openscreen::cast::Answer answer{.udp_port = 1234};

    if (!offer.audio_streams.empty()) {
      answer.send_indexes.push_back(offer.audio_streams[0].stream.index);
      answer.ssrcs.push_back(next_receiver_ssrc_++);
    }

    if (!offer.video_streams.empty()) {
      answer.send_indexes.push_back(offer.video_streams[0].stream.index);
      answer.ssrcs.push_back(next_receiver_ssrc_++);
    }

    openscreen::cast::ReceiverMessage receiver_message{
        .type = openscreen::cast::ReceiverMessage::Type::kAnswer,
        .sequence_number = last_sent_offer_.value().sequence_number,
        .valid = true,
        .body = std::move(answer)};
    Json::Value message_json = receiver_message.ToJson().value();
    ErrorOr<std::string> message_string = Stringify(message_json);
    ASSERT_TRUE(message_string);

    mojom::CastMessagePtr message = mojom::CastMessage::New(
        openscreen::cast::kCastWebrtcNamespace, message_string.value());
    inbound_channel_->Send(std::move(message));
  }

  OpenscreenSessionHost::AsyncInitializedCallback MakeOnInitializedCallback() {
    return base::BindOnce(&OpenscreenSessionHostTest::OnInitialized,
                          base::Unretained(this));
  }

  // Create a mirroring session. Expect to send OFFER message.
  void CreateSession(SessionType session_type) {
    session_type_ = session_type;
    mojom::SessionParametersPtr session_params =
        mojom::SessionParameters::New();
    session_params->type = session_type_;
    session_params->receiver_address = receiver_endpoint_.address();
    session_params->receiver_model_name = "Chromecast";
    session_params->source_id = "sender-123";
    session_params->destination_id = "receiver-456";
    if (target_playout_delay_ms_ != kDefaultPlayoutDelay) {
      session_params->target_playout_delay =
          base::Milliseconds(target_playout_delay_ms_);
    }
    cast_mode_ = "mirroring";
    mojo::PendingRemote<mojom::ResourceProvider> resource_provider_remote;
    mojo::PendingRemote<mojom::SessionObserver> session_observer_remote;
    mojo::PendingRemote<mojom::CastMessageChannel> outbound_channel_remote;
    resource_provider_receiver_.Bind(
        resource_provider_remote.InitWithNewPipeAndPassReceiver());
    session_observer_receiver_.Bind(
        session_observer_remote.InitWithNewPipeAndPassReceiver());
    outbound_channel_receiver_.Bind(
        outbound_channel_remote.InitWithNewPipeAndPassReceiver());
    // Expect to send OFFER message when session is created.
    EXPECT_CALL(*this, OnGetNetworkContext());
    EXPECT_CALL(*this, OnError(_)).Times(0);
    EXPECT_CALL(*this, OnOutboundMessage(SenderMessage::Type::kOffer));
    EXPECT_CALL(*this, OnInitialized());
    session_host_ = std::make_unique<OpenscreenSessionHost>(
        std::move(session_params), gfx::Size(1920, 1080),
        std::move(session_observer_remote), std::move(resource_provider_remote),
        std::move(outbound_channel_remote),
        inbound_channel_.BindNewPipeAndPassReceiver(), nullptr);
    session_host_->AsyncInitialize(MakeOnInitializedCallback());
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
  }

  // Negotiates a mirroring session.
  void StartSession() {
    ASSERT_EQ(cast_mode_, "mirroring");
    const int num_to_get_video_host =
        session_type_ == SessionType::AUDIO_ONLY ? 0 : 1;
    const int num_to_create_audio_stream =
        session_type_ == SessionType::VIDEO_ONLY ? 0 : 1;
    EXPECT_CALL(*this, OnGetVideoCaptureHost()).Times(num_to_get_video_host);
    EXPECT_CALL(*this, OnCreateAudioStream()).Times(num_to_create_audio_stream);
    EXPECT_CALL(*this, OnError(_)).Times(0);
    EXPECT_CALL(*this,
                OnOutboundMessage(SenderMessage::Type::kGetCapabilities));
    EXPECT_CALL(*this, DidStart());
    GenerateAndReplyWithAnswer();
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
  }

  void StopSession() {
    if (video_host_)
      EXPECT_CALL(*video_host_, OnStopped());
    EXPECT_CALL(*this, DidStop());
    session_host_.reset();
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
  }

  void CaptureOneVideoFrame() {
    ASSERT_EQ(cast_mode_, "mirroring");
    ASSERT_TRUE(video_host_);
    // Expect to send out some UDP packets.
    EXPECT_CALL(*network_context_->udp_socket(), OnSendTo()).Times(AtLeast(1));
    EXPECT_CALL(*video_host_, ReleaseBuffer(_, _, _));
    // Send one video frame to the consumer.
    video_host_->SendOneFrame(gfx::Size(64, 32), base::TimeTicks::Now());
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(network_context_.get());
    Mock::VerifyAndClear(video_host_.get());
  }

  void SignalAnswerTimeout() {
    EXPECT_CALL(*this, LogErrorMessage(_));
    if (cast_mode_ == "mirroring") {
      EXPECT_CALL(*this, DidStop());
      EXPECT_CALL(*this, OnError(SessionError::ANSWER_TIME_OUT));
    } else {
      EXPECT_CALL(*this, DidStop()).Times(0);
      EXPECT_CALL(*this, OnError(SessionError::ANSWER_TIME_OUT)).Times(0);
      // Expect to send OFFER message to fallback on mirroring.
      EXPECT_CALL(*this, OnOutboundMessage(SenderMessage::Type::kOffer));
      // The start of remoting is expected to fail.
      EXPECT_CALL(
          remoting_source_,
          OnStartFailed(RemotingStartFailReason::INVALID_ANSWER_MESSAGE));
      EXPECT_CALL(remoting_source_, OnSinkGone()).Times(AtLeast(1));
    }

    session_host_->OnError(session_host_->session_.get(),
                           openscreen::Error::Code::kAnswerTimeout);
    task_environment_.RunUntilIdle();
    cast_mode_ = "mirroring";
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  void SendRemotingCapabilities() {
    static const openscreen::cast::RemotingCapabilities capabilities{
        {openscreen::cast::AudioCapability::kBaselineSet,
         openscreen::cast::AudioCapability::kAac,
         openscreen::cast::AudioCapability::kOpus},
        {openscreen::cast::VideoCapability::kSupports4k,
         openscreen::cast::VideoCapability::kVp8,
         openscreen::cast::VideoCapability::kVp9,
         openscreen::cast::VideoCapability::kH264,
         openscreen::cast::VideoCapability::kHevc}};

    EXPECT_CALL(*this, OnConnectToRemotingSource());
    EXPECT_CALL(remoting_source_, OnSinkAvailable(_));

    session_host_->OnCapabilitiesDetermined(session_host_->session_.get(),
                                            capabilities);
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  void StartRemoting() {
    base::RunLoop run_loop;
    ASSERT_TRUE(remoter_.is_bound());
    // GET_CAPABILITIES is only sent once at the start of mirroring.
    EXPECT_CALL(*this, OnOutboundMessage(SenderMessage::Type::kGetCapabilities))
        .Times(0);
    EXPECT_CALL(*this, OnOutboundMessage(SenderMessage::Type::kOffer))
        .WillOnce(InvokeWithoutArgs(&run_loop, &base::RunLoop::Quit));
    remoter_->Start();
    run_loop.Run();
    task_environment_.RunUntilIdle();
    cast_mode_ = "remoting";
    Mock::VerifyAndClear(this);
  }

  void RemotingStarted() {
    ASSERT_EQ(cast_mode_, "remoting");
    EXPECT_CALL(remoting_source_, OnStarted());
    GenerateAndReplyWithAnswer();
    task_environment_.RunUntilIdle();
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  void StopRemoting() {
    ASSERT_EQ(cast_mode_, "remoting");
    const RemotingStopReason reason = RemotingStopReason::LOCAL_PLAYBACK;
    // Expect to send OFFER message to fallback on mirroring.
    EXPECT_CALL(*this, OnOutboundMessage(SenderMessage::Type::kOffer));
    EXPECT_CALL(remoting_source_, OnStopped(reason));
    remoter_->Stop(reason);
    task_environment_.RunUntilIdle();
    cast_mode_ = "mirroring";
    Mock::VerifyAndClear(this);
    Mock::VerifyAndClear(&remoting_source_);
  }

  void SetTargetPlayoutDelay(int target_playout_delay_ms) {
    target_playout_delay_ms_ = target_playout_delay_ms;
  }

  void SetAnswer(std::unique_ptr<openscreen::cast::Answer> answer) {
    answer_ = std::move(answer);
  }

  OpenscreenSessionHost& session_host() { return *session_host_; }

  const openscreen::cast::SenderMessage& last_sent_offer() const {
    EXPECT_TRUE(last_sent_offer_);
    return *last_sent_offer_;
  }

  base::test::TaskEnvironment& task_environment() { return task_environment_; }

 private:
  base::test::ScopedFeatureList feature_list_;
  base::test::TaskEnvironment task_environment_;
  const net::IPEndPoint receiver_endpoint_ =
      media::cast::test::GetFreeLocalPort();
  mojo::Receiver<mojom::ResourceProvider> resource_provider_receiver_{this};
  mojo::Receiver<mojom::SessionObserver> session_observer_receiver_{this};
  mojo::Receiver<mojom::CastMessageChannel> outbound_channel_receiver_{this};
  mojo::Remote<mojom::CastMessageChannel> inbound_channel_;
  SessionType session_type_ = SessionType::AUDIO_AND_VIDEO;
  mojo::Remote<media::mojom::Remoter> remoter_;
  NiceMock<MockRemotingSource> remoting_source_;
  std::string cast_mode_;
  int32_t target_playout_delay_ms_ = kDefaultPlayoutDelay;

  std::unique_ptr<OpenscreenSessionHost> session_host_;
  std::unique_ptr<FakeVideoCaptureHost> video_host_;
  std::unique_ptr<MockNetworkContext> network_context_;
  std::unique_ptr<openscreen::cast::Answer> answer_;

  int next_receiver_ssrc_ = 35336;
  absl::optional<openscreen::cast::SenderMessage> last_sent_offer_;
};

TEST_F(OpenscreenSessionHostTest, AudioOnlyMirroring) {
  CreateSession(SessionType::AUDIO_ONLY);
  StartSession();
  StopSession();
}

TEST_F(OpenscreenSessionHostTest, VideoOnlyMirroring) {
  SetTargetPlayoutDelay(1000);
  CreateSession(SessionType::VIDEO_ONLY);
  StartSession();
  CaptureOneVideoFrame();
  StopSession();
}

TEST_F(OpenscreenSessionHostTest, AudioAndVideoMirroring) {
  SetTargetPlayoutDelay(150);
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  StartSession();
  StopSession();
}

TEST_F(OpenscreenSessionHostTest, AnswerWithConstraints) {
  SetAnswer(std::make_unique<openscreen::cast::Answer>(kAnswerWithConstraints));
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  StartSession();
  StopSession();
}

TEST_F(OpenscreenSessionHostTest, AnswerTimeout) {
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  SignalAnswerTimeout();
}

TEST_F(OpenscreenSessionHostTest, SwitchToAndFromRemoting) {
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  StartSession();
  SendRemotingCapabilities();
  StartRemoting();
  RemotingStarted();
  StopRemoting();
  StopSession();
}

TEST_F(OpenscreenSessionHostTest, StopSessionWhileRemoting) {
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  StartSession();
  SendRemotingCapabilities();
  StartRemoting();
  RemotingStarted();
  StopSession();
}

TEST_F(OpenscreenSessionHostTest, StartRemotingFailed) {
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  StartSession();
  SendRemotingCapabilities();
  StartRemoting();
  SignalAnswerTimeout();
  GenerateAndReplyWithAnswer();
  CaptureOneVideoFrame();
  StopSession();
}

// TODO(https://crbug.com/1363017): reenable adaptive playout delay.
TEST_F(OpenscreenSessionHostTest, ChangeTargetPlayoutDelay) {
  CreateSession(SessionType::AUDIO_AND_VIDEO);
  StartSession();

  // Currently new delays are ignored due to the playout delay
  // being bounded by a min-max of (400, 400).
  session_host().SetTargetPlayoutDelay(base::Milliseconds(300));
  EXPECT_EQ(session_host().audio_stream_->GetTargetPlayoutDelay(),
            base::Milliseconds(400));
  EXPECT_EQ(session_host().audio_stream_->GetTargetPlayoutDelay(),
            base::Milliseconds(400));

  StopSession();
}

TEST_F(OpenscreenSessionHostTest, UpdateBandwidthEstimate) {
  CreateSession(SessionType::VIDEO_ONLY);
  StartSession();

  // Default bitrate should be twice the minimum.
  EXPECT_EQ(786432, session_host().GetSuggestedVideoBitrate());

  // If the estimate is below the minimum, it should stay at the minimum.
  session_host().forced_bandwidth_estimate_ = 1000;
  session_host().UpdateBandwidthEstimate();
  EXPECT_EQ(393216, session_host().GetSuggestedVideoBitrate());

  // It should go up gradually instead of all the way to the max.
  session_host().forced_bandwidth_estimate_ = 1000000;
  session_host().UpdateBandwidthEstimate();
  EXPECT_EQ(432537, session_host().GetSuggestedVideoBitrate());

  session_host().UpdateBandwidthEstimate();
  EXPECT_EQ(475790, session_host().GetSuggestedVideoBitrate());

  session_host().UpdateBandwidthEstimate();
  EXPECT_EQ(523369, session_host().GetSuggestedVideoBitrate());

  session_host().UpdateBandwidthEstimate();
  EXPECT_EQ(575705, session_host().GetSuggestedVideoBitrate());

  // Should continue to climb at a reasonable rate if the estimate goes up.
  session_host().forced_bandwidth_estimate_ = 10000000;
  session_host().UpdateBandwidthEstimate();
  EXPECT_EQ(633275, session_host().GetSuggestedVideoBitrate());

  StopSession();
}

TEST_F(OpenscreenSessionHostTest, CanRequestRefresh) {
  CreateSession(SessionType::VIDEO_ONLY);

  // We just want to make sure this doesn't result in an error or crash.
  session_host().RequestRefreshFrame();
}

TEST_F(OpenscreenSessionHostTest, Vp9CodecEnabledInOffer) {
  base::test::ScopedFeatureList feature_list(features::kCastStreamingVp9);
  CreateSession(SessionType::VIDEO_ONLY);

  const openscreen::cast::Offer& offer =
      absl::get<openscreen::cast::Offer>(last_sent_offer().body);

  // We should have offered VP9.
  EXPECT_TRUE(
      std::any_of(offer.video_streams.begin(), offer.video_streams.end(),
                  [](const openscreen::cast::VideoStream& stream) {
                    return stream.codec == openscreen::cast::VideoCodec::kVp9;
                  }));
}

TEST_F(OpenscreenSessionHostTest, Av1CodecEnabledInOffer) {
// Cast streaming of AV1 is desktop only.
#if !BUILDFLAG(IS_ANDROID)
  base::test::ScopedFeatureList feature_list(features::kCastStreamingAv1);
  CreateSession(SessionType::VIDEO_ONLY);

  const openscreen::cast::Offer& offer =
      absl::get<openscreen::cast::Offer>(last_sent_offer().body);

  // We should have offered AV1.
  EXPECT_TRUE(
      std::any_of(offer.video_streams.begin(), offer.video_streams.end(),
                  [](const openscreen::cast::VideoStream& stream) {
                    return stream.codec == openscreen::cast::VideoCodec::kAv1;
                  }));
#endif
}

TEST_F(OpenscreenSessionHostTest, ShouldEnableHardwareVp8EncodingIfSupported) {
#if !BUILDFLAG(IS_CHROMEOS)
  CreateSession(SessionType::VIDEO_ONLY);

  // Mock the profiles to enable VP8 hardware encode.
  session_host().supported_profiles_ =
      std::vector<media::VideoEncodeAccelerator::SupportedProfile>{
          media::VideoEncodeAccelerator::SupportedProfile(
              media::VideoCodecProfile::VP8PROFILE_ANY, gfx::Size{1920, 1080})};
  session_host().NegotiateMirroring();
  task_environment().RunUntilIdle();

  const openscreen::cast::Offer& offer =
      absl::get<openscreen::cast::Offer>(last_sent_offer().body);

  // We should have offered VP8.
  EXPECT_TRUE(
      std::any_of(offer.video_streams.begin(), offer.video_streams.end(),
                  [](const openscreen::cast::VideoStream& stream) {
                    return stream.codec == openscreen::cast::VideoCodec::kVp8;
                  }));

  // We should have put a video config for VP8 with hardware enabled in the last
  // offered configs.
  EXPECT_TRUE(std::any_of(session_host().last_offered_video_configs_.begin(),
                          session_host().last_offered_video_configs_.end(),

                          [](const media::cast::FrameSenderConfig& config) {
                            return config.codec ==
                                       media::cast::Codec::CODEC_VIDEO_VP8 &&
                                   config.use_external_encoder;
                          }));
#endif
}

TEST_F(OpenscreenSessionHostTest, ShouldEnableHardwareH264EncodingIfSupported) {
#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_WIN) && !BUILDFLAG(IS_CHROMEOS)
  CreateSession(SessionType::VIDEO_ONLY);

  session_host().supported_profiles_ =
      std::vector<media::VideoEncodeAccelerator::SupportedProfile>{
          media::VideoEncodeAccelerator::SupportedProfile(
              media::VideoCodecProfile::H264PROFILE_MIN,
              gfx::Size{1920, 1080})};
  session_host().NegotiateMirroring();
  task_environment().RunUntilIdle();

  const openscreen::cast::Offer& offer =
      absl::get<openscreen::cast::Offer>(last_sent_offer().body);

  // We should have offered H264.
  EXPECT_TRUE(
      std::any_of(offer.video_streams.begin(), offer.video_streams.end(),
                  [](const openscreen::cast::VideoStream& stream) {
                    return stream.codec == openscreen::cast::VideoCodec::kH264;
                  }));

  // We should have put a video config for H264 with hardware enabled in the
  // last offered configs.
  EXPECT_TRUE(std::any_of(session_host().last_offered_video_configs_.begin(),
                          session_host().last_offered_video_configs_.end(),

                          [](const media::cast::FrameSenderConfig& config) {
                            return config.codec ==
                                       media::cast::Codec::CODEC_VIDEO_H264 &&
                                   config.use_external_encoder;
                          }));
#endif
}

}  // namespace mirroring
