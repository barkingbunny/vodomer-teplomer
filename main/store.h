#pragma once

// 30denni buffer vzorku ve flash (LittleFS, oddil "storage", /vt).
//
// Kazde 4 h se nove vzorky z RTC bufferu zapisou jako jeden blok
// (/vt/s/<epocha>_<poradi>): zacatek v mono case + sloty po SAMPLE_PERIOD_US.
// Mono cas plati jen v ramci jedne epochy (studeny start = nova epocha, mono od
// nuly), proto se ke kazde epose vede historie NTP syncu (/vt/h/<epocha>):
// casy vzorku se pri odeslani prepocitaji linearne mezi syncy, pred prvnim
// a za poslednim extrapolaci s driftem. Data tak preziji i vypadek napajeni
// (ztrati se jen RTC buffer od posledniho zapisu, max. 4 h).
//
// Pres STORE_MAX_SAMPLES se mazou nejstarsi bloky (kruh 30 dni).

#include <stdint.h>

#include "rtc_state.h"

// Zapise RTC samples[persisted..count) jako novy blok a posune persisted.
// Kontroluje strop 30 dni. false = flash nejde (data zustanou v RTC).
bool storeFlush(RtcState &s);

// Zaznam NTP syncu do historie aktualni epochy.
void storeAddSync(const RtcState &s, uint64_t mono_us, int64_t unix_us);

// Vsechny neodeslane vzorky v casovem poradi: bloky ve flash a pak
// RTC samples[persisted..count). Volat jen s platnym NTP casem. fn() dostane
// unix cas (s, UTC) a hodnotu; chybne sloty a vzorky bez casu (epocha bez
// syncu) se preskoci. fn() vrati false = prerusit (chyba odeslani).
// Vraci false, kdyz fn() prerusila.
typedef bool (*StoreSampleFn)(void *ctx, int64_t unix_s, int16_t raw);
bool storeForEach(const RtcState &s, StoreSampleFn fn, void *ctx);

// Po uspesnem odeslani: smazat bloky, RTC buffer oznacit jako odeslany,
// historii syncu zkratit na posledni zaznam (kotva pro dalsi vzorky).
void storeClearSent(RtcState &s);
