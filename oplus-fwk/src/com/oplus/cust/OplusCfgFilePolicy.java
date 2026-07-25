/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub for stock com.oplus.cust.OplusCfgFilePolicy (oplus-framework.jar).
 * AIUnit / Oplus cust loaders call these; return empty results on AOSP/Alpha.
 */
package com.oplus.cust;

import java.io.File;
import java.util.Collections;
import java.util.List;

public final class OplusCfgFilePolicy {
    public static final int DEFAULT_SLOT = -2;

    private OplusCfgFilePolicy() {}

    /** Stock: List<String> of cfg directory paths. */
    public static List<String> getCfgLevelList(String pathPrefix, int slotId) {
        return Collections.emptyList();
    }

    /** Stock: List<File> matching fileName under pathPrefix. */
    public static List<File> getCfgFileList(String fileName, String pathPrefix, int slotId) {
        return Collections.emptyList();
    }

    /** Stock: highest-priority matching File, or null. */
    public static File getCfgTopPriorityFile(String fileName, String pathPrefix, int slotId) {
        return null;
    }

    public static String getCarrierId(int slotId) {
        return "";
    }
}
