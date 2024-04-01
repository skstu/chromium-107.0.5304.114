// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Polymer element for network configuration selection menus.
 */
import '//resources/cr_elements/md_select.css.js';
import '//resources/cr_elements/policy/cr_tooltip_icon.js';
import '//resources/cr_elements/cr_shared_vars.css.js';
import '//resources/cr_elements/cr_shared_style.css.js';
import './cr_policy_network_indicator_mojo.js';
import './network_shared_css.js';

import {assertNotReached} from '//resources/js/assert.m.js';
import {I18nBehavior} from '//resources/cr_elements/i18n_behavior.js';
import {html, Polymer} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {CrPolicyNetworkBehaviorMojo} from './cr_policy_network_behavior_mojo.js';
import {NetworkConfigElementBehavior} from './network_config_element_behavior.js';
import {OncMojo} from './onc_mojo.js';

Polymer({
  _template: html`{__html_template__}`,
  is: 'network-config-select',

  behaviors: [
    I18nBehavior,
    CrPolicyNetworkBehaviorMojo,
    NetworkConfigElementBehavior,
  ],

  properties: {
    label: String,

    /** Set to true if |items| is a list of certificates. */
    certList: Boolean,

    /**
     * Set true if the dropdown list should allow only device-wide
     * certificates.
     * Note: only used when |items| is a list of certificates.
     */
    deviceCertsOnly: Boolean,

    /**
     * Array of item values to select from.
     * @type {!Array<string|number>}
     */
    items: Array,

    /** Select item key, used for converting enums to strings */
    key: String,

    /** Prefix used to look up ONC property names. */
    oncPrefix: {
      type: String,
      value: '',
    },

    /**
     * Select item value
     * @type {string|number}
     */
    value: {
      type: Object,
      notify: true,
    },
  },

  observers: ['updateSelected_(items, value)'],

  focus() {
    this.$$('select').focus();
  },

  /**
   * Ensure that the <select> value is updated when |items| or |value| changes.
   * @private
   */
  updateSelected_() {
    // Wait for the dom-repeat to populate the <option> entries.
    this.async(function() {
      const select = this.$$('select');
      if (select.value !== this.value) {
        select.value = this.value;
      }
    });
  },

  /**
   * Returns a localized label for |item|. If |this.key| is set, |item| is
   * expected to be an enum and the key is used to convert it to a string.
   * @param {string|number|!chromeos.networkConfig.mojom.NetworkCertificate}
   *     item
   * @return {string}
   * @private
   */
  getItemLabel_(item) {
    if (this.certList) {
      return this.getCertificateName_(
          /** @type {!chromeos.networkConfig.mojom.NetworkCertificate}*/ (
              item));
    }
    let value;
    if (this.key) {
      // |item| is an enum, convert the enum to a string.
      value = /** @type {string} */ (
          OncMojo.getTypeString(this.key, /** @type {number} */ (item)));
    } else {
      value = /** @type {string} */ (item);
    }
    // The i18n dictonary is populated with all supported ONC values in the
    // format Onc + prefix + value, with '-' replaceing '.' in the prefix.
    // See network_element_localized_strings_provider.cc.
    const oncValue = 'Onc' + this.oncPrefix.replace(/\./g, '-') + '_' + value;
    if (this.i18nExists(oncValue)) {
      return this.i18n(oncValue);
    }
    // All selectable values should be localized.
    assertNotReached('ONC value not found: ' + oncValue);
    return value;
  },

  /**
   * @param {string|number|!chromeos.networkConfig.mojom.NetworkCertificate}
   *     item
   * @return {string|number}
   * @private
   */
  getItemValue_(item) {
    if (this.certList) {
      return /** @type {chromeos.networkConfig.mojom.NetworkCertificate}*/ (
                 item)
          .hash;
    }
    return /** @type {string|number}*/ (item);
  },

  /**
   * @param {string|!chromeos.networkConfig.mojom.NetworkCertificate} item
   * @return {boolean}
   * @private
   */
  getItemEnabled_(item) {
    if (this.certList) {
      const cert =
          /** @type {chromeos.networkConfig.mojom.NetworkCertificate}*/ (item);
      if (this.deviceCertsOnly && !cert.deviceWide) {
        return false;
      }
      return !!cert.hash;
    }
    return true;
  },

  /**
   * @param {!chromeos.networkConfig.mojom.NetworkCertificate} certificate
   * @return {string}
   * @private
   */
  getCertificateName_(certificate) {
    if (certificate.hardwareBacked) {
      return this.i18n(
          'networkCertificateNameHardwareBacked', certificate.issuedBy,
          certificate.issuedTo);
    }
    if (certificate.issuedTo) {
      return this.i18n(
          'networkCertificateName', certificate.issuedBy, certificate.issuedTo);
    }
    return certificate.issuedBy;
  },
});