// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/speech/tts_lacros.h"

#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/speech/tts_client_lacros.h"
#include "chrome/browser/speech/tts_crosapi_util.h"

namespace {
bool g_enable_for_test = false;
}

// static
TtsPlatformImplLacros* TtsPlatformImplLacros::GetInstance() {
  static base::NoDestructor<TtsPlatformImplLacros> tts_platform;
  return tts_platform.get();
}

// static
void TtsPlatformImplLacros::EnablePlatformSupportForTesting() {
  g_enable_for_test = true;
}

TtsPlatformImplLacros::TtsPlatformImplLacros() {
  if (PlatformImplSupported())
    profile_manager_observation_.Observe(g_browser_process->profile_manager());
}

TtsPlatformImplLacros::~TtsPlatformImplLacros() = default;

void TtsPlatformImplLacros::OnProfileAdded(Profile* profile) {
  // Create TtsClientLacros for |profile|.
  TtsClientLacros::GetForBrowserContext(profile);
}

void TtsPlatformImplLacros::OnProfileManagerDestroying() {
  if (PlatformImplSupported())
    profile_manager_observation_.Reset();
}

bool TtsPlatformImplLacros::PlatformImplSupported() {
  return tts_crosapi_util::ShouldEnableLacrosTtsSupport() || g_enable_for_test;
}

bool TtsPlatformImplLacros::PlatformImplInitialized() {
  return true;
}

void TtsPlatformImplLacros::GetVoicesForBrowserContext(
    content::BrowserContext* browser_context,
    const GURL& source_url,
    std::vector<content::VoiceData>* out_voices) {
  TtsClientLacros::GetForBrowserContext(browser_context)
      ->GetAllVoices(out_voices);
}

std::string TtsPlatformImplLacros::GetError() {
  return "";
}

bool TtsPlatformImplLacros::StopSpeaking() {
  return false;
}

bool TtsPlatformImplLacros::IsSpeaking() {
  return false;
}

void TtsPlatformImplLacros::FinalizeVoiceOrdering(
    std::vector<content::VoiceData>& voices) {}