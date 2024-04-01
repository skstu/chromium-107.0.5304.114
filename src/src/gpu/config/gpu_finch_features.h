// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines all the public base::FeatureList features for the gpu
// module.

#ifndef GPU_CONFIG_GPU_FINCH_FEATURES_H_
#define GPU_CONFIG_GPU_FINCH_FEATURES_H_

#include "base/feature_list.h"
#include "build/build_config.h"
#include "gpu/gpu_export.h"

namespace features {

GPU_EXPORT extern const base::Feature kUseGles2ForOopR;

// All features in alphabetical order. The features should be documented
// alongside the definition of their values in the .cc file.
#if BUILDFLAG(IS_ANDROID)
GPU_EXPORT extern const base::Feature kAndroidSurfaceControl;
GPU_EXPORT extern const base::Feature kWebViewSurfaceControl;
GPU_EXPORT extern const base::Feature kAImageReader;
GPU_EXPORT extern const base::Feature kWebViewVulkan;
GPU_EXPORT extern const base::Feature kLimitAImageReaderMaxSizeToOne;
GPU_EXPORT extern const base::Feature kWebViewThreadSafeMediaDefault;
GPU_EXPORT extern const base::Feature kIncreaseBufferCountForHighFrameRate;
#endif  // BUILDFLAG(IS_ANDROID)

GPU_EXPORT extern const base::Feature kDefaultEnableGpuRasterization;

GPU_EXPORT extern const base::Feature kCanvasOopRasterization;

GPU_EXPORT extern const base::Feature kEnableMSAAOnNewIntelGPUs;

GPU_EXPORT extern const base::Feature kDefaultEnableANGLEValidation;

GPU_EXPORT extern const base::Feature kCanvasContextLostInBackground;

#if BUILDFLAG(IS_WIN)
GPU_EXPORT extern const base::Feature kGpuProcessHighPriorityWin;

GPU_EXPORT extern const base::Feature kDisableVideoOverlayIfMoving;
#endif

#if BUILDFLAG(IS_MAC)
GPU_EXPORT extern const base::Feature kMetal;

#if defined(ARCH_CPU_ARM64)
GPU_EXPORT extern const base::Feature kDisableFlushWorkaroundForMacCrash;
#endif
#endif

GPU_EXPORT extern const base::Feature kSharedImageManager;

GPU_EXPORT extern const base::Feature kVaapiJpegImageDecodeAcceleration;

GPU_EXPORT extern const base::Feature kVaapiWebPImageDecodeAcceleration;

GPU_EXPORT extern const base::Feature kVulkan;

GPU_EXPORT extern const base::Feature kSkiaDawn;

GPU_EXPORT extern const base::Feature kEnableGrShaderCacheForVulkan;

GPU_EXPORT extern const base::Feature kEnableWatchdogReportOnlyModeOnGpuInit;

GPU_EXPORT extern const base::Feature kEnableVkPipelineCache;

GPU_EXPORT extern const base::Feature kReduceOpsTaskSplitting;

GPU_EXPORT extern const base::Feature kNoDiscardableMemoryForGpuDecodePath;

GPU_EXPORT extern const base::Feature kEnableDrDc;

GPU_EXPORT extern const base::Feature kForceGpuMainThreadToNormalPriorityDrDc;

#if BUILDFLAG(IS_ANDROID)
// This flag is use additionally with kEnableDrDc to enable the feature for
// vulkan enabled android devices.
GPU_EXPORT extern const base::Feature kEnableDrDcVulkan;
#endif  // BUILDFLAG(IS_ANDROID)

GPU_EXPORT extern const base::Feature kWebGPUService;

GPU_EXPORT extern const base::Feature kIncreasedCmdBufferParseSlice;

GPU_EXPORT bool UseGles2ForOopR();
GPU_EXPORT bool IsUsingVulkan();
GPU_EXPORT bool IsDrDcEnabled();
GPU_EXPORT bool IsGpuMainThreadForcedToNormalPriorityDrDc();
GPU_EXPORT bool NeedThreadSafeAndroidMedia();
GPU_EXPORT bool IsANGLEValidationEnabled();

#if BUILDFLAG(IS_ANDROID)
GPU_EXPORT bool IsAImageReaderEnabled();
GPU_EXPORT bool IsAndroidSurfaceControlEnabled();
GPU_EXPORT bool LimitAImageReaderMaxSizeToOne();
GPU_EXPORT bool IncreaseBufferCountForHighFrameRate();
GPU_EXPORT bool IncreaseBufferCountForWebViewOverlays();
#endif

}  // namespace features

#endif  // GPU_CONFIG_GPU_FINCH_FEATURES_H_