# Pracovní poznámky — Vodomer-teplomer (M5Stack)

Zadání a decision log: [zadani.md](zadani.md). HW a zapojení: [hw.md](hw.md).
Návrh projektu: [navrh-m5stack-fire.md](navrh-m5stack-fire.md).

Stav Fire (2026-10-09): **stále náš FW 0.2.0** — obnova do továrního stavu
(UIFlow) zatím neprovedena, postup v hw.md (bin stažený v `%TEMP%\uiflow-fire.bin`
je dočasný, po smazání TEMP znovu stáhnout z GitHubu).

## Build / upload (od FW 0.4.0: ESP-IDF 6.1)

- ESP-IDF 6.1 v `C:\Espressif` (instalace `eim install -i v6.1 -t esp32`),
  aktivace `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`
  (dělá ji `tools\build.ps1` sám).
- Build: `tools\build.ps1`, release `-Release`, nahrání `-Flash` (COM10,
  přímo přes USB), monitor `-Monitor` (`idf.py monitor --no-reset` — reset
  by smazal RTC buffer i odhad času).
- **Spouštět z PowerShellu.** Z Git Bash idf.py skončí hláškou „MSys/Mingw is
  no longer supported“.
- Build, `sdkconfig` a stažené komponenty mimo Google Drive:
  `%LOCALAPPDATA%\idf-build\vodomer-teplomer\{debug,release,managed_components}`;
  `managed_components` v projektu je junction (cesta je v component manageru napevno).
- `dependencies.lock` (verze komponent) patří do projektu.
- Do FW 0.3.0 PlatformIO + Arduino 2.0.17 (`pio run -e basic-debug`, Fire COM5).

## Milník 1 — deep sleep + napájení (2026-10-08)

FW: počítadla probuzení v RTC paměti, deep sleep s timer (30 s) + ext0 BtnA,
GPIO25 (repro) držený LOW přes RTC hold i ve spánku, IP5306 keep-on
(SYS_CTL0 bit 1) s ověřením zápisu, displej se rozsvítí jen po BtnA.

Ověřeno na USB (3 cykly po sobě):
- timer wake funguje opakovaně, RTC_DATA_ATTR počítadla přežívají spánek,
- `IP5306 keep-on OK` (bit už byl nastavený — M5Unified ho nastavuje taky),
- probuzení trvá ~592 ms (boot ESP32 + M5.begin), pak hned spánek,
- displej po timer wake zůstává zhasnutý (sleep + brightness 0).

### Zbývá ověřit ručně
- [ ] stisk BtnA → stavová obrazovka (boot count, baterie), BtnB zhasne, timeout 15 s
- [ ] **noční test na baterii (odpojit USB!)**: ráno stisknout BtnA a zkontrolovat,
      že `timer waku` odpovídá době běhu (~2 probuzení/min v debug buildu).
      Pokud deska ráno nereaguje → IP5306 ji vypnul → go/no-go rozhodnutí z návrhu.
- [ ] změřit klidový odběr v deep sleep (odhad z návrhu: 3–15 mA)

## Milník 2 — měření + displej (2026-10-09, FW 0.2.0)

Port měřicího jádra z ESP8266 projektu + displej. Moduly:
`config`, `log`, `clock` (gettimeofday — ESP32 drží systémový čas i přes deep
sleep, mřížka je spojitá přes všechna probuzení), `rtc_state` (RTC_DATA_ATTR,
buffer 720 vzorků = 24 h), `ds18b20` (beze změny, pin 26), `power` (IP5306 +
repro), `display` (UI anglicky: aktuální teplota, graf 8 h, baterie, timeout
20 s, BtnB zhasne hned), `main`.

Displej se rozsvítí jen po BtnA nebo po zapnutí napájení; timer wake ho vůbec
neinicializuje. Když slot mřížky padne do doby zobrazení, měří se při něm
(displayTick) a obrazovka se překreslí.

Ověřeno na USB:
- studený start ukázal obrazovku 20 s (awake 20,6 s), pak spánek **99,9 s =
  přesně dopočet do slotu mřížky** — zobrazení mřížku nerozhodí,
- timer wake: awake 594 ms, spánek 119,3 s, sloty sedí,
- čidlo zatím nepřipojené → `[ds] cidlo neodpovida`, sloty SAMPLE_MISSING,
  err_count roste — ošetřeno, FW běží dál.

### Zbývá ověřit ručně
- [ ] **připojit DS18B20**: DQ na GPIO26 (Grove Port B), pull-up 4k7 na 3,3 V,
      VDD trvale (3,3 V z M-Bus nebo 5 V z Grove) — pak zkontrolovat hodnoty
- [ ] stisk BtnA → obrazovka s teplotou a grafem, timeout 20 s, BtnB zhasne
- [ ] **noční test na baterii (odpojit USB!)**: ráno BtnA → `wake #` ve
      stavovém řádku musí odpovídat době běhu. Pokud deska nereaguje,
      IP5306 ji vypnul → go/no-go rozhodnutí z návrhu.
- [ ] změřit klidový odběr v deep sleep (odhad z návrhu: 3–15 mA)

## M5Stack Basic (2026-10-09)

Druhá deska: starší **M5Stack Basic** — ESP32-D0WDQ6 rev 1.0, **4 MB flash**,
CP2104 na **COM10**, MAC 98:f4:ab:6c:1c:34. Prostředí `basic-debug` /
`basic-release` (board `m5stack-core-esp32`, 4MB partition). Kód beze změny.
K dispozici je i **Battery modul +700 mAh** (stohuje se mezi core a bottom,
paralelně k ~110mAh baterii bottomu → celkem ~810 mAh, ~7× výdrž).

