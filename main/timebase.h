#pragma once

// Prevod monotonniho casu zarizeni na realny cas (UTC) podle NTP syncu.
//
//   unix = sync_unix + (mono - sync_mono) * (1 + drift_ppb / 1e9)
//
// Systemovy cas se NEprepisuje (settimeofday se nevola) - mono hodiny zustanou
// spojite a mrizka mereni se syncem nerozhodi. Drift se pri kazdem syncu
// zmeri znovu: o kolik mono hodiny ujely proti NTP od minuleho syncu. Casy
// vzorku se pri odeslani prepocitaji linearne mezi syncy (store.h).
//
// Do prvniho NTP jen odhad: pri prvnim startu noveho buildu se cas vezme
// z casu prekladu (tools/buildtime.cmake) + odhad doby nahrani (FLAG_TIME_APPROX,
// na displeji s "~"). Po vypadku napajeni bez noveho FW je cas neznamy.
// Zobrazuje se a okna se planuji v mistnim case (Europe/Prague).

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "rtc_state.h"

// Nastavit casove pasmo (volat pri kazdem startu).
void timebaseInit();

// Po rtcInit() pri studenem startu: nacte drift z NVS a kdyz je to prvni start
// tohoto buildu, nastavi odhad casu (FLAG_TIME_APPROX).
void timebaseColdStart(RtcState &s);

// Mame cas z NTP (ne jen odhad)?
bool timeValid(const RtcState &s);

// Odhad unix casu (us, UTC) pro dany mono cas. Plati pri NTP i odhadu.
int64_t monoToUnixUs(const RtcState &s, uint64_t mono_us);

// Inverzni prevod - kdy (v mono) nastane dany unix cas.
uint64_t unixToMonoUs(const RtcState &s, int64_t unix_us);

// Zapise novy NTP sync, prepocita drift (ulozi ho do NVS) a zaloguje chybu odhadu.
void timeSync(RtcState &s, uint64_t mono_us, int64_t unix_us);

// Prvni okno syncu po unix_s v mistnim case: na USB kazdych SYNC_EVERY_H_USB
// hodin, jinak SYNC_HOURS_BATTERY. S odstupem aspon hodinu.
int64_t nextSyncWindowUnix(int64_t unix_s, bool usb);

// Lokalni cas, false kdyz neni znamy. approx = jen odhad (ne NTP).
bool timebaseLocal(const RtcState &s, struct tm &out, bool &approx);

// "2026-09-30 12:34:56" v mistnim case.
void formatLocal(int64_t unix_us, char *buf, size_t len);
