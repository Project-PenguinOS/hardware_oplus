package com.oplus.shortcuts;

import android.content.Intent;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.os.Bundle;
import android.service.chooser.ChooserTarget;

import com.oplus.wrapper.app.prediction.AppTarget;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;

class ShortcutToChooserTargetConverter {

    public List<ChooserTarget> convertToChooserTarget(
            List<ShortcutManager.ShareShortcutInfo> matchingShortcuts,
            List<ShortcutManager.ShareShortcutInfo> allShortcuts,
            List<AppTarget> allAppTargets,
            Map<ChooserTarget, AppTarget> directShareAppTargetCache,
            Map<ChooserTarget, ShortcutInfo> directShareShortcutInfoCache) {
        boolean isFromAppPredictor = allAppTargets != null;
        List<Integer> scoreList = new ArrayList<>();
        if (!isFromAppPredictor) {
            for (int i = 0; i < matchingShortcuts.size(); i++) {
                int shortcutRank = matchingShortcuts.get(i).getShortcutInfo().getRank();
                if (!scoreList.contains(shortcutRank)) {
                    scoreList.add(shortcutRank);
                }
            }
            Collections.sort(scoreList);
        }

        List<ChooserTarget> chooserTargetList = new ArrayList<>(matchingShortcuts.size());
        for (int i = 0; i < matchingShortcuts.size(); i++) {
            ShortcutInfo shortcutInfo = matchingShortcuts.get(i).getShortcutInfo();
            int indexInAllShortcuts = allShortcuts.indexOf(matchingShortcuts.get(i));

            float score;
            if (isFromAppPredictor) {
                score = Math.max(1.0f - (0.01f * indexInAllShortcuts), 0.0f);
            } else {
                int rankIndex = scoreList.indexOf(shortcutInfo.getRank());
                score = Math.max(1.0f - (0.01f * rankIndex), 0.0f);
            }

            Bundle extras = new Bundle();
            extras.putString(Intent.EXTRA_SHORTCUT_ID, shortcutInfo.getId());
            ChooserTarget chooserTarget = new ChooserTarget(shortcutInfo.getLabel(), null, score,
                    matchingShortcuts.get(i).getTargetComponent().clone(), extras);
            chooserTargetList.add(chooserTarget);

            if (directShareAppTargetCache != null && allAppTargets != null) {
                directShareAppTargetCache.put(chooserTarget, allAppTargets.get(indexInAllShortcuts));
            }
            if (directShareShortcutInfoCache != null) {
                directShareShortcutInfoCache.put(chooserTarget, shortcutInfo);
            }
        }

        Comparator<ChooserTarget> byScore =
                (a, b) -> -Float.compare(a.getScore(), b.getScore());
        Collections.sort(chooserTargetList, byScore);
        return chooserTargetList;
    }
}
