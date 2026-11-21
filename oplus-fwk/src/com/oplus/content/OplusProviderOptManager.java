package com.oplus.content;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

/**
 * On ColorOS this hands the query arguments of a following provider query to a framework hook
 * that answers it in the client. That hook does not exist here, so the client simply queries
 * the provider itself. OpenCapabilityService calls it before reading the ROM update provider.
 */
public final class OplusProviderOptManager {

    private static final String TAG = "OplusProviderOptManager";

    private OplusProviderOptManager() {}

    public static boolean prepareForClient(Context context, Uri uri, String selection,
            String[] selectionArgs, String sortOrder) {
        if (context == null || uri == null) {
            Log.d(TAG, "prepareForClient failed for uri: " + uri);
            return false;
        }
        if (selection == null && selectionArgs == null && sortOrder == null) {
            Log.d(TAG, "prepareForClient failed for null queryArgs");
            return false;
        }
        return true;
    }
}
