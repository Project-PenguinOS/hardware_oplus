package com.oplus.wrapper.util;

/**
 * Stand-in for ColorOS's wrapper around android.util.StatsEvent.
 *
 * AIUnit reports download telemetry through
 * com.oplus.aiunit.statistical.quality.AISystemReporter, which builds one of
 * these for every state change of a SimpleDownloadTask. The class was missing, so
 * installing a downloaded model blew up with NoClassDefFoundError inside the
 * download task's state dispatcher -- the plugin downloaded but never installed.
 *
 * The builder accepts everything and discards it. The payload is Oplus's own
 * usage analytics: there is no statsd atom registered for it here, the DCS
 * pipeline already rejects the records ("track data [unit_file_download] is
 * illegal!"), and it is not something this ROM should be shipping off-device
 * anyway. Only the *flow* matters -- the builder has to chain and build() has to
 * return non-null so the reporter completes and the install proceeds.
 */
public final class StatsEvent {

    public static final class Builder {

        Builder() {
        }

        public Builder setAtomId(int atomId) {
            return this;
        }

        public Builder writeInt(int value) {
            return this;
        }

        public Builder writeLong(long value) {
            return this;
        }

        public Builder writeFloat(float value) {
            return this;
        }

        public Builder writeString(String value) {
            return this;
        }

        public Builder usePooledBuffer() {
            return this;
        }

        public StatsEvent build() {
            return new StatsEvent();
        }
    }

    public StatsEvent() {
    }

    public static Builder newBuilder() {
        return new Builder();
    }
}
