// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the shared code for the new (Chrome 37) security interstitials. It is
// used for both SSL interstitials and Safe Browsing interstitials.

let expandedDetails = false;
let keyPressState = 0;

/**
 * This allows errors to be skippped by typing a secret phrase into the page.
 * @param {string} e The key that was just pressed.
 */
function handleKeypress(e) {
  // HTTPS errors are serious and should not be ignored. For testing purposes,
  // other approaches are both safer and have fewer side-effects.
  // See https://goo.gl/ZcZixP for more details.
  const BYPASS_SEQUENCE = window.atob('dGhpc2lzdW5zYWZl');
  if (BYPASS_SEQUENCE.charCodeAt(keyPressState) === e.keyCode) {
    keyPressState++;
    if (keyPressState === BYPASS_SEQUENCE.length) {
      sendCommand(SecurityInterstitialCommandId.CMD_PROCEED);
      keyPressState = 0;
    }
  } else {
    keyPressState = 0;
  }
}

/**
 * This appends a piece of debugging information to the end of the warning.
 * When complete, the caller must also make the debugging div
 * (error-debugging-info) visible.
 * @param {string} title  The name of this debugging field.
 * @param {string} value  The value of the debugging field.
 * @param {boolean=} fixedWidth If true, the value field is displayed fixed
 *                              width.
 */
function appendDebuggingField(title, value, fixedWidth) {
  // The values input here are not trusted. Never use innerHTML on these
  // values!
  const spanTitle = document.createElement('span');
  spanTitle.classList.add('debugging-title');
  spanTitle.innerText = title + ': ';

  const spanValue = document.createElement('span');
  spanValue.classList.add('debugging-content');
  if (fixedWidth) {
    spanValue.classList.add('debugging-content-fixed-width');
  }
  spanValue.innerText = value;

  const pElem = document.createElement('p');
  pElem.classList.add('debugging-content');
  pElem.appendChild(spanTitle);
  pElem.appendChild(spanValue);
  $('error-debugging-info').appendChild(pElem);
}

function toggleDebuggingInfo() {
  const hiddenDebug = $('error-debugging-info').classList.toggle(HIDDEN_CLASS);
  $('error-code').setAttribute('aria-expanded', !hiddenDebug);
}

