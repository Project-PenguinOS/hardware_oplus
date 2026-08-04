package android.os.customize;

import android.content.ComponentName;
import android.content.Context;

/**
 * The ColorOS enterprise-customization restriction query. Gallery wraps it in
 * com.heytap.addon.os.customize.OplusCustomizeRestrictionManager and calls
 * straight through, so with the framework class absent the wrapper's getInstance
 * throws NoClassDefFoundError.
 *
 * The reachable path is OtherSystemStorage -> OplusCustomizeRestrictionManagerWrapper:
 *
 *   fun isPrivateSafeDisabled(context: Context): Boolean =
 *       OplusCustomizeRestrictionManager.getInstance(context).isPrivateSafeDisabled()
 *
 * isPrivateSafeDisabled returns true rather than the false a stock unrestricted
 * device would report. The question being asked is whether Private Safe is
 * unavailable, and here it genuinely is -- it is a ColorOS storage location that
 * this build does not have. Answering true keeps Gallery from offering it as a
 * destination or trying to enumerate it; answering false would advertise a
 * storage backend that is not there.
 *
 * The float-task pair and getForbidRecordScreenState are the ordinary "no
 * customization applied" answers: nothing is restricted, and setting a
 * restriction does not take. All four are referenced by oplus-services.jar,
 * OplusCamera, VideoGallery and OppoGallery2.
 *
 * Not stubbed here: the sibling OplusCustomizeContactManager, which Gallery also
 * references. It is equally absent, but nothing on the AI paths was observed
 * reaching it, and its getters feed phone-number masking where a wrong default
 * would change behaviour rather than just avoid a crash. Left out until there is
 * a call path to justify a value.
 */
public class OplusCustomizeRestrictionManager {

    private static OplusCustomizeRestrictionManager sInstance;

    public static synchronized OplusCustomizeRestrictionManager getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new OplusCustomizeRestrictionManager();
        }
        return sInstance;
    }

    public boolean isPrivateSafeDisabled() {
        return true;
    }

    public boolean getForbidRecordScreenState() {
        return false;
    }

    public boolean isFloatTaskDisabled(ComponentName componentName) {
        return false;
    }

    public boolean setFloatTaskDisabled(ComponentName componentName, boolean disabled) {
        return false;
    }
}
