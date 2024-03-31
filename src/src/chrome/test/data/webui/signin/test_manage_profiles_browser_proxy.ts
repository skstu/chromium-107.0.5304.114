// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {AutogeneratedThemeColorInfo, ManageProfilesBrowserProxy, ProfileState, UserThemeChoice} from 'chrome://profile-picker/profile_picker.js';
import {TestBrowserProxy} from 'chrome://webui-test/test_browser_proxy.js';

export class TestManageProfilesBrowserProxy extends TestBrowserProxy implements
    ManageProfilesBrowserProxy {
  profileThemeInfo: AutogeneratedThemeColorInfo;
  profileSample: ProfileState;
  private getSwitchProfilePromise_: Promise<ProfileState>|null;

  constructor() {
    super([
      'initializeMainView', 'launchGuestProfile',
      'openManageProfileSettingsSubPage', 'launchSelectedProfile',
      'askOnStartupChanged', 'getNewProfileSuggestedThemeInfo',
      'getProfileThemeInfo', 'removeProfile', 'getProfileStatistics',
      'selectNewAccount', 'createProfile',
      'createProfileAndOpenCustomizationDialog', 'setProfileName',
      'recordSignInPromoImpression', 'getAvailableIcons', 'getSwitchProfile',
      'confirmProfileSwitch', 'cancelProfileSwitch',
      // <if expr="chromeos_lacros">
      'getAvailableAccounts', 'openAshAccountSettingsPage',
      'selectExistingAccountLacros',
      // </if>
    ]);

    this.profileThemeInfo = {
      colorId: 22,
      color: -10799479,
      themeFrameColor: 'rgb(70, 42, 104)',
      themeShapeColor: 'rgb(109, 65, 161)',
      themeFrameTextColor: 'rgb(255, 255, 255)',
      themeGenericAvatar: 'AvatarUrl-22',
    };

    this.profileSample = {
      profilePath: 'profile1',
      localProfileName: 'Work',
      needsSignin: false,
      isSyncing: true,
      gaiaName: 'Alice',
      userName: 'Alice@gmail.com',
      isManaged: false,
      avatarIcon: 'url',
      // <if expr="chromeos_lacros">
      isPrimaryLacrosProfile: false,
      // </if>
    };

    /**
     * The promise to return from `getSwitchProfile()`.
     */
    this.getSwitchProfilePromise_ = null;
  }

  setProfileThemeInfo(profileThemeInfo: AutogeneratedThemeColorInfo) {
    this.profileThemeInfo = profileThemeInfo;
  }

  setGetSwitchProfilePromise(promise: Promise<ProfileState>) {
    this.getSwitchProfilePromise_ = promise;
  }

  initializeMainView() {
    this.methodCalled('initializeMainView');
  }

  launchGuestProfile() {
    this.methodCalled('launchGuestProfile');
  }

  openManageProfileSettingsSubPage(profilePath: string) {
    this.methodCalled('openManageProfileSettingsSubPage', profilePath);
  }

  launchSelectedProfile(profilePath: string) {
    this.methodCalled('launchSelectedProfile', profilePath);
  }

  askOnStartupChanged(shouldShow: boolean) {
    this.methodCalled('askOnStartupChanged', shouldShow);
  }

  getNewProfileSuggestedThemeInfo() {
    this.methodCalled('getNewProfileSuggestedThemeInfo');
    return Promise.resolve(this.profileThemeInfo);
  }

  getProfileThemeInfo(theme: UserThemeChoice) {
    this.methodCalled('getProfileThemeInfo', theme);
    return Promise.resolve(this.profileThemeInfo);
  }

  removeProfile(profilePath: string) {
    this.methodCalled('removeProfile', profilePath);
  }

  getProfileStatistics(profilePath: string) {
    this.methodCalled('getProfileStatistics', profilePath);
  }

  selectNewAccount(profileColor: number|null) {
    this.methodCalled('selectNewAccount', [profileColor]);
  }

  createProfile(
      profileName: string, profileColor: number, avatarIndex: number,
      createShortcut: boolean) {
    this.methodCalled(
        'createProfile',
        [profileName, profileColor, avatarIndex, createShortcut]);
  }

  createProfileAndOpenCustomizationDialog(profileColor: number) {
    this.methodCalled(
        'createProfileAndOpenCustomizationDialog', [profileColor]);
  }

  setProfileName(profilePath: string, profileName: string) {
    this.methodCalled('setProfileName', [profilePath, profileName]);
  }

  recordSignInPromoImpression() {
    this.methodCalled('recordSignInPromoImpression');
  }

  getAvailableIcons() {
    this.methodCalled('getAvailableIcons');
    return Promise.resolve([
      {
        url: 'fake-icon-1.png',
        label: 'fake-icon-1',
        index: 1,
        selected: false,
        isGaiaAvatar: false,
      },
      {
        url: 'fake-icon-2.png',
        label: 'fake-icon-2',
        index: 2,
        selected: false,
        isGaiaAvatar: false,
      },
      {
        url: 'fake-icon-3.png',
        label: 'fake-icon-3',
        index: 3,
        selected: false,
        isGaiaAvatar: false,
      },
    ]);
  }

  getSwitchProfile() {
    this.methodCalled('getSwitchProfile');
    return this.getSwitchProfilePromise_ || Promise.resolve(this.profileSample);
  }

  confirmProfileSwitch(profilePath: string) {
    this.methodCalled('confirmProfileSwitch', [profilePath]);
  }

  cancelProfileSwitch() {
    this.methodCalled('cancelProfileSwitch');
  }

  // <if expr="chromeos_lacros">
  getAvailableAccounts() {
    this.methodCalled('getAvailableAccounts');
  }

  openAshAccountSettingsPage() {
    this.methodCalled('openAshAccountSettingsPage');
  }

  selectExistingAccountLacros(profileColor: number|null, gaiaId: string) {
    this.methodCalled('selectExistingAccountLacros', [profileColor, gaiaId]);
  }
  // </if>
}