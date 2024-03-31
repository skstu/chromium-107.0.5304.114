// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/lens/lens_url_utils.h"

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "components/lens/lens_entrypoints.h"
#include "components/lens/lens_rendering_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::MatchesRegex;

namespace lens {

TEST(LensUrlUtilsTest, GetRegionSearchNewTabQueryParameterTest) {
  lens::EntryPoint lens_region_search_ep =
      lens::EntryPoint::CHROME_REGION_SEARCH_MENU_ITEM;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_region_search_ep, /* is_side_panel_request= */ false,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("ep=crs&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetImageSearchNewTabQueryParameterTest) {
  lens::EntryPoint lens_image_search_ep =
      lens::EntryPoint::CHROME_SEARCH_WITH_GOOGLE_LENS_CONTEXT_MENU_ITEM;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_image_search_ep, /* is_side_panel_request= */ false,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("ep=ccm&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetRegionSearchSidePanelQueryParameterTest) {
  lens::EntryPoint lens_region_search_ep =
      lens::EntryPoint::CHROME_REGION_SEARCH_MENU_ITEM;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_region_search_ep, /* is_side_panel_request= */ true,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("ep=crs&re=dcsp&s=csp&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetImageSearchSidePanelQueryParameterTest) {
  lens::EntryPoint lens_image_search_ep =
      lens::EntryPoint::CHROME_SEARCH_WITH_GOOGLE_LENS_CONTEXT_MENU_ITEM;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_image_search_ep, /* is_side_panel_request= */ true,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("ep=ccm&re=dcsp&s=csp&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetOpenNewTabSidePanelParameterTest) {
  lens::EntryPoint lens_open_new_tab_side_panel_ep =
      lens::EntryPoint::CHROME_OPEN_NEW_TAB_SIDE_PANEL;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_open_new_tab_side_panel_ep, /* is_side_panel_request= */ false,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("ep=cnts&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetFullscreenSearchQueryParameterTest) {
  lens::EntryPoint lens_ep =
      lens::EntryPoint::CHROME_FULLSCREEN_SEARCH_MENU_ITEM;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_ep, /* is_side_panel_request= */ false,
      /* is_full_screen_region_search_request= */ true);
  EXPECT_THAT(query_param, MatchesRegex("ep=cfs&re=avsf&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetScreenshotSearchQueryParameterTest) {
  lens::EntryPoint lens_ep = lens::EntryPoint::CHROME_SCREENSHOT_SEARCH;
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens_ep, /* is_side_panel_request= */ false,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("ep=css&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetUnknownEntryPointTest) {
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens::EntryPoint::UNKNOWN, /* is_side_panel_request= */ false,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, GetUnknownEntryPointSidePanelTest) {
  std::string query_param = lens::GetQueryParametersForLensRequest(
      lens::EntryPoint::UNKNOWN, /* is_side_panel_request= */ true,
      /* is_full_screen_region_search_request= */ false);
  EXPECT_THAT(query_param, MatchesRegex("re=dcsp&s=csp&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendRegionSearchNewTabQueryParameterTest) {
  lens::EntryPoint lens_region_search_ep =
      lens::EntryPoint::CHROME_REGION_SEARCH_MENU_ITEM;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_FULLSCREEN;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_region_search_ep, re,
      /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("ep=crs&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendImageSearchNewTabQueryParameterTest) {
  lens::EntryPoint lens_image_search_ep =
      lens::EntryPoint::CHROME_SEARCH_WITH_GOOGLE_LENS_CONTEXT_MENU_ITEM;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_FULLSCREEN;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_image_search_ep, re,
      /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("ep=ccm&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendRegionSearchSidePanelQueryParameterTest) {
  lens::EntryPoint lens_region_search_ep =
      lens::EntryPoint::CHROME_REGION_SEARCH_MENU_ITEM;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_CHROME_SIDE_PANEL;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_region_search_ep, re,
      /* is_side_panel_request= */ true);
  EXPECT_THAT(url.query(), MatchesRegex("ep=crs&re=dcsp&s=csp&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendImageSearchSidePanelQueryParameterTest) {
  lens::EntryPoint lens_image_search_ep =
      lens::EntryPoint::CHROME_SEARCH_WITH_GOOGLE_LENS_CONTEXT_MENU_ITEM;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_CHROME_SIDE_PANEL;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_image_search_ep, re,
      /* is_side_panel_request= */ true);
  EXPECT_THAT(url.query(), MatchesRegex("ep=ccm&re=dcsp&s=csp&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendOpenNewTabSidePanelParameterTest) {
  lens::EntryPoint lens_open_new_tab_side_panel_ep =
      lens::EntryPoint::CHROME_OPEN_NEW_TAB_SIDE_PANEL;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_FULLSCREEN;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_open_new_tab_side_panel_ep, re,
      /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("ep=cnts&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendFullscreenSearchQueryParameterTest) {
  lens::EntryPoint lens_ep =
      lens::EntryPoint::CHROME_FULLSCREEN_SEARCH_MENU_ITEM;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_AMBIENT_VISUAL_SEARCH_WEB_FULLSCREEN;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_ep, re, /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("ep=cfs&re=avsf&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendScreenshotSearchQueryParameterTest) {
  lens::EntryPoint lens_ep = lens::EntryPoint::CHROME_SCREENSHOT_SEARCH;
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_FULLSCREEN;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens_ep, re, /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("ep=css&re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendUnknownEntryPointTest) {
  lens::RenderingEnvironment re =
      lens::RenderingEnvironment::ONELENS_DESKTOP_WEB_FULLSCREEN;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, lens::EntryPoint::UNKNOWN, re,
      /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("re=df&s=&st=\\d+"));
}

TEST(LensUrlUtilsTest, AppendUnknownRenderingEnvironmentTest) {
  lens::EntryPoint ep = lens::EntryPoint::CHROME_REGION_SEARCH_MENU_ITEM;
  GURL original_url = GURL("https://lens.google.com/");
  GURL url = lens::AppendOrReplaceQueryParametersForLensRequest(
      original_url, ep, lens::RenderingEnvironment::RENDERING_ENV_UNKNOWN,
      /* is_side_panel_request= */ false);
  EXPECT_THAT(url.query(), MatchesRegex("ep=crs&s=&st=\\d+"));
}

}  // namespace lens