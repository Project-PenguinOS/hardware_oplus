package com.oplus.wrapper.content;

import android.database.ContentObserver;
import android.net.Uri;

/* Wrapper over the hidden per-user ContentResolver calls. */
public class ContentResolver {
    private final android.content.ContentResolver mResolver;

    public ContentResolver(android.content.ContentResolver resolver) {
        mResolver = resolver;
    }

    public void registerContentObserver(Uri uri, boolean notifyForDescendants,
            ContentObserver observer, int userHandle) {
        mResolver.registerContentObserver(uri, notifyForDescendants, observer, userHandle);
    }
}
