#pragma once

// Wi-Fi (stanice i AP pro portal) a NTP.
//
// Udaje k siti jsou v NVS (persist.h), driver Wi-Fi si nic neuklada
// (WIFI_STORAGE_RAM). NTP je vlastni minimalni SNTP dotaz: systemovy cas se
// NEprepisuje (mono hodiny musi zustat spojite, viz timebase.h), jen se vrati
// dvojice (mono, unix) zmerena v okamziku prijmu odpovedi.

#include <stdint.h>

// Inicializace netif / event loop / driveru (idempotentni).
bool netInit();

// Pripojeni k ulozene siti: sken vsech kanalu, z vice AP se stejnym SSID
// (firemni sit) ten nejsilnejsi; pri odpojeni behem pripojovani dalsi pokus
// az do timeoutu. Vraci false po timeoutu nebo bez udaju; reason = posledni
// duvod odpojeni (wifi_err_reason_t, 0 = zadny).
bool netConnect(uint32_t timeout_ms, int8_t &rssi, uint8_t &reason);

// Kratky popis duvodu odpojeni pro displej (anglicky).
const char *netReasonText(uint8_t reason);

// SNTP (NTP_SERVER1, pak NTP_SERVER2). unix_us = UTC v okamziku mono_us.
bool netNtp(uint32_t timeout_ms, int64_t &unix_us, uint64_t &mono_us);

// Vypnout Wi-Fi (radio off), driver zustane inicializovany.
void netOff();

// --- pro portal
// Rezim AP+STA, otevrena AP se zadanym SSID na 192.168.4.1.
bool netStartAp(const char *ssid);
// Pocet pripojenych klientu AP.
int netApStations();
// Konec MAC (2 B hex, velkymi) pro jmeno AP.
void netMacSuffix(char *buf, int len);

struct NetScanEntry {
    char ssid[33];
    int8_t rssi;
};
// Sken okolnich siti (blokujici, ~2 s), bez duplicit, serazene podle RSSI.
int netScan(NetScanEntry *out, int max);
