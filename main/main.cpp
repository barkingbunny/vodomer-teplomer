// Vodomer-teplomer: mereni DS18B20 + deep sleep na M5Stack Basic (ESP32, ESP-IDF 6.1).
// Port z ESP-WROOM-02_temp_meas, navrh: navrh-m5stack-fire.md, zadani: zadani.md.
//
// Kazde probuzeni casovacem (1x za SAMPLE_PERIOD_S):
//   1. precte vysledek prevodu spusteneho minule a ulozi ho s casem jeho spusteni,
//   2. spusti novy prevod,
//   3. kazde 4 h zapise nove vzorky do flash (store.h),
//   4. kdyz je na rade okno (6:00/14:00/22:00, na USB kazde 2 h; po studenem
//      startu hned): Wi-Fi -> NTP (cas, drift) -> odeslani na ThingSpeak,
//   5. usne - DS18B20 prevadi, zatimco ESP spi.
//
// Probuzeni tlacitkem BtnA (ext0): rozsviti displej (teplota, graf 8 h, stav
// Wi-Fi), BtnC = sync hned, BtnA + BtnC 3 s = vypnout (shutdown()).
// Konfiguracni portal jen po zapnuti napajeni
// a drzenim BtnB pri startu; neuspesne okno ho neotevira (udaje k Wi-Fi
// zustavaji, dalsi okno to zkusi znovu).
// Mono cas (clock.h) bezi pres vsechna probuzeni spojite; kdyz slot mrizky
// pripadne do zobrazeni nebo portalu, zmeri se primo pri nem.

#include <M5Unified.h>

#include "esp_sleep.h"
#include "esp_system.h"

#include "clock.h"
#include "config.h"
#include "diag.h"
#include "display.h"
#include "ds18b20.h"
#include "log.h"
#include "net.h"
#include "persist.h"
#include "portal.h"
#include "power.h"
#include "rtc_state.h"
#include "store.h"
#include "timebase.h"
#include "upload.h"

static RtcState state;

static void coldStart(uint8_t reason) {
    rtcInit(state);
    state.h.next_wake_mono_us = clockNow() + SAMPLE_PERIOD_US;
    state.h.next_sync_mono_us = 0;  // synchronizovat hned
    state.h.sync_attempts_left = SYNC_ATTEMPTS;
    state.h.cold_reason = reason;
    state.h.last_flush_mono_us = clockNow();

    // Nova epocha: mono cas zacina od nuly, bloky ve flash se podle ni rozlisi.
    uint32_t epoch = 0;
    persistGetU32("epoch", epoch);
    state.h.epoch = epoch + 1;
    persistSetU32("epoch", state.h.epoch);

    timebaseColdStart(state);
    LOG("[boot] studeny start (reset reason %u), FW " FW_VERSION ", epocha %lu",
        unsigned(reason), (unsigned long)state.h.epoch);
}

static void logMono(const char *what, uint64_t mono_us) {
    if (timeValid(state)) {
        char buf[32];
        formatLocal(monoToUnixUs(state, mono_us), buf, sizeof buf);
        LOG("%s mono %lu.%03lu s = %s", what, (unsigned long)(mono_us / 1000000),
            (unsigned long)(mono_us / 1000 % 1000), buf);
    } else {
        LOG("%s mono %lu.%03lu s", what, (unsigned long)(mono_us / 1000000),
            (unsigned long)(mono_us / 1000 % 1000));
    }
}

