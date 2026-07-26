/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub aligned with stock com.oplus.osense.eventinfo.EventConfig
 * (oplus-framework.jar) for AIUnit OSensePublisher.
 */
package com.oplus.osense.eventinfo;

import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;

public class EventConfig implements Parcelable {
    private Set<Integer> mEventSet;
    private Set<OsenseConfig> mOsenseConfigSet;

    public static final Creator<EventConfig> CREATOR = new Creator<EventConfig>() {
        @Override
        public EventConfig createFromParcel(Parcel in) {
            return new EventConfig(in);
        }

        @Override
        public EventConfig[] newArray(int size) {
            return new EventConfig[size];
        }
    };

    public EventConfig() {
        mEventSet = new HashSet<>();
        mOsenseConfigSet = new HashSet<>();
    }

    public EventConfig(HashSet<Integer> events) {
        mEventSet = new HashSet<>();
        if (events != null && !events.isEmpty()) {
            mEventSet.addAll(events);
        }
        mOsenseConfigSet = new HashSet<>();
    }

    protected EventConfig(Parcel in) {
        ClassLoader loader = EventConfig.class.getClassLoader();
        ArrayList<Integer> eventList = new ArrayList<>();
        in.readList(eventList, loader);
        mEventSet = new HashSet<>();
        if (!eventList.isEmpty()) {
            mEventSet.addAll(eventList);
        }
        ArrayList<OsenseConfig> osenseList = new ArrayList<>();
        in.readList(osenseList, loader);
        mOsenseConfigSet = new HashSet<>();
        if (!osenseList.isEmpty()) {
            mOsenseConfigSet.addAll(osenseList);
        }
    }

    public void setOsenseConfigSet(HashSet<OsenseConfig> osenseConfigSet) {
        if (mOsenseConfigSet == null) {
            mOsenseConfigSet = new HashSet<>();
        }
        mOsenseConfigSet.clear();
        if (osenseConfigSet != null) {
            mOsenseConfigSet.addAll(osenseConfigSet);
        }
    }

    public Set<Integer> getEventSet() {
        if (mEventSet == null) {
            mEventSet = new HashSet<>();
        }
        return mEventSet;
    }

    public Set<OsenseConfig> getOsenseConfigSet() {
        if (mOsenseConfigSet == null) {
            mOsenseConfigSet = new HashSet<>();
        }
        return mOsenseConfigSet;
    }

    public Set<Integer> getAllEventTypes() {
        Set<Integer> all = new HashSet<>();
        if (mOsenseConfigSet != null) {
            for (OsenseConfig cfg : mOsenseConfigSet) {
                all.add(cfg.getEventType());
            }
        }
        if (mEventSet != null) {
            all.addAll(mEventSet);
        }
        return all;
    }

    public Set<OsenseConfig> getAllOsenseConfigs() {
        Set<OsenseConfig> all = new HashSet<>();
        if (mEventSet != null) {
            for (Integer eventType : mEventSet) {
                all.add(new OsenseConfig(eventType, (Bundle) null));
            }
        }
        if (mOsenseConfigSet != null) {
            all.addAll(mOsenseConfigSet);
        }
        return all;
    }

    public void addEvent(int eventType) {
        if (mEventSet == null) {
            mEventSet = new HashSet<>();
        }
        mEventSet.add(eventType);
    }

    public void addOsenseConfig(OsenseConfig osenseConfig) {
        if (mOsenseConfigSet == null) {
            mOsenseConfigSet = new HashSet<>();
        }
        mOsenseConfigSet.add(osenseConfig);
    }

    @Override
    public String toString() {
        return "EventConfig{mEventSet=" + mEventSet + ", mOsenseConfigSet=" + mOsenseConfigSet + '}';
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        if (mEventSet != null) {
            dest.writeList(new ArrayList<>(mEventSet));
        }
        if (mOsenseConfigSet != null) {
            dest.writeList(new ArrayList<>(mOsenseConfigSet));
        }
    }
}
