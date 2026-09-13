package com.oplus.wrapper.app.prediction;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;

public class AppPredictor {
    private final android.app.prediction.AppPredictor mTarget;
    private CallbackImpl mCallbackImpl;

    public interface Callback {
        void onTargetsAvailable(List<AppTarget> targets);
    }

    public AppPredictor(android.app.prediction.AppPredictor target) {
        mTarget = target;
    }

    public void requestPredictionUpdate() {
        mTarget.requestPredictionUpdate();
    }

    public void destroy() {
        mTarget.destroy();
    }

    public void registerPredictionUpdates(Executor callbackExecutor, Callback callback) {
        mCallbackImpl = new CallbackImpl(callback);
        mTarget.registerPredictionUpdates(callbackExecutor, mCallbackImpl);
    }

    public void unregisterPredictionUpdates(Callback callback) {
        if (mCallbackImpl != null && callback == mCallbackImpl.mCallback) {
            mTarget.unregisterPredictionUpdates(mCallbackImpl);
        }
    }

    private static class CallbackImpl implements android.app.prediction.AppPredictor.Callback {
        private final Callback mCallback;

        CallbackImpl(Callback callback) {
            mCallback = callback;
        }

        @Override
        public void onTargetsAvailable(List<android.app.prediction.AppTarget> targets) {
            List<AppTarget> wrapped = new ArrayList<>(targets.size());
            for (int i = 0; i < targets.size(); i++) {
                wrapped.add(new AppTarget(targets.get(i)));
            }
            mCallback.onTargetsAvailable(wrapped);
        }
    }
}
