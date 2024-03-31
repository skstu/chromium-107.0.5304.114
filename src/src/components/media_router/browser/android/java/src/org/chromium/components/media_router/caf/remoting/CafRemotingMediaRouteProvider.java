// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.media_router.caf.remoting;

import androidx.annotation.Nullable;
import androidx.mediarouter.media.MediaRouter;

import org.chromium.base.Log;
import org.chromium.components.media_router.BrowserMediaRouter;
import org.chromium.components.media_router.FlingingController;
import org.chromium.components.media_router.MediaRouteManager;
import org.chromium.components.media_router.MediaRouteProvider;
import org.chromium.components.media_router.MediaRouteUmaRecorder;
import org.chromium.components.media_router.MediaSource;
import org.chromium.components.media_router.caf.BaseSessionController;
import org.chromium.components.media_router.caf.CafBaseMediaRouteProvider;

/** A {@link MediaRouteProvider} implementation for remoting, using Cast v3 API. */
public class CafRemotingMediaRouteProvider extends CafBaseMediaRouteProvider {
    private static final String TAG = "RmtMRP";

    // The session controller which is always attached to the current CastSession.
    private final RemotingSessionController mSessionController;

    public static CafRemotingMediaRouteProvider create(MediaRouteManager manager) {
        return new CafRemotingMediaRouteProvider(
                BrowserMediaRouter.getAndroidMediaRouter(), manager);
    }

    @Override
    protected MediaSource getSourceFromId(String sourceId) {
        return RemotingMediaSource.from(sourceId);
    }

    @Override
    public BaseSessionController sessionController() {
        return mSessionController;
    }

    @Override
    public void joinRoute(
            String sourceId, String presentationId, String origin, int tabId, int nativeRequestId) {
        mManager.onJoinRouteRequestError(
                "Remote playback doesn't support joining routes", nativeRequestId);
    }

    @Override
    public void detachRoute(String routeId) {
        super.detachRoute(routeId);
        MediaRouteUmaRecorder.recordRemoteSessionTimeWithoutMediaElementPercentage(
                mSessionController.getFlingingController().getApproximateCurrentTime(),
                mSessionController.getFlingingController().getDuration());
    }

    @Override
    public void sendStringMessage(String routeId, String message) {
        Log.e(TAG, "Remote playback does not support sending messages");
    }

    private CafRemotingMediaRouteProvider(
            MediaRouter androidMediaRouter, MediaRouteManager manager) {
        super(androidMediaRouter, manager);
        mSessionController = new RemotingSessionController(this);
    }

    @Override
    @Nullable
    public FlingingController getFlingingController(String routeId) {
        if (!sessionController().isConnected()) {
            return null;
        }

        if (!mRoutes.containsKey(routeId)) return null;

        return sessionController().getFlingingController();
    }
}
