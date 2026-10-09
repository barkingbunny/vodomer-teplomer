#pragma once

// Male hodnoty, ktere musi prezit i vypnuti napajeni (NVS, namespace "vt"):
// drift hodin, cislo epochy, znacka buildu (odhad casu) a udaje k Wi-Fi.
// Zapisuje se jen pri zmene (studeny start, sync, portal), bezne probuzeni
// na NVS nesaha.

#include <stddef.h>
#include <stdint.h>

// Inicializace NVS (idempotentni). Pri poskozene / jine verzi NVS ji smaze.
bool persistInit();

bool persistGetU32(const char *key, uint32_t &value);
void persistSetU32(const char *key, uint32_t value);

// Retezec vcetne '\0'; false kdyz klic neni nebo se nevejde.
bool persistGetStr(const char *key, char *buf, size_t len);
void persistSetStr(const char *key, const char *value);

// --- Udaje k Wi-Fi (nastavuje portal)
constexpr size_t WIFI_SSID_MAX = 32;
constexpr size_t WIFI_PASS_MAX = 64;

bool persistGetWifi(char (&ssid)[WIFI_SSID_MAX + 1], char (&pass)[WIFI_PASS_MAX + 1]);
void persistSetWifi(const char *ssid, const char *pass);
bool persistHasWifi();
