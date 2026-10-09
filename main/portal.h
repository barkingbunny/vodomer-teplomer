#pragma once

// Konfiguracni portal (jen po zapnuti napajeni a BtnB + BtnA): ESP vytvori
// otevrenou AP "Vodomer-XXXX" a na
// http://192.168.4.1 (captive portal - DNS odpovida na vse touto adresou)
// nabidne formular: sit ze seznamu nebo SSID rucne + heslo. Po ulozeni se
// udaje zapisou do NVS (persist.h) a portal skonci. Stranka je cesky.
//
// Casy (config.h): PORTAL_CONNECT_MS ceka na pripojeni, pak ho ukonci
// PORTAL_IDLE_MS bez pozadavku na stranku, nejdeleji bezi PORTAL_MAX_MS.

#include <stdint.h>

struct PortalHooks {
    // Volano prubezne - main v nem meri podle mrizky, at behem portalu
    // nevznikne mezera v datech.
    void (*tick)();
    // Volano 1x za s: prekresleni displeje. Vraci true = uzivatel portal
    // zrusil (BtnB).
    bool (*show)(const char *ap_ssid, uint32_t left_s, int stations);
};

// Vraci true, kdyz uzivatel ulozil nove udaje.
bool portalRun(const PortalHooks &hooks);
