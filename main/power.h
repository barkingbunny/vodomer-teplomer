#pragma once

// Napajeni M5Stack (Basic/Fire): IP5306 keep-on (jinak se deska v deep sleep vypne),
// umlceny reproduktor (GPIO25 v LOW i behem spanku), stav baterie.

#include <stdint.h>

// Uplne prvni vec v setup() (pred M5.begin): repro do LOW bez zakmitu,
// pak uvolnit hold ze spanku. Cim driv, tim kratsi plovouci pin po power-on.
void speakerOff();

// Hned za speakerOff(): podsviceni do LOW a podrzet (hold). M5.begin() ho pak
// nemuze rozsvitit; uvolni ho az backlightRelease() tesne pred rozsvicenim.
void backlightHold();
void backlightRelease();

// Po M5.begin(): nastavit a overit IP5306 keep-on. Vraci false, kdyz se
// keep-on nepodarilo nastavit.
bool powerInit();

// Tesne pred deep sleep: podrzet repro v LOW pres RTC hold.
void powerBeforeSleep();

// Stav baterie v % (po 25 %, IP5306).
int powerBatteryLevel();

// Vypnout desku: IP5306 pustit keep-on a zkratit prodlevu na 8 s, zhasnout,
// usnout bez casovace. Na baterii IP5306 do ~8 s odpoji napajeni (zapnuti
// bocnim tlacitkem POWER = power-on). Na USB IP5306 nevypina - deska zustane
// v deep sleep, probudi ji BtnA. Nevraci se. Volajici predtim ulozi data.
[[noreturn]] void powerShutdown();

// Napajeni z USB (IP5306 READ0 bit 3: vstup pritomen a nabijeni povolene -
// plati i pri plne baterii). Na USB jsou okna syncu castejsi (config.h).
bool powerOnUsb();
