package com.android.id.impl;

import android.content.Context;
import android.provider.Settings;
import android.text.TextUtils;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Stand-in for ColorOS's device identifier provider.
 *
 * StdID (com.oplus.stdid) reflects this class in its static initialiser. On a
 * stock ColorOS build it is supplied by the platform; this tree does not have it,
 * so the lookup threw NoClassDefFoundError -- an Error, not an Exception, so
 * StdID's try/catch did not save it. The manager stayed disabled and every ID API
 * returned empty with ":3004" / "DUID:ret:F".
 *
 * That matters well beyond StdID: AIUnit puts the DUID in the Odin request header
 * set, and the cloud AI endpoints reject an inference call whose device headers
 * are incomplete (error 3000404). So a missing class here shows up as "AI Clarity
 * / Reflection / Composition silently fail".
 *
 * The identifiers are synthesised from ANDROID_ID rather than from any real
 * hardware identifier. They are stable for the life of the install and distinct
 * per identifier type, which is everything the callers actually require, and they
 * deliberately carry no device-identifying information -- this ROM should not be
 * minting trackable hardware IDs for an analytics pipeline.
 *
 * Call-site contract, read off StdID's smali: every getter is reached with
 * invoke-virtual followed by move-result-object and the value is used without a
 * null check, so these must never return null. Returning an empty string is
 * likewise unsafe; the callers treat empty as failure.
 */
public class IdProviderImpl {

    private static final String FALLBACK_SEED = "alpha-oplus-fwk";

    public IdProviderImpl() {
    }

    public boolean checkGetGUID(Context context) {
        return true;
    }

    public boolean checkGetAPID(Context context) {
        return true;
    }

    public String getAUID(Context context) {
        return derive(context, "AUID");
    }

    public String getGUID(Context context) {
        return derive(context, "GUID");
    }

    /**
     * Required by name and signature, not merely by behaviour. StdID's static
     * initialiser gates its entire manager on this exact reflective lookup
     * (com.oplus.stdid.manager.a.<clinit>):
     *
     *   Class c = Class.forName("com.android.id.impl.IdProviderImpl");
     *   Method m = c.getDeclaredMethod("getOUID", Context.class);
     *   enabled = (m != null);
     *
     * With the class present but this one method absent, the lookup throws
     * NoSuchMethodException, StdID logs "3014" and leaves enabled=false -- after
     * which every getter short-circuits to "" and logs "<pkg>:3004". That is what
     * produced, on 2026-08-04:
     *
     *   E StdID: com.oplus.aiunit:3004
     *   E StdID: com.oplus.aiunit:DUID:ret:F
     *   E AIUnit-Plugin-ImageDeglareEngine: [ProcessResult] error =
     *       {"message":"duid can not empty.","code":805,"specificCode":5}
     *
     * So AI Glare reached Oplus's servers and was refused purely for an empty
     * device header. Adding the method is what switches the manager on; the
     * returned value only has to be non-empty and stable.
     */
    public String getOUID(Context context) {
        return derive(context, "OUID");
    }

    /**
     * The DUID path. StdID does not reflect for this one -- it is reached through
     * the ordinary dispatch once the manager is enabled -- but it is the value
     * AIUnit actually puts in the cloud request header, so it must be present and
     * non-empty for the same reason.
     */
    public String getDUID(Context context) {
        return derive(context, "DUID");
    }

    /**
     * Generic accessor. StdID calls this with the identifier name, e.g. "APID";
     * the DUID/OUID paths route through here too.
     */
    public String getStdid(Context context, String type) {
        return derive(context, TextUtils.isEmpty(type) ? "STDID" : type);
    }

    private static String derive(Context context, String type) {
        String seed = null;
        if (context != null) {
            try {
                seed = Settings.Secure.getString(
                        context.getContentResolver(), Settings.Secure.ANDROID_ID);
            } catch (RuntimeException ignored) {
                // No provider yet (very early boot) -- fall through to the constant.
            }
        }
        if (TextUtils.isEmpty(seed)) {
            seed = FALLBACK_SEED;
        }

        // Stock IDs are opaque fixed-width uppercase hex; match that shape so
        // nothing downstream trips on an unexpected format.
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hash = digest.digest((type + ':' + seed).getBytes(StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder(17);
            for (int i = 0; sb.length() < 17; i++) {
                sb.append(Character.forDigit((hash[i] >> 4) & 0xf, 16));
                sb.append(Character.forDigit(hash[i] & 0xf, 16));
            }
            return sb.substring(0, 17).toUpperCase();
        } catch (NoSuchAlgorithmException e) {
            // SHA-256 is mandatory on every Android platform; this cannot happen,
            // but a null return here would crash the caller.
            return (type + seed).hashCode() != 0
                    ? Integer.toHexString((type + seed).hashCode()).toUpperCase()
                    : FALLBACK_SEED;
        }
    }
}
