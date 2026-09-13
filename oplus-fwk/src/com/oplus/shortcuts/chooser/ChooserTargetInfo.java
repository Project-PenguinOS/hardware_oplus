package com.oplus.shortcuts.chooser;

import java.util.ArrayList;
import java.util.Arrays;

public abstract class ChooserTargetInfo implements TargetInfo {

    @Override
    public final boolean isChooserTargetInfo() {
        return true;
    }

    @Override
    public ArrayList<DisplayResolveInfo> getAllDisplayTargets() {
        if (getDisplayResolveInfo() == null) {
            return new ArrayList<>();
        }
        return new ArrayList<>(Arrays.asList(getDisplayResolveInfo()));
    }
}