static void measure(DsBus &ow, bool cold) {
    if (state.h.flags & FLAG_CONV_PENDING) {
        int16_t raw = SAMPLE_MISSING;
        bool config_ok = true;
        DsStatus st = DsStatus::NoPresence;
        for (uint8_t attempt = 1; attempt <= DS_READ_ATTEMPTS; attempt++) {
            st = dsRead(ow, raw, config_ok);
            if (st != DsStatus::CrcError && st != DsStatus::NoPresence) break;
            LOG("[ds] pokus %u: %s", attempt, dsStatusName(st));
        }
        if (st == DsStatus::Ok) {
            rtcPushSample(state, state.h.conv_mono_us, raw);
            LOG("[ds] %.2f C (raw %d)", raw / 16.0f, raw);
        } else {
            // Chybne mereni se vynecha - slot zustane SAMPLE_MISSING.
            rtcPushSample(state, state.h.conv_mono_us, SAMPLE_MISSING);
            if (state.h.err_count < UINT8_MAX) state.h.err_count++;
            LOG("[ds] chyba: %s", dsStatusName(st));
        }
        // Rozliseni znovu nastavit (po vypadku napajeni cidla). Klon, ktery ho
        // ignoruje, se po DS_RES_ATTEMPTS pokusech nechava na svem rozliseni.
        if (st == DsStatus::Ok && config_ok) {
            state.h.ds_res_fails = 0;
        } else if (st == DsStatus::Ok) {
            if (state.h.ds_res_fails < DS_RES_ATTEMPTS) {
                state.h.ds_res_fails++;
                LOG("[ds] cidlo ma %d bit, nastavuji 10 bit (pokus %u/%u)", dsLastBits,
                    state.h.ds_res_fails, DS_RES_ATTEMPTS);
                cold = true;
                if (state.h.ds_res_fails == DS_RES_ATTEMPTS) {
                    LOG("[ds] cidlo nastaveni rozliseni ignoruje - dal se meri v %d bit", dsLastBits);
                }
            }
        }
        if (st == DsStatus::PowerOn85) cold = true;
    }

    if (cold) {
        if (!dsSetResolution10(ow)) LOG("[ds] nastaveni rozliseni selhalo");
    }

    uint64_t now = clockNow();
    if (dsStartConversion(ow)) {
        state.h.flags |= FLAG_CONV_PENDING;
        state.h.conv_mono_us = now;
        logMono("[ds] start prevodu,", now);
    } else {
        state.h.flags &= ~FLAG_CONV_PENDING;
        rtcPushSample(state, now, SAMPLE_MISSING);
        if (state.h.err_count < UINT8_MAX) state.h.err_count++;
        LOG("[ds] cidlo neodpovida");
    }
}

// Merit podle mrizky i behem displeje / portalu, at v datech neni mezera.
static bool gridTick(RtcState &s) {
    uint64_t now = clockNow();
    if (now < s.h.next_wake_mono_us) return false;
    DsBus ow;
    measure(ow, false);
    while (s.h.next_wake_mono_us <= now) s.h.next_wake_mono_us += SAMPLE_PERIOD_US;
    return true;
}

static void portalTick() {
    gridTick(state);
}

// Kazde 4 h nove vzorky do flash (pri vypadku napajeni se ztrati max. 4 h).
static void maybeFlush() {
    uint64_t now = clockNow();
    if (now - state.h.last_flush_mono_us < FLUSH_PERIOD_US) return;
    state.h.last_flush_mono_us = now;
    storeFlush(state);
}

// --- Okna syncu

static void scheduleNextWindow() {
    state.h.sync_attempts_left = SYNC_ATTEMPTS;
    bool usb = state.h.flags & FLAG_USB_POWER;
    if (timeValid(state)) {
        int64_t now_unix = monoToUnixUs(state, clockNow());
        int64_t next = nextSyncWindowUnix(now_unix / 1000000, usb);
        state.h.next_sync_mono_us = unixToMonoUs(state, next * 1000000LL);
        char buf[32];
        formatLocal(next * 1000000LL, buf, sizeof buf);
        LOG("[sync] dalsi okno %s (%s)", buf, usb ? "USB" : "baterie");
    } else {
        state.h.next_sync_mono_us = clockNow() + SYNC_RETRY_UNKNOWN_US;
        LOG("[sync] cas neznamy, dalsi pokus za %lu min",
            (unsigned long)(SYNC_RETRY_UNKNOWN_US / 60000000));
    }
}

// Zmena napajeni (USB <-> baterie) -> okna podle noveho rezimu.
static void checkPowerSource() {
    bool usb = powerOnUsb();
    if (usb == bool(state.h.flags & FLAG_USB_POWER)) return;
    if (usb) state.h.flags |= FLAG_USB_POWER;
    else state.h.flags &= ~FLAG_USB_POWER;
    LOG("[pwr] napajeni: %s", usb ? "USB" : "baterie");
    // Rozbehnute okno s pokusy nechat dobehnout, jinak preplanovat.
    if (state.h.sync_attempts_left == SYNC_ATTEMPTS && timeValid(state)) scheduleNextWindow();
}

