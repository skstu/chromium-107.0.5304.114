// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/page_load_metrics/browser/observers/use_counter_page_load_metrics_observer.h"

#include "base/no_destructor.h"

// This file defines a list of UseCounter WebFeature measured in the
// UKM-based UseCounter. Features must all satisfy UKM privacy requirements
// (see go/ukm). In addition, features should only be added if it's shown to be
// (or highly likely to be) rare, e.g. <1% of page views as measured by UMA.
//
// UKM-based UseCounter should be used to cover the case when UMA UseCounter
// data shows a behaviour that is rare but too common to blindly change.
// UKM-based UseCounter would allow us to find specific pages to reason about
// whether a breaking change is acceptable or not.
//
// This event is emitted for every page load and is not sub-sampled by the UKM
// system. The WebFeature::kPageVisits is always present in the event, so
// it is valid to take the ratio of events with your feature to kPageVisit to
// estimate "percentage of page views using a feature". Note that the lack of
// sub-sampling is the reason why this event must only be used for rare
// features.

using WebFeature = blink::mojom::WebFeature;

// UKM-based UseCounter features (WebFeature) should be defined in
// opt_in_features list.
const UseCounterPageLoadMetricsObserver::UkmFeatureList&
UseCounterPageLoadMetricsObserver::GetAllowedUkmFeatures() {
  static base::NoDestructor<UseCounterPageLoadMetricsObserver::UkmFeatureList>
      // We explicitly use an std::initializer_list below to work around GCC
      // bug 84849, which causes having a base::NoDestructor<T<U>> and passing
      // an initializer list of Us does not work.
      opt_in_features(std::initializer_list<WebFeature>({
          WebFeature::kNavigatorVibrate,
          WebFeature::kNavigatorVibrateSubFrame,
          WebFeature::kTouchEventPreventedNoTouchAction,
          WebFeature::kTouchEventPreventedForcedDocumentPassiveNoTouchAction,
          WebFeature::kMixedContentAudio,
          WebFeature::kMixedContentImage,
          WebFeature::kMixedContentVideo,
          WebFeature::kMixedContentPlugin,
          WebFeature::kOpenerNavigationWithoutGesture,
          WebFeature::kUsbRequestDevice,
          WebFeature::kXMLHttpRequestSynchronousInMainFrame,
          WebFeature::kXMLHttpRequestSynchronousInCrossOriginSubframe,
          WebFeature::kXMLHttpRequestSynchronousInSameOriginSubframe,
          WebFeature::kXMLHttpRequestSynchronousInWorker,
          WebFeature::kPaymentHandler,
          WebFeature::kPaymentRequestShowWithoutGesture,
          WebFeature::kPaymentRequestShowWithoutGestureOrToken,
          WebFeature::kCredentialManagerCreatePublicKeyCredential,
          WebFeature::kCredentialManagerGetPublicKeyCredential,
          WebFeature::kCredentialManagerMakePublicKeyCredentialSuccess,
          WebFeature::kCredentialManagerGetPublicKeyCredentialSuccess,
          WebFeature::kU2FCryptotokenRegister,
          WebFeature::kU2FCryptotokenSign,
          WebFeature::kTextToSpeech_Speak,
          WebFeature::kTextToSpeech_SpeakDisallowedByAutoplay,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetTop,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetLeft,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetRight,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetBottom,
          WebFeature::kMediaControlsDisplayCutoutGesture,
          WebFeature::kPolymerV1Detected,
          WebFeature::kPolymerV2Detected,
          WebFeature::kFullscreenSecureOrigin,
          WebFeature::kFullscreenInsecureOrigin,
          WebFeature::kPrefixedVideoEnterFullscreen,
          WebFeature::kPrefixedVideoExitFullscreen,
          WebFeature::kPrefixedVideoEnterFullScreen,
          WebFeature::kPrefixedVideoExitFullScreen,
          WebFeature::kDocumentLevelPassiveDefaultEventListenerPreventedWheel,
          WebFeature::kDocumentDomainBlockedCrossOriginAccess,
          WebFeature::kDocumentDomainEnabledCrossOriginAccess,
          WebFeature::kCursorImageGT32x32,
          WebFeature::kCursorImageLE32x32,
          WebFeature::kCursorImageGT64x64,
          WebFeature::kAdClick,
          WebFeature::kUpdateWithoutShippingOptionOnShippingAddressChange,
          WebFeature::kUpdateWithoutShippingOptionOnShippingOptionChange,
          WebFeature::kSignedExchangeInnerResponseInMainFrame,
          WebFeature::kSignedExchangeInnerResponseInSubFrame,
          WebFeature::kWebShareShare,
          WebFeature::kDownloadInAdFrameWithoutUserGesture,
          WebFeature::kOpenWebDatabase,
          WebFeature::kV8MediaCapabilities_DecodingInfo_Method,
          WebFeature::kOpenerNavigationDownloadCrossOrigin,
          WebFeature::kLinkRelPrerender,
          WebFeature::kAdClickNavigation,
          WebFeature::kV8HTMLVideoElement_RequestPictureInPicture_Method,
          WebFeature::kMediaCapabilitiesDecodingInfoWithKeySystemConfig,
          WebFeature::kTextFragmentAnchor,
          WebFeature::kTextFragmentAnchorMatchFound,
          WebFeature::kCookieInsecureAndSameSiteNone,
          WebFeature::kCookieStoreAPI,
          WebFeature::kDeviceOrientationSecureOrigin,
          WebFeature::kDeviceOrientationAbsoluteSecureOrigin,
          WebFeature::kDeviceMotionSecureOrigin,
          WebFeature::kRelativeOrientationSensorConstructor,
          WebFeature::kAbsoluteOrientationSensorConstructor,
          WebFeature::kLinearAccelerationSensorConstructor,
          WebFeature::kAccelerometerConstructor,
          WebFeature::kGyroscopeConstructor,
          WebFeature::kServiceWorkerInterceptedRequestFromOriginDirtyStyleSheet,
          WebFeature::kDownloadPrePolicyCheck,
          WebFeature::kDownloadPostPolicyCheck,
          WebFeature::kDownloadInAdFrame,
          WebFeature::kDownloadInSandbox,
          WebFeature::kDownloadWithoutUserGesture,
          WebFeature::kLazyLoadFrameLoadingAttributeLazy,
          WebFeature::kLazyLoadFrameLoadingAttributeEager,
          WebFeature::kLazyLoadImageLoadingAttributeLazy,
          WebFeature::kLazyLoadImageLoadingAttributeEager,
          WebFeature::kWebOTP,
          WebFeature::kBaseWithCrossOriginHref,
          WebFeature::kWakeLockAcquireScreenLock,
          WebFeature::kWakeLockAcquireSystemLock,
          WebFeature::kThirdPartyServiceWorker,
          WebFeature::kThirdPartySharedWorker,
          WebFeature::kThirdPartyBroadcastChannel,
          WebFeature::kHeavyAdIntervention,
          WebFeature::kGetGamepadsFromCrossOriginSubframe,
          WebFeature::kGetGamepadsFromInsecureContext,
          WebFeature::kGetGamepads,
          WebFeature::kMovedOrResizedPopup,
          WebFeature::kMovedOrResizedPopup2sAfterCreation,
          WebFeature::kDOMWindowOpenPositioningFeatures,
          WebFeature::kCSSSelectorInternalMediaControlsOverlayCastButton,
          WebFeature::kWebBluetoothRequestDevice,
          WebFeature::kWebBluetoothRequestScan,
          WebFeature::
              kV8VideoPlaybackQuality_CorruptedVideoFrames_AttributeGetter,
          WebFeature::kV8MediaSession_Metadata_AttributeSetter,
          WebFeature::kV8MediaSession_SetActionHandler_Method,
          WebFeature::kLargeStickyAd,
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTopSetPositive,
          WebFeature::kThirdPartyFileSystem,
          WebFeature::kThirdPartyIndexedDb,
          WebFeature::kThirdPartyCacheStorage,
          WebFeature::kOverlayPopup,
          WebFeature::kOverlayPopupAd,
          WebFeature::kTrustTokenXhr,
          WebFeature::kTrustTokenFetch,
          WebFeature::kTrustTokenIframe,
          WebFeature::kV8Document_HasTrustToken_Method,
          WebFeature::kV8HTMLVideoElement_RequestVideoFrameCallback_Method,
          WebFeature::kV8HTMLVideoElement_CancelVideoFrameCallback_Method,
          WebFeature::kSchemefulSameSiteContextDowngrade,
          WebFeature::kIdleDetectionStart,
          WebFeature::kPerformanceObserverEntryTypesAndBuffered,
          WebFeature::kStorageAccessAPI_HasStorageAccess_Method,
          WebFeature::kStorageAccessAPI_requestStorageAccess_Method,
          WebFeature::kThirdPartyCookieRead,
          WebFeature::kThirdPartyCookieWrite,
          WebFeature::kCrossSitePostMessage,
          WebFeature::kSchemelesslySameSitePostMessage,
          WebFeature::kSchemelesslySameSitePostMessageSecureToInsecure,
          WebFeature::kSchemelesslySameSitePostMessageInsecureToSecure,
          WebFeature::kAddressSpacePrivateSecureContextEmbeddedLocal,
          WebFeature::kAddressSpacePrivateNonSecureContextEmbeddedLocal,
          WebFeature::kAddressSpacePublicSecureContextEmbeddedLocal,
          WebFeature::kAddressSpacePublicNonSecureContextEmbeddedLocal,
          WebFeature::kAddressSpacePublicSecureContextEmbeddedPrivate,
          WebFeature::kAddressSpacePublicNonSecureContextEmbeddedPrivate,
          WebFeature::kAddressSpaceUnknownSecureContextEmbeddedLocal,
          WebFeature::kAddressSpaceUnknownNonSecureContextEmbeddedLocal,
          WebFeature::kAddressSpaceUnknownSecureContextEmbeddedPrivate,
          WebFeature::kAddressSpaceUnknownNonSecureContextEmbeddedPrivate,
          WebFeature::kAddressSpacePrivateSecureContextNavigatedToLocal,
          WebFeature::kAddressSpacePrivateNonSecureContextNavigatedToLocal,
          WebFeature::kAddressSpacePublicSecureContextNavigatedToLocal,
          WebFeature::kAddressSpacePublicNonSecureContextNavigatedToLocal,
          WebFeature::kAddressSpacePublicSecureContextNavigatedToPrivate,
          WebFeature::kAddressSpacePublicNonSecureContextNavigatedToPrivate,
          WebFeature::kAddressSpaceUnknownSecureContextNavigatedToLocal,
          WebFeature::kAddressSpaceUnknownNonSecureContextNavigatedToLocal,
          WebFeature::kAddressSpaceUnknownSecureContextNavigatedToPrivate,
          WebFeature::kAddressSpaceUnknownNonSecureContextNavigatedToPrivate,
          WebFeature::kFileSystemPickerMethod,
          WebFeature::kV8Window_ShowOpenFilePicker_Method,
          WebFeature::kV8Window_ShowSaveFilePicker_Method,
          WebFeature::kV8Window_ShowDirectoryPicker_Method,
          WebFeature::kV8StorageManager_GetDirectory_Method,
          WebFeature::kBarcodeDetectorDetect,
          WebFeature::kFaceDetectorDetect,
          WebFeature::kTextDetectorDetect,
          WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation,
          WebFeature::kV8HTMLVideoElement_GetVideoPlaybackQuality_Method,
          WebFeature::kOBSOLETE_RTCPeerConnectionConstructedWithPlanB,
          WebFeature::kOBSOLETE_RTCPeerConnectionConstructedWithUnifiedPlan,
          WebFeature::kOBSOLETE_RTCPeerConnectionUsingComplexPlanB,
          WebFeature::kOBSOLETE_RTCPeerConnectionUsingComplexUnifiedPlan,
          WebFeature::kWebPImage,
          WebFeature::kAVIFImage,
          WebFeature::kGetDisplayMedia,
          WebFeature::kLaxAllowingUnsafeCookies,
          WebFeature::kOversrollBehaviorOnViewportBreaks,
          WebFeature::kPaymentRequestCSPViolation,
          WebFeature::kRequestedFileSystemPersistentThirdPartyContext,
          WebFeature::kPrefixedStorageInfoThirdPartyContext,
          WebFeature::
              kCrossBrowsingContextGroupMainFrameNulledNonEmptyNameAccessed,
          WebFeature::kControlledWorkerWillBeUncontrolled,
          WebFeature::kUsbDeviceOpen,
          WebFeature::kWebBluetoothRemoteServerConnect,
          WebFeature::kSerialRequestPort,
          WebFeature::kSerialPortOpen,
          WebFeature::kHidRequestDevice,
          WebFeature::kHidDeviceOpen,
          WebFeature::kControlledNonBlobURLWorkerWillBeUncontrolled,
          WebFeature::kSameSiteCookieInclusionChangedByCrossSiteRedirect,
          WebFeature::
              kBlobStoreAccessAcrossAgentClustersInResolveAsURLLoaderFactory,
          WebFeature::kBlobStoreAccessAcrossAgentClustersInResolveForNavigation,
          WebFeature::kSearchEventFired,
          WebFeature::kReadOrWriteWebDatabase,
          WebFeature::kExternalProtocolBlockedBySandbox,
          WebFeature::kWebCodecsAudioDecoder,
          WebFeature::kWebCodecsVideoDecoder,
          WebFeature::kWebCodecsVideoEncoder,
          WebFeature::kWebCodecsVideoTrackReader,
          WebFeature::kWebCodecsImageDecoder,
          WebFeature::kWebCodecsAudioEncoder,
          WebFeature::kWebCodecsVideoFrameFromImage,
          WebFeature::kWebCodecsVideoFrameFromBuffer,
          WebFeature::kOpenWebDatabaseInsecureContext,
          WebFeature::kPrivateNetworkAccessIgnoredPreflightError,
          WebFeature::kWebBluetoothGetAvailability,
          WebFeature::kCookieHasNotBeenRefreshedIn201To300Days,
          WebFeature::kCookieHasNotBeenRefreshedIn301To350Days,
          WebFeature::kCookieHasNotBeenRefreshedIn351To400Days,
          WebFeature::kPartitionedCookies,
          WebFeature::kScriptSchedulingType_Defer,
          WebFeature::kScriptSchedulingType_ParserBlocking,
          WebFeature::kScriptSchedulingType_ParserBlockingInline,
          WebFeature::kScriptSchedulingType_InOrder,
          WebFeature::kScriptSchedulingType_Async,
          WebFeature::kClientHintsMetaHTTPEquivAcceptCH,
          WebFeature::kAutomaticLazyAds,
          WebFeature::kAutomaticLazyEmbeds,
          WebFeature::kCookieDomainNonASCII,
          WebFeature::kClientHintsMetaEquivDelegateCH,
          WebFeature::kAuthorizationCoveredByWildcard,
          WebFeature::kImageAd,
          WebFeature::kLinkRelPrefetchAsDocumentSameOrigin,
          WebFeature::kLinkRelPrefetchAsDocumentCrossOrigin,
          WebFeature::kChromeLoadTimesCommitLoadTime,
          WebFeature::kChromeLoadTimesConnectionInfo,
          WebFeature::kChromeLoadTimesFinishDocumentLoadTime,
          WebFeature::kChromeLoadTimesFinishLoadTime,
          WebFeature::kChromeLoadTimesFirstPaintAfterLoadTime,
          WebFeature::kChromeLoadTimesFirstPaintTime,
          WebFeature::kChromeLoadTimesNavigationType,
          WebFeature::kChromeLoadTimesNpnNegotiatedProtocol,
          WebFeature::kChromeLoadTimesRequestTime,
          WebFeature::kChromeLoadTimesStartLoadTime,
          WebFeature::kChromeLoadTimesUnknown,
          WebFeature::kChromeLoadTimesWasAlternateProtocolAvailable,
          WebFeature::kChromeLoadTimesWasFetchedViaSpdy,
          WebFeature::kChromeLoadTimesWasNpnNegotiated,
      }));
  return *opt_in_features;
}
