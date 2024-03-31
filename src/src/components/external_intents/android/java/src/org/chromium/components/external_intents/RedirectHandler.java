// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.external_intents;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.os.SystemClock;
import android.provider.Browser;
import android.text.TextUtils;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.ContextUtils;
import org.chromium.base.Function;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.ui.base.PageTransition;

import java.util.HashSet;
import java.util.List;

/**
 * This class contains the logic to determine effective navigation/redirect.
 */
public class RedirectHandler {
    private static final String TAG = "RedirectHandler";

    // The last committed entry index when no navigations have committed.
    public static final int NO_COMMITTED_ENTRY_INDEX = -1;
    // An invalid entry index.
    private static final int INVALID_ENTRY_INDEX = -2;
    public static final long INVALID_TIME = -1;

    private static final int NAVIGATION_TYPE_FROM_INTENT = 1;
    private static final int NAVIGATION_TYPE_FROM_USER_TYPING = 2;
    private static final int NAVIGATION_TYPE_FROM_LINK_WITHOUT_USER_GESTURE = 3;
    private static final int NAVIGATION_TYPE_FROM_RELOAD = 4;
    private static final int NAVIGATION_TYPE_OTHER = 5;

    // Analogous to Transient User Activation in blink (See
    // https://html.spec.whatwg.org/multipage/interaction.html#tracking-user-activation). We don't
    // want an "unattended" page to redirect to an app as the user is likely not expecting that.
    // However, historically there was no timeout like this for external navigation (and instead
    // touching the screen reset the navigation chain), so this timeout is very generous and should
    // allow for redirect chains.
    public static final long NAVIGATION_CHAIN_TIMEOUT_MILLIS = 15000;

    private static class IntentState {
        final Intent mInitialIntent;
        final boolean mIsCustomTabIntent;
        final boolean mPreferToStayInChrome;
        final boolean mExternalIntentStartedTask;

        // A resolver list which includes all resolvers of |mInitialIntent|.
        HashSet<ComponentName> mCachedResolvers = new HashSet<ComponentName>();

        IntentState(Intent initialIntent, boolean preferToStayInChrome, boolean isCustomTabIntent,
                boolean externalIntentStartedTask) {
            mInitialIntent = initialIntent;
            mPreferToStayInChrome = preferToStayInChrome;
            mIsCustomTabIntent = isCustomTabIntent;
            mExternalIntentStartedTask = externalIntentStartedTask;
        }
    }

    /**
     * Captures the state of the initial navigation in a Navigation Chain.
     */
    public class InitialNavigationState {
        public final boolean isRendererInitiated;
        public final boolean hasUserGesture;
        public final boolean isFromReload;
        public final boolean isFromTyping;
        public final boolean isFromFormSubmit;
        public final boolean isFromIntent;

        public InitialNavigationState(boolean isRendererInitiated, boolean hasUserGesture,
                boolean isFromReload, boolean isFromTyping, boolean isFromFormSubmit,
                boolean isFromIntent) {
            this.isRendererInitiated = isRendererInitiated;
            this.hasUserGesture = hasUserGesture;
            this.isFromReload = isFromReload;
            this.isFromTyping = isFromTyping;
            this.isFromFormSubmit = isFromFormSubmit;
            this.isFromIntent = isFromIntent;
        }
    }

    private class NavigationChainState {
        private final int mInitialNavigationType;
        final boolean mHasUserStartedNonInitialNavigation;
        boolean mIsOnFirstLoadInChain = true;
        boolean mShouldNotOverrideUrlLoadingOnCurrentNavigationChain;
        boolean mShouldNotBlockOverrideUrlLoadingOnCurrentNavigationChain;
        // TODO(https://crbug.com/1286053): Plumb through the user activation time from blink.
        final long mNavigationChainStartTime = currentRealtime();
        boolean mUsedBackOrForward;
        private final InitialNavigationState mInitialNavigationState;

        NavigationChainState(
                int initialNavigationType, boolean hasUserStartedNonInitialNavigation) {
            assert !isRefactoringEnabled();
            mInitialNavigationType = initialNavigationType;
            mHasUserStartedNonInitialNavigation = hasUserStartedNonInitialNavigation;
            mInitialNavigationState = null;
        }

