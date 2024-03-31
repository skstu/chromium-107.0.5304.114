// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Route} from '../router.js';

/**
 * Specifies all possible os routes in settings.
 *
 * @typedef {{
 *   A11Y_AUDIO_AND_CAPTIONS: !Route,
 *   A11Y_CURSOR_AND_TOUCHPAD: !Route,
 *   A11Y_DISPLAY_AND_MAGNIFICATION: !Route,
 *   A11Y_KEYBOARD_AND_TEXT_INPUT: !Route,
 *   A11Y_TEXT_TO_SPEECH: !Route,
 *   ABOUT: !Route,
 *   ABOUT_ABOUT: !Route,
 *   ACCOUNTS: !Route,
 *   ACCOUNT_MANAGER: !Route,
 *   ADVANCED: !Route,
 *   APP_NOTIFICATIONS: !Route,
 *   APP_MANAGEMENT: !Route,
 *   APP_MANAGEMENT_DETAIL: !Route,
 *   APP_MANAGEMENT_PLUGIN_VM_SHARED_PATHS: !Route,
 *   APP_MANAGEMENT_PLUGIN_VM_SHARED_USB_DEVICES: !Route,
 *   APPS: !Route,
 *   ANDROID_APPS_DETAILS: !Route,
 *   ANDROID_APPS_DETAILS_ARC_VM_SHARED_USB_DEVICES: !Route,
 *   AUDIO: !Route,
 *   CROSTINI: !Route,
 *   CROSTINI_ANDROID_ADB: !Route,
 *   CROSTINI_DETAILS: !Route,
 *   CROSTINI_DISK_RESIZE: !Route,
 *   CROSTINI_EXPORT_IMPORT: !Route,
 *   CROSTINI_EXTRA_CONTAINERS: !Route,
 *   CROSTINI_PORT_FORWARDING: !Route,
 *   CROSTINI_SHARED_PATHS: !Route,
 *   CROSTINI_SHARED_USB_DEVICES: !Route,
 *   BASIC: !Route,
 *   BLUETOOTH: !Route,
 *   BLUETOOTH_DEVICES: !Route,
 *   BLUETOOTH_DEVICE_DETAIL: !Route,
 *   BLUETOOTH_SAVED_DEVICES: !Route,
 *   BRUSCHETTA_DETAILS: !Route,
 *   BRUSCHETTA_SHARED_USB_DEVICES: !Route,
 *   CHANGE_PICTURE: !Route,
 *   CUPS_PRINTERS: !Route,
 *   DARK_MODE: !Route,
 *   DATETIME: !Route,
 *   DATETIME_TIMEZONE_SUBPAGE: !Route,
 *   DETAILED_BUILD_INFO: !Route,
 *   DEVICE: !Route,
 *   DISPLAY: !Route,
 *   EXTERNAL_STORAGE_PREFERENCES: !Route,
 *   FINGERPRINT: !Route,
 *   FILES: !Route,
 *   GOOGLE_ASSISTANT: !Route,
 *   INTERNET: !Route,
 *   INTERNET_NETWORKS: !Route,
 *   KERBEROS: !Route,
 *   KERBEROS_ACCOUNTS_V2: !Route,
 *   KEYBOARD: !Route,
 *   KNOWN_NETWORKS: !Route,
 *   LOCK_SCREEN: !Route,
 *   MANAGE_ACCESSIBILITY: !Route,
 *   MANAGE_CAPTION_SETTINGS: !Route,
 *   MANAGE_SWITCH_ACCESS_SETTINGS: !Route,
 *   MANAGE_TTS_SETTINGS: !Route,
 *   MULTIDEVICE: !Route,
 *   MULTIDEVICE_FEATURES: !Route,
 *   NEARBY_SHARE: !Route,
 *   NETWORK_DETAIL: !Route,
 *   ON_STARTUP: !Route,
 *   OS_ACCESSIBILITY: !Route,
 *   OS_LANGUAGES: !Route,
 *   OS_LANGUAGES_EDIT_DICTIONARY: !Route,
 *   OS_LANGUAGES_INPUT: !Route,
 *   OS_LANGUAGES_INPUT_METHOD_OPTIONS: !Route,
 *   OS_LANGUAGES_LANGUAGES: !Route,
 *   OS_LANGUAGES_SMART_INPUTS: !Route,
 *   OS_PRINTING: !Route,
 *   OS_PRIVACY: !Route,
 *   OS_RESET: !Route,
 *   OS_SEARCH: !Route,
 *   OS_SIGN_OUT: !Route,
 *   OS_SYNC: !Route,
 *   OS_PEOPLE: !Route,
 *   PERSONALIZATION: !Route,
 *   POINTERS: !Route,
 *   POWER: !Route,
 *   PRIVACY: !Route,
 *   PRIVACY_HUB: !Route,
 *   SEARCH: !Route,
 *   SEARCH_SUBPAGE: !Route,
 *   SMART_LOCK: !Route,
 *   SMART_PRIVACY: !Route,
 *   SMB_SHARES: !Route,
 *   STORAGE: !Route,
 *   STYLUS: !Route,
 *   SYNC: !Route,
 *   SYNC_ADVANCED: !Route,
 * }}
 */
export let OsSettingsRoutes;