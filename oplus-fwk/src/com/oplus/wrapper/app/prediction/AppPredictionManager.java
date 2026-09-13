package com.oplus.wrapper.app.prediction;

import android.content.Context;

public class AppPredictionManager {
    private final android.app.prediction.AppPredictionManager mTarget;

    public AppPredictionManager(Context context) {
        mTarget = new android.app.prediction.AppPredictionManager(context);
    }

    public AppPredictor createAppPredictionSession(AppPredictionContext predictionContext) {
        return new AppPredictor(
                mTarget.createAppPredictionSession(predictionContext.getAppPredictionContext()));
    }
}
