#pragma once

// Odeslani neodeslanych vzorku na ThingSpeak (bulk_update.csv), viz web.md
// puvodniho projektu, kap. 2 a 4.
//
// Kazdy platny vzorek = jeden zaznam s unix casem (store.h ho prepocita
// linearne mezi syncy). Davky po max. TS_BATCH_MAX zaznamech, mezi nimi pauza
// TS_BATCH_SPACING_MS (limit ThingSpeak). Vic davek jen po dlouhem vypadku
// (> 32 h dat); slot mereni, ktery pripadne do odesilani, zustane prazdny.
// Diagnostika (field2..6 + status) se pripoji k poslednimu zaznamu posledni
// davky; kdyz neni zadny platny vzorek, posle se jen ona (cas = ted).
//
// ThingSpeak zaznam se stejnym casem tise zahodi, takze opakovane odeslani
// po neuspechu nevytvori duplicity.

#include <stdint.h>

#include "rtc_state.h"

struct UploadDiag {
    int8_t rssi;
    uint8_t failed_windows;
    uint8_t err_count;
    int32_t drift_ppb;
    int battery;  // %, -1 = nezname
    uint8_t reset_reason;  // esp_reset_reason_t
};

// true = vsechny davky vratily HTTP 202. Buffer nemaze - to dela volajici.
bool uploadAll(const RtcState &s, const UploadDiag &diag, int64_t now_unix_us);
