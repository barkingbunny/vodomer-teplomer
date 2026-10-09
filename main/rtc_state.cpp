#include "rtc_state.h"

#include <stddef.h>
#include <string.h>

#include "esp_attr.h"

#include "config.h"
#include "log.h"

namespace {

constexpr uint32_t RTC_MAGIC = 0x56543147;  // "VT1G" - pri zmene layoutu zvysit

// Uloziste v RTC slow pameti - prezije deep sleep, maze se power-onem.
RTC_DATA_ATTR RtcState rtc_store;

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

uint32_t stateCrc(const RtcState &s) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
    constexpr size_t skip = offsetof(RtcHeader, crc) + sizeof(uint32_t);
    return crc32(p + skip, sizeof(RtcState) - skip);
}

}  // namespace

bool rtcLoad(RtcState &s) {
    if (rtc_store.h.magic != RTC_MAGIC || rtc_store.h.crc != stateCrc(rtc_store)) return false;
    s = rtc_store;
    return true;
}

void rtcSave(RtcState &s) {
    s.h.magic = RTC_MAGIC;
    s.h.crc = stateCrc(s);
    rtc_store = s;
}

void rtcInit(RtcState &s) {
    memset(&s, 0, sizeof s);
}

void rtcInvalidate() {
    rtc_store.h.magic = 0;
}

void rtcPushSample(RtcState &s, uint64_t t_mono_us, int16_t raw) {
    if (s.h.count == 0) {
        s.h.buf_start_mono_us = t_mono_us;
    }

    // Pred zacatkem bufferu se nic neuklada (nemelo by nastat).
    int64_t rel = int64_t(t_mono_us - s.h.buf_start_mono_us) + int64_t(SAMPLE_PERIOD_US / 2);
    if (rel < 0) return;

    uint32_t slot = uint64_t(rel) / SAMPLE_PERIOD_US;

    if (slot < s.h.count) {
        // Stejny slot podruhe - nechame novejsi hodnotu, pokud je platna.
        if (raw != SAMPLE_MISSING) s.samples[slot] = raw;
        return;
    }

    // Posunout okno (24h kruh pro graf), nejstarsi vzorky vypadnou. Ztrata je
    // jen u tech, ktere jeste nebyly ve flash ani odeslane (flash nesla).
    if (slot >= RTC_SAMPLES) {
        uint32_t shift = slot - RTC_SAMPLES + 1;
        if (shift > s.h.count) shift = s.h.count;
        uint32_t lost = shift > s.h.persisted ? shift - s.h.persisted : 0;
        memmove(s.samples, s.samples + shift, (s.h.count - shift) * sizeof(int16_t));
        s.h.count -= shift;
        s.h.persisted = s.h.persisted > shift ? s.h.persisted - shift : 0;
        s.h.buf_start_mono_us += uint64_t(shift) * SAMPLE_PERIOD_US;
        slot -= shift;
        if (slot >= RTC_SAMPLES) {
            // Mezera delsi nez cely buffer - zacit znovu od tohoto vzorku.
            s.h.buf_start_mono_us += uint64_t(slot) * SAMPLE_PERIOD_US;
            slot = 0;
        }
        if (lost) {
            s.h.dropped += lost;
            LOG("[rtc] neulozene vzorky vypadly z bufferu: %u (celkem %u)", unsigned(lost),
                s.h.dropped);
        }
    }

    while (s.h.count < slot) s.samples[s.h.count++] = SAMPLE_MISSING;
    s.samples[s.h.count++] = raw;
}
