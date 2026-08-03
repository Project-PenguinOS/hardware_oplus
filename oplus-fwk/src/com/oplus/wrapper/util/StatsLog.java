package com.oplus.wrapper.util;

/**
 * Sink for the events built by {@link StatsEvent}.
 *
 * Stock forwards these to statsd. Nothing here registers the atoms AIUnit writes,
 * so the record is dropped rather than pushed off-device. See StatsEvent for why
 * discarding is deliberate.
 */
public final class StatsLog {

    private StatsLog() {
    }

    public static void write(StatsEvent event) {
        // Intentionally empty -- see StatsEvent javadoc.
    }
}
