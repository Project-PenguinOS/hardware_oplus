package com.oplus.wrapper.os;

/* Wrapper over the hidden Parcel calls. */
public class Parcel {
    private final android.os.Parcel mParcel;

    public Parcel(android.os.Parcel parcel) {
        mParcel = parcel;
    }

    public String[] readStringArray() {
        return mParcel.readStringArray();
    }
}
