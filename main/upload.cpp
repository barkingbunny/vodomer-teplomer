#include "upload.h"

#include <stdio.h>

#include <string>
#include <vector>

#include "esp_http_client.h"
#include "esp_system.h"

#include "clock.h"
#include "config.h"
#include "log.h"
#include "store.h"
#include "vt_secrets.h"  // build adresar, tools/secrets.cmake

#ifndef TS_CHANNEL_ID
#error "TS_CHANNEL_ID chybi - doplnuje ho tools/secrets.cmake z vodomer-teplomer.env"
#endif
#ifndef TS_WRITE_KEY
#error "TS_WRITE_KEY chybi - doplnuje ho tools/secrets.cmake z vodomer-teplomer.env"
#endif

namespace {

#define STR2(x) #x
#define STR(x) STR2(x)

constexpr const char *TS_URL = "http://" TS_HOST "/channels/" STR(TS_CHANNEL_ID) "/bulk_update.csv";

// Jen znaky, ktere ve status textu nevadi ('+' = mezera v urlencoded).
const char *resetName(uint8_t reason) {
    switch (esp_reset_reason_t(reason)) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "ext-reset";
        case ESP_RST_SW:        return "soft-restart";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        default:                return "unknown";
    }
}

struct Record {
    int64_t unix_s;
    int16_t raw;
};

struct Ctx {
    const UploadDiag *diag;
    std::vector<Record> batch;
    uint32_t sent = 0;     // zaznamu v uz odeslanych davkach
    uint32_t batches = 0;
    bool failed = false;
};

// "ts,teplota" + diagnostika nebo prazdna pole - 13 sloupcu: cas, field1..8,
// lat, long, elev, status. Pole: 1 teplota, 2 RSSI, 3 neuspesna okna,
// 4 chybna mereni, 5 drift [s/den], 6 baterie [%], 7 a 8 volne (vodomer).
// Pocet odeslanych vzorku je jen v textu status ("... samples N").
void appendRecord(std::string &out, int64_t unix_s, const int16_t *raw, const UploadDiag *d,
                  unsigned valid) {
    char buf[128];
    if (raw) snprintf(buf, sizeof buf, "%lld,%.2f", (long long)unix_s, *raw / 16.0f);
    else snprintf(buf, sizeof buf, "%lld,", (long long)unix_s);
    out += buf;
    if (!d) {
        out += ",,,,,,,,,,,";
        return;
    }
    char bat[12] = "";
    if (d->battery >= 0) snprintf(bat, sizeof bat, "%d", d->battery);
    snprintf(buf, sizeof buf, ",%d,%u,%u,%.1f,%s,,,,,,FW+" FW_VERSION "+%s+samples+%u",
             int(d->rssi), d->failed_windows, d->err_count, d->drift_ppb * 86400.0 / 1e9, bat,
             resetName(d->reset_reason), valid);
    out += buf;
}

bool post(const std::string &body, unsigned records) {
    uint32_t t0 = uptimeMs();
    esp_http_client_config_t cfg = {};
    cfg.url = TS_URL;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = int(TS_TIMEOUT_MS);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(c, body.data(), int(body.size()));
    esp_err_t err = esp_http_client_perform(c);
    int code = err == ESP_OK ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);
    LOG("[web] %u zaznamu, %u B, HTTP %d za %lu ms", records, unsigned(body.size()), code,
        (unsigned long)(uptimeMs() - t0));
    return code == 202;  // 202 = prijato do fronty
}

// Odeslat davku; diag != nullptr = posledni davka, diagnostika k poslednimu zaznamu.
bool sendBatch(Ctx &c, const UploadDiag *diag, int64_t now_unix_s) {
    if (c.batches > 0) {
        LOG("[web] pauza %lu s mezi davkami (limit ThingSpeak)",
            (unsigned long)(TS_BATCH_SPACING_MS / 1000));
        delayMs(TS_BATCH_SPACING_MS);
    }
    std::string body;
    body.reserve(80 + c.batch.size() * 30);
    body += "write_api_key=" TS_WRITE_KEY "&time_format=absolute&updates=";
    unsigned total = c.sent + unsigned(c.batch.size());
    for (size_t i = 0; i < c.batch.size(); i++) {
        if (i) body += '|';
        bool last = diag && i + 1 == c.batch.size();
        appendRecord(body, c.batch[i].unix_s, &c.batch[i].raw, last ? diag : nullptr, total);
    }
    if (c.batch.empty()) {
        // Zadny platny vzorek - aspon diagnostika, at je videt, ze zarizeni zije.
        appendRecord(body, now_unix_s, nullptr, diag, 0);
    }
    bool ok = post(body, unsigned(c.batch.size()));
    c.batches++;
    c.sent += unsigned(c.batch.size());
    c.batch.clear();
    return ok;
}

bool onSample(void *ctx, int64_t unix_s, int16_t raw) {
    Ctx &c = *static_cast<Ctx *>(ctx);
    // Plnou davku poslat az s dalsim vzorkem - tak se vi, ze neni posledni.
    if (c.batch.size() >= TS_BATCH_MAX && !sendBatch(c, nullptr, 0)) {
        c.failed = true;
        return false;
    }
    c.batch.push_back({unix_s, raw});
    return true;
}

}  // namespace

bool uploadAll(const RtcState &s, const UploadDiag &diag, int64_t now_unix_us) {
    Ctx c;
    c.diag = &diag;
    c.batch.reserve(TS_BATCH_MAX);
    if (!storeForEach(s, onSample, &c) || c.failed) return false;
    return sendBatch(c, &diag, now_unix_us / 1000000);
}
