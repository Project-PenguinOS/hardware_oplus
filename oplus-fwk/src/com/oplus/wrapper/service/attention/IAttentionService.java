/*
 * SPDX-FileCopyrightText: 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.oplus.wrapper.service.attention;

import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

/**
 * Oplus wrapper around {@link android.service.attention.IAttentionService}. AONService
 * implements its attention service on this Stub; the binder it hands out is a plain
 * AOSP IAttentionService.
 */
public interface IAttentionService {
    void checkAttention(IAttentionCallback callback) throws RemoteException;

    void cancelAttentionCheck(IAttentionCallback callback) throws RemoteException;

    void onStartProximityUpdates(IProximityUpdateCallback callback) throws RemoteException;

    void onStopProximityUpdates() throws RemoteException;

    abstract class Stub implements IInterface, IAttentionService {
        private final android.service.attention.IAttentionService mTarget =
                new android.service.attention.IAttentionService.Stub() {
                    @Override
                    public void checkAttention(
                            android.service.attention.IAttentionCallback callback)
                            throws RemoteException {
                        com.oplus.wrapper.service.attention.IAttentionService.Stub.this.checkAttention(wrap(callback));
                    }

                    @Override
                    public void cancelAttentionCheck(
                            android.service.attention.IAttentionCallback callback)
                            throws RemoteException {
                        com.oplus.wrapper.service.attention.IAttentionService.Stub.this.cancelAttentionCheck(wrap(callback));
                    }

                    @Override
                    public void onStartProximityUpdates(
                            android.service.attention.IProximityUpdateCallback callback)
                            throws RemoteException {
                        com.oplus.wrapper.service.attention.IAttentionService.Stub.this.onStartProximityUpdates(callback == null ? null
                                : new IProximityUpdateCallback.Stub.Proxy(callback));
                    }

                    @Override
                    public void onStopProximityUpdates() throws RemoteException {
                        com.oplus.wrapper.service.attention.IAttentionService.Stub.this.onStopProximityUpdates();
                    }
                };

        public Stub() {
        }

        private static IAttentionCallback wrap(
                android.service.attention.IAttentionCallback callback) {
            return callback == null ? null : new IAttentionCallback.Stub.Proxy(callback);
        }

        public static IAttentionService asInterface(IBinder binder) {
            if (binder == null) {
                return null;
            }
            return new Proxy(android.service.attention.IAttentionService.Stub.asInterface(binder));
        }

        @Override
        public IBinder asBinder() {
            return mTarget.asBinder();
        }

        static class Proxy implements IInterface, IAttentionService {
            private final android.service.attention.IAttentionService mTarget;

            Proxy(android.service.attention.IAttentionService target) {
                mTarget = target;
            }

            @Override
            public IBinder asBinder() {
                return mTarget.asBinder();
            }

            @Override
            public void checkAttention(IAttentionCallback callback) throws RemoteException {
                mTarget.checkAttention(unwrap(callback));
            }

            @Override
            public void cancelAttentionCheck(IAttentionCallback callback) throws RemoteException {
                mTarget.cancelAttentionCheck(unwrap(callback));
            }

            @Override
            public void onStartProximityUpdates(IProximityUpdateCallback callback)
                    throws RemoteException {
                mTarget.onStartProximityUpdates(callback == null ? null
                        : android.service.attention.IProximityUpdateCallback.Stub.asInterface(
                                ((IInterface) callback).asBinder()));
            }

            @Override
            public void onStopProximityUpdates() throws RemoteException {
                mTarget.onStopProximityUpdates();
            }

            private static android.service.attention.IAttentionCallback unwrap(
                    IAttentionCallback callback) {
                return callback == null ? null
                        : android.service.attention.IAttentionCallback.Stub.asInterface(
                                ((IInterface) callback).asBinder());
            }
        }
    }
}
