#pragma once

// Displej (UI anglicky).
//
// Stavova obrazovka po stisku BtnA / po zapnuti: aktualni teplota, graf za
// poslednich GRAPH_HOURS, baterie, hodiny a stav Wi-Fi (nastavene SSID
// a vysledek posledniho okna, neuspech cervene). Po DISPLAY_TIMEOUT_MS nebo
// BtnB zhasne; BtnC = synchronizovat ted; BtnA + BtnC drzet 3 s = vypnout
// (s odpoctem na displeji).
//
// Obrazovka portalu: jmeno AP, adresa, zbyvajici cas; BtnB portal zrusi.

#include <stdint.h>

#include "rtc_state.h"

enum class DisplayExit : uint8_t {
    Timeout,   // vyprsel cas nebo BtnB
    SyncNow,   // BtnC - volajici ma hned spustit sync
    PowerOff,  // BtnA + BtnC drzene OFF_HOLD_MS - vypnout desku
};

// tick() se vola prubezne; kdyz vrati true (probehlo mereni na mrizce),
// obrazovka se prekresli.
DisplayExit displayRun(RtcState &s, bool (*tick)(RtcState &));

// Obrazovka portalu (hook PortalHooks::show). Pri prvnim volani displej
// rozsviti. Vraci true, kdyz uzivatel stiskl BtnB (zrusit portal).
bool displayPortal(const char *ap_ssid, uint32_t left_s, int stations);

// Kratka zprava pres celou obrazovku (napr. "Syncing..."), displej rozsviti.
void displayMessage(const char *title, const char *line);

// Zhasnout a uspat radic (pred spankem).
void displayOff();