        NavigationChainState(boolean hasUserStartedNonInitialNavigation,
                InitialNavigationState initialNavigationChainState) {
            assert isRefactoringEnabled();
            mHasUserStartedNonInitialNavigation = hasUserStartedNonInitialNavigation;
            mInitialNavigationState = initialNavigationChainState;
            mInitialNavigationType = 0;
        }

        int getInitialNavigationType() {
            assert !isRefactoringEnabled();
            return mInitialNavigationType;
        }

        InitialNavigationState getInitialNavigationState() {
            assert isRefactoringEnabled();
            return mInitialNavigationState;
        }
    }

    private long mLastNewUrlLoadingTime = INVALID_TIME;
    private IntentState mIntentState;
    private NavigationChainState mNavigationChainState;

    // Not part of NavigationChainState as this should persist through resetting of the
    // NavigationChain so that the history state can be correctly set even after the tab is hidden.
    private int mLastCommittedEntryIndexBeforeStartingNavigation = INVALID_ENTRY_INDEX;

    private long mLastUserInteractionTimeMillis;

    public static RedirectHandler create() {
        return new RedirectHandler();
    }

    protected RedirectHandler() {}

    /**
     * Temporarily gate ExternalNavigationHandler/RedirectHandler behind a finch kill switch in case
     * things unexpectedly break.
     */
    public static boolean isRefactoringEnabled() {
        return ExternalIntentsFeatures.SCARY_EXTERNAL_NAVIGATION_REFACTORING.isEnabled();
    }

