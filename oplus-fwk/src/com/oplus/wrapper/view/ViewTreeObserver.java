package com.oplus.wrapper.view;

import android.graphics.Rect;
import android.graphics.Region;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Map;
import java.util.WeakHashMap;

// COUI bottom sheets register here to claim a touchable region outside their own bounds.
// The AOSP counterparts are all @hide, so everything crosses over by reflection: that
// keeps this compiling against the public API while still driving the real listener, and
// degrades to a no-op rather than throwing if a future platform moves them.
public class ViewTreeObserver {

    private static final Map<OnComputeInternalInsetsListener, Object> sProxies =
            new WeakHashMap<>();

    private final android.view.ViewTreeObserver mBase;

    public ViewTreeObserver(android.view.ViewTreeObserver base) {
        mBase = base;
    }

    public interface OnComputeInternalInsetsListener {
        void onComputeInternalInsets(InternalInsetsInfo inoutInfo);
    }

    public static final class InternalInsetsInfo {

        public static final int TOUCHABLE_INSETS_FRAME = 0;
        public static final int TOUCHABLE_INSETS_CONTENT = 1;
        public static final int TOUCHABLE_INSETS_VISIBLE = 2;
        public static final int TOUCHABLE_INSETS_REGION = 3;

        private final Object mInfo;

        InternalInsetsInfo(Object info) {
            mInfo = info;
        }

        public Rect getContentInsets() {
            Object v = field("contentInsets");
            return v instanceof Rect ? (Rect) v : new Rect();
        }

        public Rect getVisibleInsets() {
            Object v = field("visibleInsets");
            return v instanceof Rect ? (Rect) v : new Rect();
        }

        public Region getTouchableRegion() {
            Object v = field("touchableRegion");
            return v instanceof Region ? (Region) v : new Region();
        }

        public void setTouchableInsets(int val) {
            if (mInfo == null) return;
            try {
                mInfo.getClass().getMethod("setTouchableInsets", int.class).invoke(mInfo, val);
            } catch (ReflectiveOperationException ignored) {
            }
        }

        private Object field(String name) {
            if (mInfo == null) return null;
            try {
                Field f = mInfo.getClass().getField(name);
                return f.get(mInfo);
            } catch (ReflectiveOperationException ignored) {
                return null;
            }
        }
    }

    public void addOnComputeInternalInsetsListener(final OnComputeInternalInsetsListener listener) {
        if (mBase == null || listener == null) return;
        try {
            Class<?> aospListener = Class.forName(
                    "android.view.ViewTreeObserver$OnComputeInternalInsetsListener");
            Object proxy;
            synchronized (sProxies) {
                proxy = sProxies.get(listener);
                if (proxy == null) {
                    proxy = Proxy.newProxyInstance(aospListener.getClassLoader(),
                            new Class<?>[] { aospListener }, new InvocationHandler() {
                                @Override
                                public Object invoke(Object p, Method m, Object[] args) {
                                    if ("onComputeInternalInsets".equals(m.getName())
                                            && args != null && args.length == 1) {
                                        listener.onComputeInternalInsets(
                                                new InternalInsetsInfo(args[0]));
                                    }
                                    return null;
                                }
                            });
                    sProxies.put(listener, proxy);
                }
            }
            android.view.ViewTreeObserver.class
                    .getMethod("addOnComputeInternalInsetsListener", aospListener)
                    .invoke(mBase, proxy);
        } catch (ReflectiveOperationException ignored) {
        }
    }

    public void removeOnComputeInternalInsetsListener(OnComputeInternalInsetsListener victim) {
        if (mBase == null || victim == null) return;
        Object proxy;
        synchronized (sProxies) {
            proxy = sProxies.remove(victim);
        }
        if (proxy == null) return;
        try {
            Class<?> aospListener = Class.forName(
                    "android.view.ViewTreeObserver$OnComputeInternalInsetsListener");
            android.view.ViewTreeObserver.class
                    .getMethod("removeOnComputeInternalInsetsListener", aospListener)
                    .invoke(mBase, proxy);
        } catch (ReflectiveOperationException ignored) {
        }
    }
}
