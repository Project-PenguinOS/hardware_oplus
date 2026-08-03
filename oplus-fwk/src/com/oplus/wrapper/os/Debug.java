package com.oplus.wrapper.os;

/**
 * Stand-in for ColorOS's wrapper around android.os.Debug.
 *
 * AIUnit uses getCallers() purely to decorate its own log lines. Unlike the
 * StatsEvent pair this one is genuinely reproducible from public API, so it
 * returns a real caller chain -- the logs stay useful for diagnosing AIUnit
 * rather than printing a placeholder.
 */
public final class Debug {

    private Debug() {
    }

    public static String getCallers(int depth) {
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();

        // 0 getStackTrace, 1 this method, 2 the caller asking -- start past those.
        final int start = 3;
        StringBuilder sb = new StringBuilder();
        for (int i = start; i < stack.length && i < start + depth; i++) {
            if (sb.length() > 0) {
                sb.append(' ');
            }
            sb.append(stack[i].getClassName())
                    .append('.')
                    .append(stack[i].getMethodName())
                    .append(':')
                    .append(stack[i].getLineNumber());
        }
        return sb.toString();
    }
}
