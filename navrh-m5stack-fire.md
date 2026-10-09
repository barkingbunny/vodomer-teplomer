# Návrh: port teploměru na M5Stack Fire (ESP32)

Návrh přenosu FW z projektu `ESP-WROOM-02_temp_meas` (deska FDM-MK4-WiFi, ESP8266)
na **M5Stack Fire** (ESP32, baterie, displej, 3 tlačítka). Chování beze změny:
měření DS18B20 každé 2 min, buffer, odeslání na ThingSpeak 3× denně, NTP čas
s korekcí driftu, konfigurační portál. **Nově: displej, který se rozsvítí jen
po stisku tlačítka; jinak deska spí v deep sleep a čeká na další měření.**

Výchozí projekt: `c:\workspace\mine\programovani\ESP-WROOM-02\ESP-WROOM-02_temp_meas\`
(zadání `zadani.md`, HW popis `hw/zapojeni-llm.md`).

Poznámka k názvu složky: volitelný odečet vodoměru (TMAG5273, viz
`optional-vodomer.md` v původním projektu) **není součástí tohoto návrhu** —
nepřidávat bez výslovného pokynu.

---

## 1. HW rozdíly, které mění návrh

| | FDM-MK4-WiFi (ESP8266) | M5Stack Fire (ESP32) |
|---|---|---|
| MCU | ESP-WROOM-02D, 2 MB flash | ESP32-D0WDQ6, 16 MB flash, 4 MB PSRAM |
| Probuzení z deep sleep | jen přes HW úpravu (dioda GPIO16→RST) | nativně: RTC timer + tlačítka (RTC GPIO), **žádná úprava HW** |
| RTC paměť pro buffer | 512 B | 8 KB (RTC slow mem, `RTC_DATA_ATTR`) |
| Napájení | baterie + LDO (Iq v µA), klid ~20 µA | Li-pol ~500 mAh přes **IP5306** (boost 5 V) — viz oddíl 2, zásadní omezení |
| Displej | žádný | ILI9342C 320×240 (SPI), podsvícení GPIO32 |
| Tlačítka | žádná | BtnA=GPIO39, BtnB=GPIO38, BtnC=GPIO37 (RTC piny, aktivní LOW) |
| DS18B20 | IO2, pull-up R1 10k na desce | **Grove Port B: DQ na GPIO26**, pull-up a napájení dodat externě (oddíl 3) |
| Volné periferie navíc | — | LED pásky SK6812 (GPIO15), repro (GPIO25), SD slot — ve FW uspat/umlčet |

Pozor: **Port C (GPIO16/17) na Fire nepoužívat** — piny kolidují s PSRAM.
Čidlo patří na Port B.

## 2. Klíčový problém: IP5306 vs. deep sleep

Fire nenapájí ESP32 přímo z baterie, ale přes boost IC **IP5306** (verze s I2C,
adresa 0x75). Dva důsledky:

1. **IP5306 při malém odběru sám vypne výstup** (po ~30–60 s). ESP32 v deep
   sleep odebírá mikroampéry → IP5306 by desku vypnul a **timer wake by už
   nikdy nepřišel**. Nutné při každém bootu zapnout „keep on" přes I2C —
   s M5Unified to řeší `M5.Power` (power hold / boost keep-on). Bez toho
   koncept vůbec nefunguje.
2. **Boost běží trvale** → klidový odběr desky v deep sleep není µA, ale řádově
   **jednotky mA** (odhad 3–15 mA, změřit). Z ~500 mAh baterie to je zhruba
   **2–7 dní provozu**, ne měsíce jako u původní desky.

Doporučení: počítat s tím od začátku — buď Fire provozovat na USB napájení
(baterie jen jako záloha), nebo akceptovat nabíjení ~1× týdně. První milník
projektu je **změřit skutečný klidový odběr** (viz oddíl 8).

Protože spotřebu stejně dominuje IP5306, je na zvážení i **light sleep místo
deep sleep** (ESP32 light sleep ~1 mA navíc, RAM přežije, program pokračuje bez
rebootu → odpadá celá RTC-state mašinérie). Návrh níže zachovává deep sleep
(minimální odchylka od ověřeného FW), light sleep je záložní zjednodušení.

## 3. Zapojení DS18B20

- **DQ → GPIO26** (Grove Port B, bílý/žlutý vodič dle kabelu).
- **Pull-up 4k7 z DQ na 3,3 V** (ne na 5 V! GPIO ESP32 nejsou 5V tolerantní).
  3,3 V je na M-Bus konektoru; případně pull-up zapojit uvnitř kabelu/redukce.
- **VDD čidla: 3,3 V (M-Bus) nebo 5 V (Grove)** — DS18B20 snese 3,0–5,5 V,
  napájet trvale (ne parazitně), stejně jako dosud. Při VDD=5 V pull-up DQ
  zůstává na 3,3 V (VIH čidla je 2,2 V, funguje).
- GPIO36 (druhý pin Portu B) je input-only, pro 1-Wire nepoužitelný — nechat volný.

## 4. Logika probuzení a displeje

Deep sleep se nastavuje se **dvěma zdroji probuzení současně**:

- `esp_sleep_enable_timer_wakeup(t)` — do dalšího slotu měřicí mřížky (2 min),
- `esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0)` — **BtnA** (aktivní LOW).

Pozn.: ext1 na ESP32 umí jen ANY_HIGH / ALL_LOW, takže „libovolné z tří
aktivních-LOW tlačítek" jedním zdrojem nejde. Budí **jen BtnA**; po probuzení
displeje už fungují všechna tři tlačítka normálně (čtení přes M5.BtnA/B/C).

Po probuzení rozhodne `esp_sleep_get_wakeup_cause()` + `esp_reset_reason()`:

| příčina | akce |
|---|---|
| TIMER | stávající cesta: přečíst výsledek převodu, spustit nový, případně sync okno, spát. Displej zůstane zhasnutý (neinicializuje se → žádná spotřeba navíc). |
| EXT0 (BtnA) | **režim displeje** (níže). Měření se nenaruší: pokud mezitím uplynul slot mřížky, doměřit; jinak jen zobrazovat. Před usnutím spánek dopočítat do dalšího slotu. |
| power-on / reset | studený start: portál (stejná logika jako dosud — na ESP32 se power-on pozná spolehlivě přes `ESP_RST_POWERON`, bez ESP8266 kompromisů). Navíc lze portál vyvolat podržením BtnB při zapnutí. |

**Režim displeje:**

- Zapnout LCD + podsvícení, zobrazit: aktuální (poslední) teplota velkým písmem,
  mini-graf z bufferu v RTC paměti (okamžitě k dispozici, bez čtení flash),
  stav: počet vzorků v bufferu, čas posledního úspěšného odeslání, RSSI
  z posledního syncu, stav baterie (IP5306 přes I2C, po 25 %).
- BtnA/BtnC = listování (graf 3 h / 24 h / stav), BtnB dlouze = zhasnout hned.
- Timeout nečinnosti **30 s** → zhasnout a spát do dalšího slotu mřížky.
- Během zobrazení se o slotech mřížky dál měří (stejný princip jako
  `portalTick()` u portálu).

## 5. Změny ve FW po modulech

Struktura a logika zůstává; mění se platformní vrstva. Většina modulů je
přenositelná beze změny logiky.

| modul | změna |
|---|---|
| `platformio.ini` | nové prostředí: `platform = espressif32`, `board = m5stack-fire`, `board_build.filesystem = littlefs` + partition tabulka (16 MB: app + LittleFS), lib_deps + `m5stack/M5Unified`. Zachovat debug/release varianty a `tools/secrets.py`. Upload přímo přes USB (Fire má USB-UART) — **CM4 programátor není potřeba.** |
| `main.cpp` | větvení podle wakeup cause (oddíl 4); **vypadne celá RF mašinérie ESP8266** (`rfReboot`, `FLAG_RF_NEXT/RF_REBOOT/SETTLE/POST_SETTLE`, vyrovnávací spánky) — ESP32 nemá WAKE_RF_DISABLED problém, Wi-Fi se prostě inicializuje jen když je sync okno. Přibude volání `M5.begin()` (konfigurované bez LCD při timer wake) a IP5306 keep-on. |
| `rtc_state.*` | místo `system_rtc_mem_read/write` atribut `RTC_DATA_ATTR static RtcState` (přežívá deep sleep, maže se power-onem — stejné chování jako teď, CRC+magic zachovat). Buffer lze zvětšit z ~220 na tisíce vzorků (8 KB), tím řidší zápisy do flash. |
| `clock.* / timebase.*` | logika (mono čas, NTP, drift ppb) beze změny. Smazat kompenzace specifické pro ESP8266 (`POST_SETTLE_FAST_PPM`, settle). ESP32 RTC v deep sleep běží také z RC ~150 kHz s driftem — **mechanismus měření driftu přes NTP se hodí beze změny**, jen se znovu změří konstanty. |
| `ds18b20.*` | beze změny, jen `PIN_ONEWIRE = 26`. |
| `net.*` | `ESP8266WiFi.h` → `WiFi.h`; `netHasCredentials()` přepsat (jiné SDK) — nejčistší je uložit SSID/heslo explicitně do NVS (`Preferences`) místo spoléhání na persistenci SDK. NTP část beze změny. |
| `portal.*` | `ESP8266WebServer` → `WebServer`, jinak beze změny (HTML, časy, chování). Spouštěč: power-on nebo BtnB při startu. |
| `persist.* / upload.*` | beze změny (LittleFS i na ESP32, ThingSpeak protokol stejný). Jen include `LittleFS.h` a partition. |
| `log.h` | beze změny (Serial přes USB). |
| **`display.*` (nový)** | M5GFX/M5Unified; obrazovky dle oddílu 4; žádná logika měření, jen čte `RtcState`. |
| **`power.*` (nový)** | IP5306: keep-on při bootu, čtení stavu baterie; umlčet repro (GPIO25), zhasnout SK6812 (GPIO15). |

## 6. Co se nemění (záměrně)

- Mřížka měření 2 min, rozlišení 10 bit, zacházení s chybnými vzorky.
- Sync okna 6:00 / 14:00 / 22:00 (debug: každé 2 h), tolerance, retry logika.
- ThingSpeak bulk_update, mapování polí field1–field8 + status, streamování z flash.
- Portál UX (AP TempMeas-XXXX, časy 3/3/20 min, měření během portálu).
- NTP + softwarová korekce driftu, zpětné dopočítání času.

## 7. Spotřeba — očekávání (ověřit měřením)

| stav | ESP8266 deska | M5Stack Fire (odhad) |
|---|---|---|
| deep sleep | ~20 µA | **3–15 mA** (IP5306 keep-on + periferie) |
| měření (probuzení ~200 ms) | ~70 mA špička | podobné, delší boot ESP32 |
| Wi-Fi sync | 70–350 mA | podobné |
| displej zapnutý | — | ~100–150 mA (LCD + podsvícení) |

Výdrž z ~500 mAh: realisticky **2–7 dní** (vs. měsíce u původní desky).
Fire je tedy spíš „přenosný zobrazovač + logger s občasným nabíjením",
ne dlouhodobě bateriové čidlo. Pokud má běžet trvale, napájet USB.

## 8. Postup prací (milníky)

1. **Ověření napájení**: holý sketch — IP5306 keep-on, deep sleep 2 min,
   timer wake, počítadlo v RTC paměti. Změřit klidový odběr a potvrdit, že
   deska přežije noc v deep sleep bez vypnutí IP5306. *Go/no-go celého návrhu.*
2. Port platformní vrstvy: `rtc_state`, `clock` (bez settle), deep sleep +
   wakeup cause, DS18B20 na GPIO26. Ověřit mřížku měření proti reálnému času.
3. Wi-Fi/NTP/upload + portál (WebServer), NVS kredenciály. Ověřit sync okna
   a drift na ESP32 (nové konstanty do poznámek).
4. Displej: probuzení BtnA, obrazovky, timeout, měření během zobrazení.
5. Dlouhodobý test výdrže + kalibrace odhadů; aktualizovat tenhle dokument
   o naměřené hodnoty.

## 9. Otevřené otázky

- Stačí buzení jen tlačítkem BtnA, nebo chtít všechna tři? (HW limit ext0/ext1
  u aktivních-LOW tlačítek — šlo by obejít jen přes light sleep + gpio wake.)
- Deep sleep vs. light sleep: pokud měření v bodě 1 ukáže, že IP5306 žere
  jednotky mA tak jako tak, light sleep zjednoduší kód za ~1 mA navíc.
- Revize M5Stack Fire (starší kusy mají jiné zapojení LED/baterie) — ověřit
  podle konkrétního kusu.
- Napájení čidla: Grove 5 V vs. M-Bus 3,3 V — podle toho, jak bude veden kabel.