function setupEvents() {
  const overridable = loadTimeData.getBoolean('overridable');
  const interstitialType = loadTimeData.getString('type');
  const ssl = interstitialType === 'SSL';
  const captivePortal = interstitialType === 'CAPTIVE_PORTAL';
  const badClock = ssl && loadTimeData.getBoolean('bad_clock');
  const lookalike = interstitialType === 'LOOKALIKE';
  const billing =
      interstitialType === 'SAFEBROWSING' && loadTimeData.getBoolean('billing');
  const blockedInterception = interstitialType === 'BLOCKED_INTERCEPTION';
  const insecureForm = interstitialType == 'INSECURE_FORM';
  const httpsOnly = interstitialType == 'HTTPS_ONLY';
  const hidePrimaryButton = loadTimeData.getBoolean('hide_primary_button');
  const showRecurrentErrorParagraph = loadTimeData.getBoolean(
    'show_recurrent_error_paragraph');

  if (ssl || blockedInterception) {
    $('body').classList.add(badClock ? 'bad-clock' : 'ssl');
    if (loadTimeData.valueExists('errorCode')) {
      $('error-code').textContent = loadTimeData.getString('errorCode');
      $('error-code').classList.remove(HIDDEN_CLASS);
    }
  } else if (captivePortal) {
    $('body').classList.add('captive-portal');
  } else if (billing) {
    $('body').classList.add('safe-browsing-billing');
  } else if (lookalike) {
    $('body').classList.add('lookalike-url');
  } else if (insecureForm) {
    $('body').classList.add('insecure-form');
  } else if (httpsOnly) {
    $('body').classList.add('https-only');
  } else {
    $('body').classList.add('safe-browsing');
    // Override the default theme color.
    document.querySelector('meta[name=theme-color]').setAttribute('content',
      'rgb(217, 48, 37)');
  }

  $('icon').classList.add('icon');

  if (hidePrimaryButton) {
    $('primary-button').classList.add(HIDDEN_CLASS);
  } else {
    $('primary-button').addEventListener('click', function() {
      switch (interstitialType) {
        case 'CAPTIVE_PORTAL':
          sendCommand(SecurityInterstitialCommandId.CMD_OPEN_LOGIN);
          break;

        case 'SSL':
          if (badClock) {
            sendCommand(SecurityInterstitialCommandId.CMD_OPEN_DATE_SETTINGS);
          } else if (overridable) {
            sendCommand(SecurityInterstitialCommandId.CMD_DONT_PROCEED);
          } else {
            sendCommand(SecurityInterstitialCommandId.CMD_RELOAD);
          }
          break;

        case 'SAFEBROWSING':
        case 'ORIGIN_POLICY':
          sendCommand(SecurityInterstitialCommandId.CMD_DONT_PROCEED);
          break;
        case 'HTTPS_ONLY':
        case 'INSECURE_FORM':
        case 'LOOKALIKE':
          sendCommand(SecurityInterstitialCommandId.CMD_DONT_PROCEED);
          break;

        default:
          throw new Error('Invalid interstitial type');
      }
    });
  }

  if (lookalike || insecureForm || httpsOnly) {
    const proceedButton = 'proceed-button';
    $(proceedButton).classList.remove(HIDDEN_CLASS);
    $(proceedButton).textContent = loadTimeData.getString('proceedButtonText');
    $(proceedButton).addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_PROCEED);
    });
  }
  if (lookalike) {
    // Lookalike interstitials with a suggested URL have a link in the title:
    // "Did you mean <link>example.com</link>?". Handle those clicks. Lookalike
    // interstitails without a suggested URL don't have this link.
    const dontProceedLink = 'dont-proceed-link';
    if ($(dontProceedLink)) {
      $(dontProceedLink).addEventListener('click', function(event) {
        sendCommand(SecurityInterstitialCommandId.CMD_DONT_PROCEED);
      });
    }
  }

  if (overridable) {
    const overrideElement = billing ? 'proceed-button' : 'proceed-link';
    // Captive portal page isn't overridable.
    $(overrideElement).addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_PROCEED);
    });

    if (ssl) {
      $(overrideElement).classList.add('small-link');
    } else if (billing) {
      $(overrideElement).classList.remove(HIDDEN_CLASS);
      $(overrideElement).textContent =
          loadTimeData.getString('proceedButtonText');
    }
  } else if (!ssl) {
    $('final-paragraph').classList.add(HIDDEN_CLASS);
  }


  if (!ssl || !showRecurrentErrorParagraph) {
    $('recurrent-error-message').classList.add(HIDDEN_CLASS);
  } else {
    $('body').classList.add('showing-recurrent-error-message');
  }

  if ($('diagnostic-link')) {
    $('diagnostic-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_OPEN_DIAGNOSTIC);
    });
  }

  if ($('learn-more-link')) {
    $('learn-more-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_OPEN_HELP_CENTER);
    });
  }

  if (captivePortal || billing || lookalike || insecureForm || httpsOnly) {
    // Captive portal, billing, lookalike pages, insecure form, and
    // HTTPS only mode interstitials don't have details buttons.
    $('details-button').classList.add('hidden');
  } else {
    $('details-button')
        .setAttribute(
            'aria-expanded', !$('details').classList.contains(HIDDEN_CLASS));
    $('details-button').addEventListener('click', function(event) {
      const hiddenDetails = $('details').classList.toggle(HIDDEN_CLASS);
      $('details-button').setAttribute('aria-expanded', !hiddenDetails);

      if (mobileNav) {
        // Details appear over the main content on small screens.
        $('main-content').classList.toggle(HIDDEN_CLASS, !hiddenDetails);
      } else {
        $('main-content').classList.remove(HIDDEN_CLASS);
      }

      $('details-button').innerText = hiddenDetails ?
          loadTimeData.getString('openDetails') :
          loadTimeData.getString('closeDetails');
      if (!expandedDetails) {
        // Record a histogram entry only the first time that details is opened.
        sendCommand(SecurityInterstitialCommandId.CMD_SHOW_MORE_SECTION);
        expandedDetails = true;
      }
    });
  }

  if ($('report-error-link')) {
    $('report-error-link').addEventListener('click', function(event) {
      sendCommand(SecurityInterstitialCommandId.CMD_REPORT_PHISHING_ERROR);
    });
  }

  if (lookalike) {
    console.warn(loadTimeData.getString('lookalikeConsoleMessage'));
  }

  preventDefaultOnPoundLinkClicks();
  setupExtendedReportingCheckbox();
  setupEnhancedProtectionMessage();
  setupSSLDebuggingInfo();
  document.addEventListener('keypress', handleKeypress);
}

document.addEventListener('DOMContentLoaded', setupEvents);