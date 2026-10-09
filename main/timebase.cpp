#include "timebase.h"

#include <stdlib.h>

#include "clock.h"
#include "config.h"
#include "log.h"
#include "persist.h"
#include "vt_buildtime.h"

namespace {

// Odhad doby od spusteni buildu do prvniho startu desky (preklad + nahrani).
constexpr int64_t UPLOAD_DELAY_S = 30;

}  // namespace

void timebaseInit() {
    setenv("TZ", TZ_INFO, 1);
    tzset();
}

void timebaseColdStart(RtcState &s) {
    // Drift z minula - neni potreba ho 1-2 okna znovu ucit.
    uint32_t drift;
    if (persistGetU32("drift", drift)) {
        s.h.drift_ppb = int32_t(drift);
        LOG("[time] drift z NVS: %ld ppm", (long)(s.h.drift_ppb / 1000));
    }

    // Build, ze ktereho uz byl cas odhadnut, si pamatujeme v NVS - zapis jen
    // jednou za nahrani FW, ne pri kazdem probuzeni.
    uint32_t stamp = 0;
    persistGetU32("build", stamp);
    if (stamp != uint32_t(VT_BUILD_UNIX)) {
        s.h.sync_mono_us = clockNow();
        s.h.sync_unix_us = (int64_t(VT_BUILD_UNIX) + UPLOAD_DELAY_S) * 1000000LL;
        s.h.flags |= FLAG_TIME_APPROX;
        persistSetU32("build", uint32_t(VT_BUILD_UNIX));
        LOG("[time] novy FW - cas odhadnut z buildu (unix %lu)",
            (unsigned long)(VT_BUILD_UNIX + UPLOAD_DELAY_S));
    } else {
        LOG("[time] cas neznamy do prvniho NTP (studeny start bez noveho FW)");
    }
}

bool timeValid(const RtcState &s) {
    return s.h.flags & FLAG_TIME_VALID;
}

int64_t monoToUnixUs(const RtcState &s, uint64_t mono_us) {
    int64_t d = int64_t(mono_us) - int64_t(s.h.sync_mono_us);
    return s.h.sync_unix_us + d + d * s.h.drift_ppb / 1000000000LL;
}

uint64_t unixToMonoUs(const RtcState &s, int64_t unix_us) {
    int64_t d = unix_us - s.h.sync_unix_us;
    // d_mono = d / (1 + drift) ~ d - d * drift (drift je maly)
    return uint64_t(int64_t(s.h.sync_mono_us) + d - d * s.h.drift_ppb / 1000000000LL);
}

void timeSync(RtcState &s, uint64_t mono_us, int64_t unix_us) {
    char buf[32];
    formatLocal(unix_us, buf, sizeof buf);

    if (!timeValid(s)) {
        // Odhad z buildu se nepocita - drift se meri jen mezi dvema NTP.
        LOG("[time] prvni sync: %s, mono %lu.%03lu s", buf, (unsigned long)(mono_us / 1000000),
            (unsigned long)(mono_us / 1000 % 1000));
    } else {
        int64_t predicted = monoToUnixUs(s, mono_us);
        int64_t err_ms = (predicted - unix_us) / 1000;

        int64_t d_mono = int64_t(mono_us - s.h.sync_mono_us);
        int64_t d_real = unix_us - s.h.sync_unix_us;
        int32_t old_ppb = s.h.drift_ppb;
        if (d_mono > int64_t(DRIFT_MIN_SPAN_US)) {
            int64_t ppb = (d_real - d_mono) * 1000000000LL / d_mono;
            // Nerealny drift = skok v mono case. Offset opravi sync, drift ne.
            if (ppb > -DRIFT_MAX_PPB && ppb < DRIFT_MAX_PPB) {
                s.h.drift_ppb = int32_t(ppb);
                persistSetU32("drift", uint32_t(s.h.drift_ppb));
            } else {
                LOG("[time] drift %ld ppm mimo rozsah, ponechan puvodni", (long)(ppb / 1000));
            }
        }
        LOG("[time] sync: %s, za %lu s odhad %s o %ld ms, drift %ld -> %ld ppm", buf,
            (unsigned long)(d_real / 1000000), err_ms >= 0 ? "napred" : "pozadu",
            (long)(err_ms >= 0 ? err_ms : -err_ms), (long)(old_ppb / 1000),
            (long)(s.h.drift_ppb / 1000));
    }

    s.h.sync_mono_us = mono_us;
    s.h.sync_unix_us = unix_us;
    s.h.flags |= FLAG_TIME_VALID;
    s.h.flags &= ~FLAG_TIME_APPROX;
}

static bool isSyncHour(int hour, bool usb) {
    if (usb) return hour % SYNC_EVERY_H_USB == 0;
    for (uint8_t h : SYNC_HOURS_BATTERY) {
        if (h == hour) return true;
    }
    return false;
}

int64_t nextSyncWindowUnix(int64_t unix_s, bool usb) {
    constexpr int64_t MIN_GAP_S = 3600;
    time_t now = time_t(unix_s);
    for (int day = 0; day < 3; day++) {
        for (int hour = 0; hour < 24; hour++) {
            if (!isSyncHour(hour, usb)) continue;
            struct tm lt;
            localtime_r(&now, &lt);
            lt.tm_mday += day;
            lt.tm_hour = hour;
            lt.tm_min = 0;
            lt.tm_sec = 0;
            lt.tm_isdst = -1;
            int64_t t = mktime(&lt);
            if (t >= unix_s + MIN_GAP_S) return t;
        }
    }
    return unix_s + 8 * 3600;  // nemelo by nastat
}

bool timebaseLocal(const RtcState &s, struct tm &out, bool &approx) {
    if (!(s.h.flags & (FLAG_TIME_VALID | FLAG_TIME_APPROX))) return false;
    approx = !timeValid(s);
    time_t t = time_t(monoToUnixUs(s, clockNow()) / 1000000);
    return localtime_r(&t, &out) != nullptr;
}

void formatLocal(int64_t unix_us, char *buf, size_t len) {
    time_t t = time_t(unix_us / 1000000);
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &lt);
}
