#pragma once

// Stav, ktery musi prezit deep sleep: rozpracovany prevod, mrizka probuzeni,
// buffer vzorku, mapovani na realny cas a stav oken syncu. Ulozeny v RTC slow
// pameti (RTC_DATA_ATTR, 8 KB); prezije deep sleep i SW reset, maze se
// vypnutim napajeni.
//
// Vzorky nemaji vlastni casove znacky. Lezi na mrizce po SAMPLE_PERIOD_US
// od buf_start_mono_us, chybejici nebo chybny slot ma hodnotu SAMPLE_MISSING.
// Casy jsou v "monotonnim" case zarizeni (us od studeneho startu, clock.h),
// na realny cas se prevedou az pri odeslani (timebase.h, store.h). Kazde 4 h
// se nove vzorky zapisou do flash (store.h); buffer sam zustava jako 24h kruh
// pro graf na displeji, `persisted` rika, kolik z nej uz je ulozeno/odeslano.

#include <stdint.h>

constexpr int16_t SAMPLE_MISSING = INT16_MIN;

enum : uint8_t {
    FLAG_CONV_PENDING   = 1 << 0,  // bezi prevod, vysledek se precte pri probuzeni
    FLAG_TIME_VALID     = 1 << 1,  // mame aspon jeden NTP sync (sync_* plati)
    FLAG_TIME_APPROX    = 1 << 2,  // cas jen odhadnuty z casu buildu (timebase.h)
    FLAG_USB_POWER      = 1 << 4,  // okna naplanovana pro napajeni z USB
};

// Vysledek posledniho pokusu o sync (pro displej).
enum class SyncResult : uint8_t {
    None,        // od studeneho startu jeste nebyl
    Ok,          // odeslano
    NoWifi,      // nepripojeno k Wi-Fi (nebo nejsou udaje)
    NoNtp,       // Wi-Fi ano, NTP ne
    UploadFail,  // ThingSpeak nevratil 202
};

struct RtcHeader {
    uint32_t magic;
    uint32_t crc;                // CRC32 vseho za timto polem

    uint32_t boot_count;         // probuzeni od studeneho startu
    uint32_t timer_wakes;
    uint32_t button_wakes;
    uint32_t epoch;              // poradi studeneho startu (NVS) - oznacuje bloky ve flash

    uint8_t  cold_reason;        // duvod posledniho studeneho startu (esp_reset_reason)
    uint8_t  flags;
    uint8_t  err_count;          // chybna mereni od posledniho odeslani (saturuje)
    uint8_t  ds_res_fails;       // kolikrat po sobe cidlo neprijalo 10 bit

    uint8_t  sync_attempts_left; // zbyvajici pokusy v aktualnim okne
    uint8_t  failed_windows;     // okna bez uspesneho odeslani od posledniho uspechu
    SyncResult last_result;      // vysledek posledniho pokusu
    int8_t   last_rssi;          // RSSI posledniho pripojeni [dBm], 0 = nebylo

    uint16_t count;              // obsazene sloty
    uint16_t dropped;            // vzorky ztracene preplnenim bufferu
    uint16_t chunk_seq;          // poradi dalsiho bloku ve flash v teto epose
    uint16_t persisted;          // samples[0..persisted) uz jsou ve flash nebo odeslane

    uint64_t conv_mono_us;       // kdy byl odeslan prikaz k prevodu
    uint64_t next_wake_mono_us;  // planovane dalsi probuzeni (mrizka mereni)
    uint64_t buf_start_mono_us;  // cas slotu samples[0]
    uint64_t last_flush_mono_us; // posledni zapis bufferu do flash

    // Realny cas: unix = sync_unix + (mono - sync_mono) * (1 + drift_ppb / 1e9).
    uint64_t sync_mono_us;       // mono cas posledniho NTP syncu (nebo odhadu z buildu)
    int64_t  sync_unix_us;       // unix cas (UTC) v tom okamziku
    uint64_t next_sync_mono_us;  // kdy se ma priste synchronizovat
    int64_t  last_ok_unix_us;    // posledni uspesne odeslani (UTC), 0 = nebylo
    int32_t  drift_ppb;          // korekce mono -> realny cas
    uint8_t  wifi_reason;        // duvod posledniho neuspesneho pripojeni (wifi_err_reason_t)
    uint8_t  reserved2[3];
};

// 24 h vzorku po 2 min; graf potrebuje poslednich 8 h, do flash se zapisuje
// kazde 4 h. Rezerva pro pripad, ze flash nejde.
constexpr uint16_t RTC_SAMPLES = 720;

struct RtcState {
    RtcHeader h;
    int16_t samples[RTC_SAMPLES];
};

// RTC slow pamet ma 8 KB a cast si bere system - nechat rezervu.
static_assert(sizeof(RtcState) <= 6 * 1024, "RtcState se nevejde do RTC pameti");

// Nacte stav, vrati false pri neplatnem magic/CRC (studeny start).
bool rtcLoad(RtcState &s);
void rtcSave(RtcState &s);
void rtcInit(RtcState &s);
// Zneplatnit ulozeny stav - dalsi probuzeni bude studeny start (po vypnuti).
void rtcInvalidate();

// Ulozi vzorek do slotu odpovidajiciho casu t_mono_us, mezery vyplni SAMPLE_MISSING.
void rtcPushSample(RtcState &s, uint64_t t_mono_us, int16_t raw);
