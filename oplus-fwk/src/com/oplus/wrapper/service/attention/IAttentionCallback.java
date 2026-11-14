/*
 * SPDX-FileCopyrightText: 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.oplus.wrapper.service.attention;

import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

/** Oplus wrapper around {@link android.service.attention.IAttentionCallback}. */
public interface IAttentionCallback {
    void onSuccess(int result, long timestamp) throws RemoteException;

    void onFailure(int error) throws RemoteException;

    abstract class Stub implements IInterface, IAttentionCallback {
        private final android.service.attention.IAttentionCallback mTarget =
                new android.service.attention.IAttentionCallback.Stub() {
                    @Override
                    public void onSuccess(int result, long timestamp) throws RemoteException {
                        com.oplus.wrapper.service.attention.IAttentionCallback.Stub.this.onSuccess(result, timestamp);
                    }

                    @Override
                    public void onFailure(int error) throws RemoteException {
                        com.oplus.wrapper.service.attention.IAttentionCallback.Stub.this.onFailure(error);
                    }
                };

        public Stub() {
        }

        public static IAttentionCallback asInterface(IBinder binder) {
            if (binder == null) {
                return null;
            }
            return new Proxy(android.service.attention.IAttentionCallback.Stub.asInterface(binder));
        }

        @Override
        public IBinder asBinder() {
            return mTarget.asBinder();
        }

        static class Proxy implements IInterface, IAttentionCallback {
            private final android.service.attention.IAttentionCallback mTarget;

            Proxy(android.service.attention.IAttentionCallback target) {
                mTarget = target;
            }

            @Override
            public IBinder asBinder() {
                return mTarget.asBinder();
            }

            @Override
            public void onSuccess(int result, long timestamp) throws RemoteException {
                mTarget.onSuccess(result, timestamp);
            }

            @Override
            public void onFailure(int error) throws RemoteException {
                mTarget.onFailure(error);
            }
        }
    }
}
