#pragma once

// Monotonni hodiny zarizeni.
//
// Na rozdil od ESP8266 udrzuje ESP32 systemovy cas (gettimeofday) i pres deep
// sleep - pri probuzeni ho dopocita z RTC citace (RC ~150 kHz, s kalibraci).
// Mono cas je tedy spojity pres vsechna probuzeni vcetne tlacitka, mrizka
// mereni se probuzenim displeje nerozhodi. Systematicky drift RC oscilatoru
// vyrovna ve fazi 3 korekce podle NTP (mapovani mono -> unix, settimeofday
// se nevola).

#include <stdint.h>

// Aktualni monotonni cas v us (od studeneho startu, ~0 po power-on).
uint64_t clockNow();

// Cas od tohoto probuzeni v ms (nahrada Arduino millis()).
uint32_t uptimeMs();

// Uspani tasku na ms (nahrada Arduino delay()).
void delayMs(uint32_t ms);
