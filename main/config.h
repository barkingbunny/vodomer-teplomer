#pragma once

#include <stdint.h>

#include "driver/gpio.h"

// DS18B20 na GPIO5 (bocni lista Basicu), pull-up 4k7 na 3V3 externe, napajeni
// trvale. GPIO5 je strap pin (casovani SDIO), vychozi uroven HIGH - pull-up
// nevadi. GPIO2 by blokoval nahravani FW, viz hw.md.
constexpr uint8_t PIN_ONEWIRE = 5;

// BtnA = GPIO39 (RTC pin, aktivni LOW) - jedine tlacitko, co umi budit pres ext0.
constexpr gpio_num_t PIN_BTN_A = GPIO_NUM_39;
// Drzene pri startu: BtnB = konfiguracni portal, BtnC = diagnostika pull-upu.
constexpr gpio_num_t PIN_BTN_B = GPIO_NUM_38;
constexpr gpio_num_t PIN_BTN_C = GPIO_NUM_37;
// Reproduktor (DAC1). Drzet LOW i ve spanku (RTC hold), jinak muze bzucet.
constexpr gpio_num_t PIN_SPEAKER = GPIO_NUM_25;
// Podsviceni LCD (RTC pin). Drzet LOW pres spanek i inicializaci M5GFX
// (ta nastavi vychozi jas) - jinak displej pri kazdem probuzeni blikne.
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_32;

// Perioda mereni (mrizka vzorku).
constexpr uint32_t SAMPLE_PERIOD_S = 120;
constexpr uint64_t SAMPLE_PERIOD_US = uint64_t(SAMPLE_PERIOD_S) * 1000000ULL;

// Kratsi spanek nema smysl - radeji preskocit na dalsi slot.
constexpr uint64_t MIN_SLEEP_US = 1000000ULL;

// Pokusy o cteni DS18B20 pri chybe CRC / chybejici presence (ruseni na kabelu).
constexpr uint8_t DS_READ_ATTEMPTS = 3;
// Kolikrat zkusit nastavit 10 bit; nektere klony nastaveni ignoruji.
constexpr uint8_t DS_RES_ATTEMPTS = 3;

// --- Displej (zapina se jen tlacitkem BtnA, UI anglicky)
constexpr uint32_t DISPLAY_TIMEOUT_MS = 20000;  // pak zhasnout a spat
// Drzet BtnA + BtnC na rozsvicenem displeji = vypnout desku (power.h).
constexpr uint32_t OFF_HOLD_MS = 3000;
constexpr uint8_t DISPLAY_BRIGHTNESS = 80;
// Graf: poslednich 8 h = 240 vzorku po 2 min.
constexpr uint32_t GRAPH_HOURS = 8;
constexpr uint16_t GRAPH_SAMPLES = GRAPH_HOURS * 3600 / SAMPLE_PERIOD_S;

// --- Buffer ve flash (LittleFS, oddil "storage")
// RTC buffer se kazde 4 h zapise jako jeden blok; 30 dni kruhove.
constexpr uint64_t FLUSH_PERIOD_US = 4ULL * 3600ULL * 1000000ULL;
constexpr uint32_t STORE_MAX_SAMPLES = 30UL * 24UL * 3600UL / SAMPLE_PERIOD_S;

// --- Wi-Fi a cas
// Okna syncu a odesilani v mistnim case: na baterii v SYNC_HOURS_BATTERY,
// na USB napajeni kazdych SYNC_EVERY_H_USB hodin (v celou hodinu delitelnou N).
constexpr uint8_t SYNC_HOURS_BATTERY[] = {6, 14, 22};
constexpr uint8_t SYNC_EVERY_H_USB = 2;
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"  // Europe/Prague
#define NTP_SERVER1 "cz.pool.ntp.org"
#define NTP_SERVER2 "pool.ntp.org"

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t NTP_TIMEOUT_MS = 10000;
// Pokusy v jednom okne; dalsi pokus pri dalsim mereni (za 2 min).
constexpr uint8_t SYNC_ATTEMPTS = 4;
// Kdyz se nepovede ani prvni sync (cas neznamy), zkusit znovu za:
constexpr uint64_t SYNC_RETRY_UNKNOWN_US = 3600ULL * 1000000ULL;
// Drift z useku kratsiho nez 10 min by byl nepresny.
constexpr uint64_t DRIFT_MIN_SPAN_US = 600ULL * 1000000ULL;
// Vetsi drift RC oscilatoru nez 0.5 % je nerealny (skok v mono case).
constexpr int64_t DRIFT_MAX_PPB = 5000000;

// Konfiguracni portal: otevrena AP "Vodomer-XXXX" (konec MAC), stranka cesky.
// Spusti se jen po zapnuti napajeni a drzenim BtnB pri startu (BtnB + BtnA).
// Neuspesne okno ho neotevira - kazdy beh portalu je otevrena AP navic.
#define PORTAL_SSID_PREFIX "Vodomer-"
constexpr uint32_t PORTAL_CONNECT_MS = 3 * 60 * 1000;  // ceka na pripojeni
constexpr uint32_t PORTAL_IDLE_MS = 3 * 60 * 1000;     // necinnost pripojeneho
constexpr uint32_t PORTAL_MAX_MS = 20 * 60 * 1000;     // strop od spusteni

// --- Odesilani (ThingSpeak bulk_update, viz web.md puvodniho projektu)
#define TS_HOST "api.thingspeak.com"
constexpr uint32_t TS_TIMEOUT_MS = 10000;
constexpr uint16_t TS_BATCH_MAX = 960;          // limit bulk_update
constexpr uint32_t TS_BATCH_SPACING_MS = 16000;  // limit 15 s mezi zapisy

#define FW_VERSION "0.6.6"
