package com.oplus.wrapper.app.prediction;

import android.content.Context;
import android.os.Bundle;

public class AppPredictionContext {
    private final android.app.prediction.AppPredictionContext mAppPredictionContext;

    private AppPredictionContext(android.app.prediction.AppPredictionContext context) {
        mAppPredictionContext = context;
    }

    public android.app.prediction.AppPredictionContext getAppPredictionContext() {
        return mAppPredictionContext;
    }

    public static final class Builder {
        private final android.app.prediction.AppPredictionContext.Builder mTarget;

        public Builder(Context context) {
            mTarget = new android.app.prediction.AppPredictionContext.Builder(context);
        }

        public Builder setUiSurface(String uiSurface) {
            mTarget.setUiSurface(uiSurface);
            return this;
        }

        public Builder setPredictedTargetCount(int predictedTargetCount) {
            mTarget.setPredictedTargetCount(predictedTargetCount);
            return this;
        }

        public Builder setExtras(Bundle extras) {
            mTarget.setExtras(extras);
            return this;
        }

        public AppPredictionContext build() {
            return new AppPredictionContext(mTarget.build());
        }
    }
}