static bool syncDue() {
    // NVS az nakonec - bezne probuzeni na ni nesaha.
    return state.h.sync_attempts_left > 0 && clockNow() >= state.h.next_sync_mono_us &&
           persistHasWifi();
}

// Okno: Wi-Fi -> NTP (cas, drift) -> odeslani. Uspech = server prijal vse.
static void doSync() {
    int8_t rssi = 0;
    int64_t unix_us = 0;
    uint64_t mono_us = 0;
    SyncResult res = SyncResult::Ok;

    // Do flash se pred pokusem nezapisuje - neodeslane vzorky cekaji v RTC
    // (24 h) a do flash jdou jen pravidelne kazde 4 h (maybeFlush).
    uint8_t reason = 0;
    if (!netConnect(WIFI_CONNECT_TIMEOUT_MS, rssi, reason)) {
        res = SyncResult::NoWifi;
        state.h.wifi_reason = reason;
    } else if (!netNtp(NTP_TIMEOUT_MS, unix_us, mono_us)) {
        res = SyncResult::NoNtp;
    } else {
        state.h.last_rssi = rssi;
        timeSync(state, mono_us, unix_us);
        storeAddSync(state, mono_us, unix_us);

        UploadDiag diag{rssi, state.h.failed_windows, state.h.err_count, state.h.drift_ppb,
                        powerBatteryLevel(), state.h.cold_reason};
        int64_t now_unix = monoToUnixUs(state, clockNow());
        if (uploadAll(state, diag, now_unix)) {
            storeClearSent(state);
            state.h.last_ok_unix_us = now_unix;
        } else {
            res = SyncResult::UploadFail;
        }
    }
    netOff();
    state.h.last_result = res;

    if (res == SyncResult::Ok) {
        state.h.dropped = 0;
        state.h.err_count = 0;
        state.h.failed_windows = 0;
        scheduleNextWindow();
        return;
    }

    state.h.sync_attempts_left--;
    LOG("[sync] neuspech (%d), zbyva pokusu %u", int(res), state.h.sync_attempts_left);
    if (state.h.sync_attempts_left == 0) {
        // Udaje k Wi-Fi zustavaji - dalsi okno to zkusi znovu (napr. router
        // po vypadku). Portal se kvuli neuspechu NEotevira (zadani.md).
        if (state.h.failed_windows < UINT8_MAX) state.h.failed_windows++;
        scheduleNextWindow();
    }
}

static void runPortal() {
    PortalHooks hooks{portalTick, displayPortal};
    if (portalRun(hooks)) {
        // Nove udaje - hned zkusit sync s plnym poctem pokusu.
        state.h.next_sync_mono_us = 0;
        state.h.sync_attempts_left = SYNC_ATTEMPTS;
    }
    displayOff();
}

// Vypnuti dvojhmatem A + C: neulozene vzorky do flash (po zapnuti se odeslou),
// RTC stav zneplatnit (zapnuti = studeny start, nova epocha), vypnout IP5306.
[[noreturn]] static void shutdown() {
    LOG("[pwr] vypnuti tlacitky A+C");
    storeFlush(state);
    bool usb = state.h.flags & FLAG_USB_POWER;
    displayMessage("SHUTTING DOWN", usb ? "on USB: press A to wake up"
                                        : "turn on: side POWER button");
    delayMs(800);
    rtcInvalidate();
    powerShutdown();
}