Ověřeno na USB:
- FW běží, mřížka drží (úvodní obrazovka 20,2 s + spánek 99,9 s = slot 120 s),
- **IP5306 odpovídá na I2C** (žádné `keep-on SELHAL` v logu) — tenhle kus
  tedy nejspíš není nejstarší revize bez I2C; bateriový deep sleep má šanci,
- probuzení jen **162 ms** (vs. 594 ms na Fire — bez PSRAM init).

Zbývá: noční test na baterii (bez USB!) — rozhodne definitivně; na USB se
IP5306 nikdy nevypíná, takže USB test nic nedokazuje. Pak připojit DS18B20
(boční lišta bottomu: GPIO26, 3V3, GND — DuPont přímo, pull-up 4k7 na 3V3).

## Další kroky (dle návrhu, odd. 8)

3. Wi-Fi/NTP/upload + portál, kredenciály v NVS, timebase (mono → unix)
4. překlopení bufferu do LittleFS před přeplněním (30 dní)
5. dlouhodobý test výdrže

## FW 0.4.0 — ESP-IDF 6.1 (2026-10-09)

Přepis z Arduina na čisté ESP-IDF 6.1, chování beze změny. Ověřeno na Basicu
(USB, bez čidla):
- studený start: odhad času z buildu, obrazovka 20 s, spánek do slotu
  (20,2 s + 99,9 s = 120 s),
- timer wake: **awake 144 ms** (Arduino 162 ms), spánek 119,8 s,
- bez čidla a bez pull-upu RMT čekal 1 s na timeout (awake 1144 ms) →
  zapnut interní pull-up, teď rychlé `cidlo neodpovida`,
- M5Unified 0.2.x + M5GFX na IDF 6.1 bez úprav; ADC diagnostika přepsaná
  na `adc_oneshot` (zatím neověřena na desce).

Neověřeno: reálné čidlo DS18B20 přes RMT, diagnostika (BtnC), displej
s hodinami po probuzení BtnA.

## FW 0.5.0 — fáze 3: Wi-Fi, NTP, ThingSpeak, portál, flash (2026-10-09)

Moduly: `persist` (NVS: drift, epocha, build, Wi-Fi), `store` (LittleFS:
bloky po 4 h + historie syncu po epochách, kruh 30 dní), `net` (Wi-Fi + vlastní
SNTP dotaz — systémový čas se nepřepisuje), `upload` (bulk_update po ≤ 960,
pauza 16 s, field7 = baterie), `portal` (AP `Vodomer-XXXX`, captive DNS,
esp_http_server, stránka česky). Displej: řádek stavu Wi-Fi (SSID + výsledek
posledního okna, problém červeně), BtnC = sync hned, obrazovka portálu.

- Write key už není na příkazové řádce kompilátoru (`-D`), ale v
  `<build>/vt_gen/vt_secrets.h` — při chybě překladu se příkaz vypisuje
  celý a klíč by skončil v logu (stalo se při prvním buildu 0.5.0, log smazán).
- Epocha = pořadí studeného startu (NVS). Mono čas platí jen v rámci epochy,
  proto bloky ve flash i historie syncu nesou číslo epochy.
- RTC buffer zůstává 24h kruh pro graf; `persisted` = kolik z něj už je ve
  flash / odeslané.

Ověřeno (USB, bez čidla): detekce USB (IP5306 READ0 bit 3), portál po
power-on, sken 10 sítí, AP `Vodomer-1C35`. Aplikace 1,06 MB.

Neověřeno: uložení Wi-Fi přes portál, NTP, odeslání na ThingSpeak, zápis do
flash po 4 h, okna podle napájení, portál po okně bez Wi-Fi, BtnC sync.

## FW 0.6.0 release — noční test na baterii (2026-10-09)

Release build (`tools\build.ps1 -Release -Flash`): bez logu, bootloader tichý.
Nahráno 14:15, první odeslání 14:17:06 (`FW 0.6.0 power-on`, RSSI −57, 2 vzorky).
Napájení: jen Battery modul ~700 mAh (článek bottomu mrtvý, viz hw.md).

Test: odpojit USB večer → FW při dalším probuzení přepne na okna 6/14/22.
Očekávané záznamy v kanálu 3529108: 22:00 a 06:00 (± pár minut po připojení),
field5 = první skutečný drift (úsek 22→06), field7 = baterie (po 25 %).
Ráno: BtnA → `wake #`, `sent HH:MM`, řádek WiFi, BAT. Deska nereaguje →
IP5306 ji vypnul (go/no-go), data do posledního zápisu do flash (≤ 4 h) se
pošlou po zapnutí.

Výsledek: (doplnit)

**16:30 nahrán release 0.6.6** (místo 0.6.0–0.6.5; 0.6.5 měl kvůli rezervaci
GPIO32 nefunkční PWM podsvícení). Při startu 16:26 `cidlo neodpovida` a diag
neviděl na G5 externí pull-up — podezření na uvolněný vodič, uživatel
kontroluje před odchodem. Doba bdění s 0.6.6 nezměřena (dřív 144–178 ms,
očekávání < 60 ms bez M5.begin).

**16:30 nahrán release 0.6.6** (místo 0.6.0–0.6.5; 0.6.5 měl kvůli rezervaci
GPIO32 nefunkční PWM podsvícení). Při startu 16:26 `cidlo neodpovida` a diag
neviděl na G5 externí pull-up — podezření na uvolněný vodič, uživatel
kontroluje před odchodem. Doba bdění s 0.6.6 nezměřena (dřív 144–178 ms,
očekávání < 60 ms bez M5.begin).
