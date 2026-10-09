# Volitelné úpravy (NEjsou zadání)

Odložené nápady, které se zatím **neimplementují**. Řešit až na výslovný pokyn,
nejpozději se k nim vrátit před release. Závazné zadání je v `zadani.md`.

## Heslo k AP konfiguračního portálu (odloženo 2026-10-09)

**Stav teď:** portál je otevřená AP `Vodomer-XXXX` bez hesla, stejně jako
v původním projektu.

**Riziko:** kdokoli v dosahu se během portálu (max. 20 min) může připojit
a nastavit vlastní Wi-Fi. Zařízení by pak posílalo data přes jeho síť, včetně
Write key, protože ThingSpeak jde přes HTTP (viz `bezpecnost.md` v původním
projektu, bod 3).

**Návrh:** AP s heslem (WPA2), které se při každém spuštění portálu vygeneruje
náhodně a ukáže na displeji spolu s názvem AP a zbývajícím časem. Displej se
během portálu rozsvítí sám, takže se nemusí sahat na tlačítka — heslo se jen
opíše do telefonu. Kdo na displej nevidí, nepřipojí se.

**Náklady:** malá změna v portálu (`wifi_config_t.ap.password`, `WIFI_AUTH_WPA2_PSK`)
a řádek na obrazovce portálu.

## Stabilnější hodiny ve spánku (zamítnuto 2026-10-09, pro případ potřeby)

Zdroj RTC hodin ve spánku přepnout z RC 150 kHz na interní 8,5 MHz / 256
(`CONFIG_RTC_CLK_SRC_INT_8MD256=y` v `sdkconfig.defaults`). Menší teplotní
závislost driftu, spotřeba navíc vedle IP5306 zanedbatelná. Teď není potřeba:
čas slouží jen pro graf a korekce přes NTP stačí. Zvážit, až se budou časy
párovat s jiným dějem měřeným na desce (další iterace).