static void goToSleep() {
    uint64_t now = clockNow();

    // Dalsi slot na mrizce; kdyz jsme nejaky prospali, preskocime ho.
    while (state.h.next_wake_mono_us < now + MIN_SLEEP_US) {
        state.h.next_wake_mono_us += SAMPLE_PERIOD_US;
    }
    uint64_t sleep_us = state.h.next_wake_mono_us - now;

    displayOff();
    powerBeforeSleep();
    rtcSave(state);

    esp_sleep_enable_timer_wakeup(sleep_us);
    esp_sleep_enable_ext0_wakeup(PIN_BTN_A, 0);

    LOG("[sleep] %lu ms, vzorku %u (ulozeno %u), chyb %u, awake %lu ms",
        (unsigned long)(sleep_us / 1000), state.h.count, state.h.persisted, state.h.err_count,
        (unsigned long)uptimeMs());
    LOG_FLUSH();
    esp_deep_sleep_start();
}

extern "C" void app_main() {
    speakerOff();     // co nejdriv - kazdy plovouci okamzik na GPIO25 je slyset
    backlightHold();  // M5.begin() by displej na okamzik rozsvitil

    // Bitmapa zdroju probuzeni (1 << esp_sleep_wakeup_cause_t). Prijdou-li
    // timer i BtnA naraz, vyhrava tlacitko - slot se domeri v gridTick().
    uint32_t causes = esp_sleep_get_wakeup_causes();
    bool woke_timer = causes & (1UL << ESP_SLEEP_WAKEUP_TIMER);
    bool woke_button = causes & (1UL << ESP_SLEEP_WAKEUP_EXT0);
    esp_reset_reason_t rst = esp_reset_reason();

    auto cfg = M5.config();
    cfg.clear_display = false;  // displej resit az podle duvodu probuzeni
    cfg.internal_imu = false;
    cfg.led_brightness = 0;  // SK6812 nechat tmave
    cfg.internal_mic = false;
    cfg.internal_spk = false;  // repro nepouzivame vubec
    M5.begin(cfg);
    powerInit();
    timebaseInit();

    // Drzene tlacitko pri startu: BtnC = diagnostika pull-upu, BtnB = portal.
    if (gpio_get_level(PIN_BTN_C) == 0) diagRun();
    bool btn_portal = gpio_get_level(PIN_BTN_B) == 0;

    bool valid = rtcLoad(state);
    bool cold = !(woke_timer || woke_button) || !valid;
    bool by_button = !cold && woke_button;
    bool power_on = cold && rst == ESP_RST_POWERON;

    // Displej se rozsviti jen po tlacitku a po zapnuti napajeni - timer wake
    // ho jen uspi (M5.begin ho inicializuje vzdy).
    if (!(by_button || power_on)) {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
    }

    if (cold) {
        coldStart(uint8_t(rst));
    } else {
        if (by_button) state.h.button_wakes++;
        else state.h.timer_wakes++;
    }
    state.h.boot_count++;
    checkPowerSource();
    LOG("[boot] #%lu causes=0x%lx (timer %lu / btn %lu), bat %d %%%s",
        (unsigned long)state.h.boot_count, (unsigned long)causes,
        (unsigned long)state.h.timer_wakes, (unsigned long)state.h.button_wakes,
        powerBatteryLevel(), (state.h.flags & FLAG_USB_POWER) ? ", USB" : "");

    if (by_button) {
        gridTick(state);  // kdyz behem spanku stihl uplynout slot mrizky, domerit hned
    } else {
        DsBus ow;
        measure(ow, cold);
    }
    maybeFlush();

    // Portal jen po zapnuti napajeni a drzenim BtnB pri startu (BtnB + BtnA).
    if (btn_portal || power_on) {
        LOG("[portal] spoustim (%s)", btn_portal ? "BtnB" : "zapnuti napajeni");
        runPortal();
    }

    bool sync_now = false;
    if (by_button || power_on) {
        DisplayExit ex = displayRun(state, gridTick);
        sync_now = ex == DisplayExit::SyncNow;
        if (ex == DisplayExit::PowerOff) shutdown();
    }
    if (sync_now) {
        LOG("[sync] rucne tlacitkem");
        displayMessage("SYNCING...", "Wi-Fi, NTP, ThingSpeak");
        state.h.next_sync_mono_us = 0;
        state.h.sync_attempts_left = SYNC_ATTEMPTS;
    }

    if (syncDue()) {
        doSync();
        if (sync_now) displayRun(state, gridTick);  // ukazat vysledek
    }

    goToSleep();
}
