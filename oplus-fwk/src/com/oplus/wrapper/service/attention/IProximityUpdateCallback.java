/*
 * SPDX-FileCopyrightText: 2026 The PenguinOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.oplus.wrapper.service.attention;

import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

/** Oplus wrapper around {@link android.service.attention.IProximityUpdateCallback}. */
public interface IProximityUpdateCallback {
    void onProximityUpdate(double distance) throws RemoteException;

    abstract class Stub implements IInterface, IProximityUpdateCallback {
        private final android.service.attention.IProximityUpdateCallback mTarget =
                new android.service.attention.IProximityUpdateCallback.Stub() {
                    @Override
                    public void onProximityUpdate(double distance) throws RemoteException {
                        com.oplus.wrapper.service.attention.IProximityUpdateCallback.Stub.this.onProximityUpdate(distance);
                    }
                };

        public Stub() {
        }

        public static IProximityUpdateCallback asInterface(IBinder binder) {
            if (binder == null) {
                return null;
            }
            return new Proxy(
                    android.service.attention.IProximityUpdateCallback.Stub.asInterface(binder));
        }

        @Override
        public IBinder asBinder() {
            return mTarget.asBinder();
        }

        static class Proxy implements IInterface, IProximityUpdateCallback {
            private final android.service.attention.IProximityUpdateCallback mTarget;

            Proxy(android.service.attention.IProximityUpdateCallback target) {
                mTarget = target;
            }

            @Override
            public IBinder asBinder() {
                return mTarget.asBinder();
            }

            @Override
            public void onProximityUpdate(double distance) throws RemoteException {
                mTarget.onProximityUpdate(distance);
            }
        }
    }
}
