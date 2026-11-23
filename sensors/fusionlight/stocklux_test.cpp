// Host scenario test for the stock lux pipeline port.
#include "StockLuxPipeline.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace android::hardware::sensors::V2_1::subhal::implementation::fusionlight::stocklux;

struct Out {
    Millis time;
    float lux;
};

struct Harness {
    Millis now = 0;
    std::vector<Out> outputs;
    Pipeline pipeline;

    Harness() : pipeline(Features(), [this](float lux) { outputs.push_back({now, lux}); }) {
        pipeline.setActive(now, true, true);
        pipeline.onProximity(now, false);
        pipeline.onWifi(now, true);
        pipeline.onDarkMode(now, false);
        pipeline.onTime(now, 143000);
    }

    // Main sensor every 250 ms with the given lux function of time (ms since start of run).
    void run(Millis duration, const std::function<float(Millis)>& lux) {
        const Millis start = now;
        for (Millis t = 0; t < duration; t += 50) {
            now = start + t;
            if (t % 250 == 0) {
                pipeline.onMainLux(now, lux(t));
            }
            pipeline.tick(now);
        }
        now = start + duration;
    }

    float last() const { return outputs.empty() ? NAN : outputs.back().lux; }
    // First output time at or after `since` with lux crossing the given level.
    Millis firstReach(Millis since, float level, bool up) const {
        for (const auto& o : outputs) {
            if (o.time >= since && (up ? o.lux >= level : o.lux <= level)) {
                return o.time - since;
            }
        }
        return -1;
    }
    size_t countSince(Millis since) const {
        size_t n = 0;
        for (const auto& o : outputs) n += o.time >= since;
        return n;
    }
};

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

int main() {
    {
        Harness h;
        h.run(20000, [](Millis) { return 120.0f; });
        check(std::fabs(h.last() - 120.0f) < 0.5f, "steady 120 lux settles at 120");
        const Millis t0 = h.now;
        // +-4% sample-to-sample flicker must not move the reported lux.
        h.run(20000, [](Millis t) { return (t / 250) % 2 ? 124.8f : 115.2f; });
        std::printf("  flicker: %zu outputs, last %.2f\n", h.countSince(t0), h.last());
        check(h.countSince(t0) <= 2, "+-4% flicker gives at most 2 updates");
        const Millis t1 = h.now;
        h.run(10000, [](Millis) { return 1000.0f; });
        const Millis up = h.firstReach(t1, 900.0f, true);
        std::printf("  step 120->1000: reached 900 after %lld ms, last %.1f\n",
                    static_cast<long long>(up), h.last());
        check(up >= 0 && up <= 1000, "big brightening step reacts within 1 s");
        const Millis t2 = h.now;
        h.run(10000, [](Millis) { return 50.0f; });
        const Millis down = h.firstReach(t2, 60.0f, false);
        std::printf("  step 1000->50: reached 60 after %lld ms, last %.1f\n",
                    static_cast<long long>(down), h.last());
        check(down >= 0 && down <= 2000, "big darkening step reacts within 2 s");
        const Millis t3 = h.now;
        // Slow drift of 10% should eventually be followed (stable force change) or held.
        h.run(15000, [](Millis) { return 55.0f; });
        std::printf("  drift 50->55: %zu outputs, last %.2f, scene %d\n", h.countSince(t3),
                    h.last(), static_cast<int>(h.pipeline.scene()));
    }
    {
        Harness h;
        h.run(10000, [](Millis) { return 30.0f; });
        h.pipeline.onProximity(h.now, true);
        const Millis t0 = h.now;
        h.run(5000, [](Millis) { return 2.0f; });
        std::printf("  proximity near + drop to 2: last %.2f\n", h.last());
        check(h.last() >= 29.0f, "proximity near holds lux when it drops");
        h.pipeline.onProximity(h.now, false);
        h.run(5000, [](Millis) { return 2.0f; });
        check(h.last() <= 5.0f, "proximity far releases the drop");
        (void)t0;
    }
    {
        Harness h;
        h.run(5000, [](Millis) { return 10.0f; });
        // Rear sensor reads much brighter (phone face up under a lamp, front shaded).
        const Millis start = h.now;
        for (Millis t = 0; t < 10000; t += 25) {
            h.now = start + t;
            if (t % 250 == 0) h.pipeline.onMainLux(h.now, 10.0f);
            if (t % 250 == 125) h.pipeline.onAuxLux(h.now, 400.0f);
            h.pipeline.tick(h.now);
        }
        std::printf("  main 10, aux 400: last %.2f (stock aux conversion = 10 + min(0.25*396, "
                    "16*e^0.1+4) = 31.68)\n", h.last());
        check(std::fabs(h.last() - 31.68f) < 0.05f, "rear sensor raises lux to the stock blend");
    }
    {
        Harness h;
        h.run(5000, [](Millis) { return 5.0f; });
        h.run(30000, [](Millis) { return 0.0f; });
        std::printf("  darkness 30 s: last %.2f\n", h.last());
        check(h.last() <= 1.0f, "darkness goes to ~0 lux");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