    /**
     * Resets |mIntentState| for the newly received Intent.
     */
    public void updateIntent(Intent intent, boolean isCustomTabIntent, boolean sendToExternalApps,
            boolean externalIntentStartedTask) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction())) {
            mIntentState = null;
            return;
        }

        boolean preferToStayInChrome = isIntentToChrome(intent);

        // A Custom Tab Intent from a Custom Tab Session will always have the package set, so the
        // Intent will always be to Chrome. Therefore, we provide an Extra to allow the initial
        // Intent navigation chain to leave Chrome.
        if (isCustomTabIntent && sendToExternalApps) preferToStayInChrome = false;

        // A sanitized copy of the initial intent for detecting if resolvers have changed.
        Intent initialIntent = new Intent(intent);
        ExternalNavigationHandler.sanitizeQueryIntentActivitiesIntent(initialIntent);
        mIntentState = new IntentState(
                initialIntent, preferToStayInChrome, isCustomTabIntent, externalIntentStartedTask);
    }

    private static boolean isIntentToChrome(Intent intent) {
        String chromePackageName = ContextUtils.getApplicationContext().getPackageName();
        return TextUtils.equals(chromePackageName, intent.getPackage())
                || TextUtils.equals(chromePackageName,
                        IntentUtils.safeGetStringExtra(intent, Browser.EXTRA_APPLICATION_ID));
    }

    /**
     * Resets navigation and intent state.
     */
    public void clear() {
        mIntentState = null;
        mNavigationChainState = null;
    }

    /**
     * Will cause shouldNotOverrideUrlLoading() to return true until a new user-initiated navigation
     * occurs.
     */
    public void setShouldNotOverrideUrlLoadingOnCurrentRedirectChain() {
        mNavigationChainState.mShouldNotOverrideUrlLoadingOnCurrentNavigationChain = true;
    }

    /**
     * Will cause shouldNotBlockUrlLoadingOverrideOnCurrentRedirectionChain() to return true until
     * a new user-initiated navigation occurs.
     */
    public void setShouldNotBlockUrlLoadingOverrideOnCurrentRedirectionChain() {
        mNavigationChainState.mShouldNotBlockOverrideUrlLoadingOnCurrentNavigationChain = true;
    }

    /**
     * @return true if the task for the Activity was created by the most recent external intent
     *         navigation to the current tab. Note that this doesn't include cold Activity starts
     *         that re-use an existing task (eg. Chrome was killed by Android without its task being
     *         swiped away or timed out).
     */
    public boolean wasTaskStartedByExternalIntent() {
        return mIntentState != null && mIntentState.mExternalIntentStartedTask;
    }

    /**
     * Updates new url loading information to trace navigation.
     * A time based heuristic is used to determine if this loading is an effective redirect or not
     * if core of |pageTransType| is LINK.
     *
     * http://crbug.com/322567 : Trace navigation started from an external app.
     * http://crbug.com/331571 : Trace navigation started from user typing to do not override such
     * navigation.
     * http://crbug.com/426679 : Trace every navigation and the last committed entry index right
     * before starting the navigation.
     *
     * @param pageTransType page transition type of this loading.
     * @param isRedirect whether this loading is http redirect or not.
     * @param hasUserGesture whether this loading is started by a user gesture.
     * @param lastUserInteractionTime time when the last user interaction was made.
     * @param lastCommittedEntryIndex the last committed entry index right before this loading.
     * @param isInitialNavigation whether this loading is for the initial navigation in a Tab.
     * @param isRendererInitiated whether the navigation was initiated by a Renderer.
     */
    public void updateNewUrlLoading(int pageTransType, boolean isRedirect, boolean hasUserGesture,
            long lastUserInteractionTime, int lastCommittedEntryIndex, boolean isInitialNavigation,
            boolean isRendererInitiated) {
        mLastUserInteractionTimeMillis = lastUserInteractionTime;

        if (isRefactoringEnabled()) {
            // Treat anything renderer-initiated without a gesture as part of the same navigation
            // chain. Server redirects are also part of the same navigation chain.
            boolean isSameNavigationChain = isRedirect || (isRendererInitiated && !hasUserGesture);

            if (mNavigationChainState != null && isSameNavigationChain) {
                updateNavigationChainState(pageTransType);
            } else {
                resetNavigationChainState(pageTransType, hasUserGesture, lastCommittedEntryIndex,
                        isInitialNavigation, isRendererInitiated);
            }
            boolean isBackOrForward = (pageTransType & PageTransition.FORWARD_BACK) != 0;
            if (isBackOrForward) mNavigationChainState.mUsedBackOrForward = true;
        } else {
            updateNewUrlLoadingOldVersion(pageTransType, isRedirect, hasUserGesture,
                    lastUserInteractionTime, lastCommittedEntryIndex, isInitialNavigation,
                    isRendererInitiated);
        }
    }

    private void updateNewUrlLoadingOldVersion(int pageTransType, boolean isRedirect,
            boolean hasUserGesture, long lastUserInteractionTime, int lastCommittedEntryIndex,
            boolean isInitialNavigation, boolean isRendererInitiated) {
        long prevNewUrlLoadingTime = mLastNewUrlLoadingTime;
        mLastNewUrlLoadingTime = SystemClock.elapsedRealtime();

        int pageTransitionCore = pageTransType & PageTransition.CORE_MASK;

        boolean isNewLoadingStartedByUser = mNavigationChainState == null;
        boolean isFromIntent = (pageTransType & PageTransition.FROM_API) != 0;
        if (!isRedirect) {
            if ((pageTransType & PageTransition.FORWARD_BACK) != 0) {
                isNewLoadingStartedByUser = true;
            } else if (pageTransitionCore != PageTransition.LINK
                    && pageTransitionCore != PageTransition.FORM_SUBMIT) {
                isNewLoadingStartedByUser = true;
            } else if (prevNewUrlLoadingTime == INVALID_TIME || isFromIntent
                    || lastUserInteractionTime > prevNewUrlLoadingTime) {
                isNewLoadingStartedByUser = true;
            }
        }
        if (!isNewLoadingStartedByUser) {
            mNavigationChainState.mIsOnFirstLoadInChain = false;
            return;
        }

        // Create the NavigationChainState for a new Navigation chain.
        int initialNavigationType;
        if (isFromIntent && mIntentState != null) {
            initialNavigationType = NAVIGATION_TYPE_FROM_INTENT;
        } else {
            mIntentState = null;
            if (pageTransitionCore == PageTransition.TYPED) {
                initialNavigationType = NAVIGATION_TYPE_FROM_USER_TYPING;
            } else if (pageTransitionCore == PageTransition.RELOAD
                    || (pageTransType & PageTransition.FORWARD_BACK) != 0) {
                initialNavigationType = NAVIGATION_TYPE_FROM_RELOAD;
            } else if (pageTransitionCore == PageTransition.LINK && !hasUserGesture) {
                initialNavigationType = NAVIGATION_TYPE_FROM_LINK_WITHOUT_USER_GESTURE;
            } else {
                initialNavigationType = NAVIGATION_TYPE_OTHER;
            }
        }
        mNavigationChainState =
                new NavigationChainState(initialNavigationType, !isInitialNavigation);
        mLastCommittedEntryIndexBeforeStartingNavigation = lastCommittedEntryIndex;
    }

    private void updateNavigationChainState(int pageTransType) {
        mNavigationChainState.mIsOnFirstLoadInChain = false;
    }

    private void resetNavigationChainState(int pageTransType, boolean hasUserGesture,
            int lastCommittedEntryIndex, boolean isInitialNavigation, boolean isRendererInitiated) {
        // Create the NavigationChainState for a new Navigation chain.
        int pageTransitionCore = pageTransType & PageTransition.CORE_MASK;
        boolean isFromApi = (pageTransType & PageTransition.FROM_API) != 0;
        boolean isFromIntent = isFromApi && mIntentState != null;
        boolean isFromReload = pageTransitionCore == PageTransition.RELOAD;
        boolean isFromTyping = pageTransitionCore == PageTransition.TYPED;
        boolean isFromFormSubmit = pageTransitionCore == PageTransition.FORM_SUBMIT;

        if (!isFromIntent) mIntentState = null;
        InitialNavigationState initialNavigationChainState =
                new InitialNavigationState(isRendererInitiated, hasUserGesture, isFromReload,
                        isFromTyping, isFromFormSubmit, isFromIntent);

        mNavigationChainState =
                new NavigationChainState(!isInitialNavigation, initialNavigationChainState);
        mLastCommittedEntryIndexBeforeStartingNavigation = lastCommittedEntryIndex;
    }

    /**
     * @return whether this is a navigation chain initiated by an intent that is on a noninitial
     *         navigation (eg. has followed a client or server redirect).
     */
    public boolean isOnNoninitialLoadForIntentNavigationChain() {
        if (isRefactoringEnabled()) {
            return mNavigationChainState.getInitialNavigationState().isFromIntent
                    && !mNavigationChainState.mIsOnFirstLoadInChain;

        } else {
            return mNavigationChainState.getInitialNavigationType() == NAVIGATION_TYPE_FROM_INTENT
                    && !mNavigationChainState.mIsOnFirstLoadInChain;
        }
    }

    /**
     * @return whether we're on the first load in the current navigation chain.
     */
    public boolean isOnFirstLoadInNavigationChain() {
        return mNavigationChainState.mIsOnFirstLoadInChain;
    }

    /**
     * @param hasExternalProtocol whether the destination URI has an external protocol or not.
     * @param isForTrustedCallingApp whether the app we would launch to is trusted and what launched
     *                               Chrome.
     * @return whether we should stay in Chrome or not.
     */
    /* package */ boolean shouldStayInApp(
            boolean hasExternalProtocol, boolean isForTrustedCallingApp) {
        assert !isRefactoringEnabled();
        // http://crbug/424029 : Need to stay in Chrome for an intent heading explicitly to Chrome.
        // http://crbug/881740 : Relax stay in Chrome restriction for Custom Tabs.
        return (mIntentState != null && mIntentState.mPreferToStayInChrome && !hasExternalProtocol)
                || shouldNavigationTypeStayInApp(isForTrustedCallingApp);
    }

    private boolean shouldNavigationTypeStayInApp(boolean isForTrustedCallingApp) {
        // http://crbug.com/162106: Never leave Chrome from a refresh.
        if (mNavigationChainState.getInitialNavigationType() == NAVIGATION_TYPE_FROM_RELOAD) {
            return true;
        }

        // If the app we would navigate to is trusted and what launched Chrome, allow the
        // navigation.
        if (isForTrustedCallingApp) return false;

        // Otherwise allow navigation out of the app only with a user gesture.
        return mNavigationChainState.getInitialNavigationType()
                == NAVIGATION_TYPE_FROM_LINK_WITHOUT_USER_GESTURE;
    }

    /**
     * @return Whether this navigation is initiated by a Custom Tabs {@link Intent}.
     */
    public boolean isFromCustomTabIntent() {
        return mIntentState != null && mIntentState.mIsCustomTabIntent;
    }

    /**
     * @return whether navigation is from a user's typing or not.
     */
    public boolean isNavigationFromUserTyping() {
        if (isRefactoringEnabled()) {
            return mNavigationChainState.getInitialNavigationState().isFromTyping;

        } else {
            return mNavigationChainState.getInitialNavigationType()
                    == NAVIGATION_TYPE_FROM_USER_TYPING;
        }
    }

    /**
     * @return whether we should stay in Chrome or not.
     */
    public boolean shouldNotOverrideUrlLoading() {
        return mNavigationChainState.mShouldNotOverrideUrlLoadingOnCurrentNavigationChain;
    }

    /**
     * @return whether we should continue allowing navigation handling in the current redirection
     * chain.
     */
    public boolean getAndClearShouldNotBlockOverrideUrlLoadingOnCurrentRedirectionChain() {
        boolean value =
                mNavigationChainState.mShouldNotBlockOverrideUrlLoadingOnCurrentNavigationChain;
        mNavigationChainState.mShouldNotBlockOverrideUrlLoadingOnCurrentNavigationChain = false;
        return value;
    }

    /**
     * @return whether on navigation or not.
     */
    public boolean isOnNavigation() {
        return mNavigationChainState != null;
    }

    /**
     * @return the last committed entry index which was saved before starting this navigation.
     */
    public int getLastCommittedEntryIndexBeforeStartingNavigation() {
        assert mLastCommittedEntryIndexBeforeStartingNavigation != INVALID_ENTRY_INDEX;
        return mLastCommittedEntryIndexBeforeStartingNavigation;
    }

    /**
     * @return whether the user has started a non-initial navigation.
     */
    public boolean hasUserStartedNonInitialNavigation() {
        return mNavigationChainState != null
                && mNavigationChainState.mHasUserStartedNonInitialNavigation;
    }

    /**
     * @return whether |intent| has a new resolver against |mIntentHistory| or not.
     */
    public boolean hasNewResolver(List<ResolveInfo> resolvingInfos,
            Function<Intent, List<ResolveInfo>> queryIntentActivitiesFunction) {
        if (mIntentState == null) return !resolvingInfos.isEmpty();

        if (mIntentState.mCachedResolvers.isEmpty()) {
            for (ResolveInfo r : queryIntentActivitiesFunction.apply(mIntentState.mInitialIntent)) {
                mIntentState.mCachedResolvers.add(
                        new ComponentName(r.activityInfo.packageName, r.activityInfo.name));
            }
        }
        if (resolvingInfos.size() > mIntentState.mCachedResolvers.size()) return true;
        for (ResolveInfo r : resolvingInfos) {
            if (!mIntentState.mCachedResolvers.contains(
                        new ComponentName(r.activityInfo.packageName, r.activityInfo.name))) {
                return true;
            }
        }
        return false;
    }

    /**
     * @return The initial intent of the navigation chain, if available.
     */
    public Intent getInitialIntent() {
        return mIntentState != null ? mIntentState.mInitialIntent : null;
    }

    /**
     * @return whether the navigation chain has expired, meaning
     * {@link #NAVIGATION_CHAIN_TIMEOUT_MILLIS} milliseconds passed since a navigation initiated by
     * the user was started.
     */
    public boolean isNavigationChainExpired() {
        return currentRealtime() - mNavigationChainState.mNavigationChainStartTime
                > NAVIGATION_CHAIN_TIMEOUT_MILLIS;
    }

    public boolean navigationChainUsedBackOrForward() {
        return mNavigationChainState.mUsedBackOrForward;
    }

    public InitialNavigationState getInitialNavigationState() {
        return mNavigationChainState.getInitialNavigationState();
    }

    public boolean intentPrefersToStayInChrome() {
        return mIntentState != null && mIntentState.mPreferToStayInChrome;
    }

    public void maybeLogExternalRedirectBlockedWithMissingGesture() {
        boolean shouldLog = false;
        if (isRefactoringEnabled()) {
            shouldLog = mNavigationChainState.getInitialNavigationState().isRendererInitiated
                    && !mNavigationChainState.getInitialNavigationState().hasUserGesture;

        } else {
            shouldLog = mNavigationChainState.getInitialNavigationType()
                    == NAVIGATION_TYPE_FROM_LINK_WITHOUT_USER_GESTURE;
        }
        if (!shouldLog) return;
        long millisSinceLastGesture =
                SystemClock.elapsedRealtime() - mLastUserInteractionTimeMillis;
        Log.w(TAG,
                "External navigation blocked due to missing gesture. Last input was "
                        + millisSinceLastGesture + "ms ago.");
        RecordHistogram.recordTimesHistogram(
                "Android.Intent.BlockedExternalNavLastGestureTime", millisSinceLastGesture);
    }

    // Facilitates simulated waiting in tests.
    @VisibleForTesting
    public long currentRealtime() {
        return SystemClock.elapsedRealtime();
    }
}
