# Zadání: Vodomer-teplomer (M5Stack, ESP32)

Nízkoenergetický teploměr s displejem na vyžádání. Port FW z projektu
`ESP-WROOM-02_temp_meas` (`c:\workspace\mine\programovani\ESP-WROOM-02\ESP-WROOM-02_temp_meas\`)
na desky M5Stack. Název složky zmiňuje vodoměr — **odečet vodoměru (TMAG5273)
NENÍ součástí zadání**, neimplementovat bez výslovného pokynu
(viz `optional-vodomer.md` v původním projektu).

## Chování (převzato z původního projektu, beze změny)

- Měření DS18B20 každé **2 minuty** na pevné mřížce, rozlišení 10 bit (0,25 °C),
  čidlo napájené trvale (ne parazitně).
- Úsporný cyklus: při probuzení přečíst výsledek minulého převodu, spustit nový,
  usnout — převod běží, zatímco ESP spí.
- Chybná měření (čidlo neodpoví, 85 °C po power-on, CRC) se vynechávají,
  slot zůstane prázdný (`SAMPLE_MISSING`), počítá se `err_count`.
- Vzorky v bufferu v RTC paměti; časové značky jsou pozice na mřížce
  od `buf_start_mono_us` (žádné vlastní timestampy).
- Fáze 3 (zatím neimplementováno, specifikace v původním `zadani.md`):
  Wi-Fi konfigurační portál, NTP + softwarová korekce driftu (mapování
  mono→unix, NE settimeofday), odeslání na ThingSpeak 3× denně
  (6:00/14:00/22:00), překlopení bufferu do LittleFS (30 dní).

## Klíče ThingSpeak (vlastní kanál)

Projekt má **vlastní kanál ThingSpeak**, oddělený od testovacího kanálu
původního projektu (3517634 „Teploty_test“). Klíče jsou v
`%USERPROFILE%\.thingspeak\vodomer-teplomer.env` (KEY=VALUE), **mimo
repozitář** (složka se synchronizuje do Google Drive):

| proměnná | k čemu |
|---|---|
| `THINGSPEAK_CHANNEL_ID` | ID kanálu |
| `THINGSPEAK_WRITE_API_KEY` | zápis, jde do FW |
| `THINGSPEAK_READ_API_KEY` | čtení (skripty, analýza) |

Soubor `thingspeak.env` ve stejné složce patří původnímu projektu a testovacímu
zařízení, tento projekt ho nepoužívá.

**Připraveno pro posílání dat:** `tools/secrets.cmake` (volá ho
`main/CMakeLists.txt`) načte ID kanálu a Write key a předá je do buildu jako
makra `TS_CHANNEL_ID` a `TS_WRITE_KEY`. Hodnoty nevypisuje; skončí jen
v build adresáři (`compile_commands.json`), který leží mimo Google Drive. Když soubor nebo klíč chybí, build skončí chybou, takže
FW nikdy nezapíše do cizího kanálu. Samotný kód odesílání (Wi-Fi, NTP,
bulk_update) je součást fáze 3. Pole kanálu jsou stejná jako v původním
projektu (`web.md`, kap. 1). Když Write key unikne, vygenerovat v UI nový,
přepsat ho v `.env` a přeflashovat.

## Nové požadavky (M5Stack)

- **Displej jen na stisk tlačítka.** Timer wake displej vůbec neinicializuje.
  Po stisku BtnA: aktuální teplota velkým písmem + **graf za posledních 8 h**
  + baterie + stav. Po **20 s zhasne** a deska spí dál; BtnB zhasne hned.
  Obrazovka se ukáže i po zapnutí napájení (power-on).
- **UI displeje anglicky.** (Kód a dokumentace česky, komentáře bez diakritiky.)
- **Reproduktor vypnutý** — GPIO25 držet v LOW i během deep sleep (RTC hold),
  ať nebzučí a nebere energii. První výslovný požadavek uživatele.
- LED pásky (Fire) zhasnuté, IMU/mikrofon vypnuté.
- **Kratší výdrž baterie se akceptuje** (rozhodnutí 2026-10-09): IP5306 bere
  v klidu jednotky mA, výdrž dny až ~2 týdny, nabíjení ~1× týdně je OK.

## Rozhodnutí (decision log)

| datum | rozhodnutí |
|---|---|
| 2026-10-08 | Cíl: port na M5Stack Fire; návrh v `navrh-m5stack-fire.md`. |
| 2026-10-08 | Deep sleep zachován (ne light sleep) — minimální odchylka od ověřeného FW. Light sleep je záložní zjednodušení, kdyby IP5306 stejně dominoval spotřebě. |
| 2026-10-08 | IP5306 keep-on (SYS_CTL0 bit 1, I2C 0x75) je povinný při každém bootu, s ověřením zápisu — bez něj se deska ve spánku vypne a timer wake nepřijde. |
| 2026-10-08 | Budí jen **BtnA** (GPIO39, ext0 na LOW). HW limit: ext1 umí jen ANY_HIGH/ALL_LOW, „libovolné z více aktivních-LOW tlačítek“ jedním zdrojem nejde. Po probuzení fungují všechna tlačítka. |
| 2026-10-08 | Mono čas = `gettimeofday()` — ESP32 drží systémový čas přes deep sleep, mřížka je spojitá přes všechna probuzení (vč. tlačítka). Odpadla dioda GPIO16→RST i RF/settle mašinérie ESP8266. |
| 2026-10-09 | Displej: timeout **20 s** (ne 30), graf **8 h**, UI anglicky. Slot mřížky během zobrazení → změří se při něm (displayTick) a překreslí. |
| 2026-10-09 | DS18B20 na **GPIO26**; pull-up 4k7 **na 3,3 V, nikdy na 5 V** (GPIO ESP32 nejsou 5V tolerantní; VIH čidla 2,2 V, takže DQ na 3,3 V funguje i při VDD 5 V). GPIO36 nelze (input-only). Port C (GPIO16/17) na Fire nepoužívat — PSRAM. |
| 2026-10-09 | Interní pull-up GPIO26 (~45k) zvažován jako náhrada rezistoru pro krátký kabel — NEimplementováno; na Basicu je 3,3 V na boční liště, externí 4k7 je bez problému. |
| 2026-10-09 | **Cílová deska změněna na M5Stack Basic** (starší kus, 4 MB flash). Fire se vrací do továrního stavu (UIFlow). Kód je společný (od FW 0.4.0 jen Basic, sdkconfig pro 4 MB flash). |
| 2026-10-09 | **Battery modul +700 mAh** se přidá mezi core a bottom Basicu (paralelně k ~110 mAh → ~810 mAh, ~7× výdrž). FW beze změny. Aktualizace: článek bottomu byl mrtvý (< 2,8 V), odpojen — kapacita jen ~700 mAh, viz hw.md. |
| 2026-10-09 | RTC buffer 720 vzorků (24 h); přesun do LittleFS až ve fázi 3. |
| 2026-10-09 | FW 0.2.1: lupnutí repra při probuzení — příčina `gpio_reset_pin()` (zapne pull-up → skok LOW→HIGH→LOW). Nově `speakerOff()` jako první řádek `setup()`: výstup LOW + pull-down nastavit **před** `gpio_hold_dis()`. Lupnutí při zapnutí napájení (pin plave během ROM bootloaderu, náběh zesilovače) FW odstranit neumí. |
| 2026-10-09 | FW 0.3.0: **odhad času na displeji** (`~HH:MM` v záhlaví, Praha CET/CEST) do doby NTP. Zdroj = čas buildu (`tools/buildtime.cmake` → hlavička v build adresáři) + 30 s na nahrání, nastaví se jen při prvním startu nového buildu (značka v NVS), pak běží z mono hodin (drift RC oscilátoru neznámý). Po výpadku napájení `--:--` až do dalšího nahrání. Ve fázi 3 nahradí NTP (`FLAG_TIME_VALID`, bez `~`). |
| 2026-10-09 | FW 0.3.0: **diagnostika pull-upů** — držet BtnC při startu/probuzení BtnA (`main/diag.*`). Interní pull-down/up (~45k) + ADC → odhad externího odporu, živě na displeji i v sériové lince, BtnB/5 min = konec. |
| 2026-10-09 | **Vlastní kanál ThingSpeak** (ne testovací 3517634). Klíče v `%USERPROFILE%\.thingspeak\vodomer-teplomer.env`, do buildu přes `tools/secrets.cmake`. Viz „Klíče ThingSpeak“. |
| 2026-10-09 | **FW 0.4.0: přechod na čisté ESP-IDF 6.1** (z Arduino 2.0.17 / IDF 4.4 v PlatformIO). Důvody: nejnovější IDF, HW 1-Wire přes RMT (`espressif/onewire_bus`, timing odolný vůči Wi-Fi ve fázi 3), nativní API pro fázi 3 (esp_wifi, esp_http_server/client, SNTP). ESP32 rev 1.0 je v IDF 6.x podporovaná. M5Unified 0.2.x se na IDF 6.1 přeloží beze změn. Ovladač DS18B20 vlastní (registrová komponenta `ds18b20` při převodu čeká → rozbila by „převod během spánku“). Oficiální PlatformIO Arduino 3.x/IDF 5+ nepodporuje, pioarduino má s PIO Core 6.2 chybu → build přes `idf.py` (`tools/build.ps1`). Probuzení 144 ms (Arduino 162 ms), aplikace 344 kB (505 kB). Partition: app 2 MB bez OTA + LittleFS 1,9 MB. |
| 2026-10-09 | **DS18B20 přesunut na GPIO5** (FW 0.4.1, uživateli se hodí lépe, druhá strana lišty). G2 zamítnut (strap: pull-up blokuje nahrávání FW), G0 zamítnut (strap boot módu, linka v LOW při startu = deska nenaběhne). |
| 2026-10-09 | Fáze 3 — schváleno: pole ThingSpeak jako v původním projektu + **field7 = úroveň baterie** (IP5306, %, po 25 %). Buffer: RTC → **LittleFS každé 4 h**, 30 dní kruhově, odesílání streamem z flash po dávkách ≤ 960. |
| 2026-10-09 | **Fáze 3 — schválené chování** (převzato z ESP-WROOM-02_temp_meas): NTP při každém syncu, mapování mono→UTC s driftem (bez `settimeofday`), drift do NVS a zpět po studeném startu, lineární přepočet časů vzorků mezi syncy (i zpětně před prvním syncem); odhad času z buildu (`~`) jen jako záloha do prvního NTP; 4 pokusy v okně (další při dalším měření), Wi-Fi 15 s, NTP 10 s; po úspěchu se buffer maže; ThingSpeak bulk_update přes HTTP, úspěch = 202. |
| 2026-10-09 | **Okna syncu podle napájení:** na USB každé **2 h**, na baterii **6:00 / 14:00 / 22:00**. Napájení se pozná z IP5306. |
| 2026-10-09 | **Portál:** jako v původním projektu (captive portál, seznam sítí, časy 3/3/20 min, „Ukončit bez změny“, měří se dál, spouští se po zapnutí napájení nebo bez údajů; údaje v NVS). Navíc: spuštění držením BtnB, displej během portálu sám ukáže AP a zbývající čas, AP **`Vodomer-XXXX`** (otevřená, bez hesla — heslo odloženo do `optional.md`), stránka **česky**. |
| 2026-10-09 | ~~Portál i po neúspěšném okně~~ (zrušeno, FW 0.5.2 — viz níže): když se v celém okně (4 pokusy) nepodaří připojit k Wi-Fi, otevře se portál na 3 min (změna sítě/hesla bez tlačítek). Nikdo se nepřipojí → měří se dál. Odhad ~5 mAh na neúspěšné okno. |
| 2026-10-09 | ~~FW 0.5.1: portál po neúspěšném okně až po 24 h bez Wi-Fi~~ — zrušeno, viz níže. Původně: (`PORTAL_AFTER_NO_WIFI_US`), ne po prvním okně — při slabém signálu (−79 dBm) propadlo jedno okno a portál se otevřel zbytečně. Neúspěšný pokus o sync už **nezapisuje do flash**, data čekají v RTC, flash jen pravidelně po 4 h. |
| 2026-10-09 | **Portál jen po zapnutí napájení a BtnB + BtnA** (FW 0.5.2). Automatický portál po neúspěšných oknech zrušen úplně: uživatel se do něj stejně netrefí a otevřená AP snižuje bezpečnost. Při výpadku routeru (i 48 h) zůstávají SSID a heslo v NVS, deska zkouší každé okno dál a data čekají ve flash (30 dní). Zrušeno i „bez údajů → portál“ — první start po nahrání je stejně power-on. |
| 2026-10-09 | FW 0.6.6 — **revize spotřeby v kódu:** (1) `M5.begin()` jen když je potřeba displej (BtnA, power-on, BtnB/BtnC) — inicializace LCD v M5GFX čeká pevných 120 ms po SLPOUT, což byla většina bdění při probuzení časovačem; IP5306 se obsluhuje vlastní instancí nad `m5::In_I2C`. (2) Flash 80 MHz DIO (jako PIO board). (3) GPIO15 LOW při Wi-Fi. Opravené chyby při tom: `gpio_config()` v IDF 6 pin rezervuje → LEDC odmítl GPIO32 (i v 0.6.5); `&M5.In_I2C` v globálu = nulový ukazatel (pořadí inicializace); tlačítka čtená před `M5.begin()` bez nastaveného vstupu četla „stisknuto“. **Úspora bdění nezměřena** (release nahrán kvůli nočnímu testu); odhad 720 × ~0,1 s × ~60 mA ≈ 1–2 mAh/den — vedle IP5306 (3–15 mA trvale = 70–360 mAh/den) okrajové. Hlavní spotřebič zůstává IP5306 keep-on. |
| 2026-10-09 | FW 0.6.6 — **revize spotřeby v kódu:** (1) `M5.begin()` jen když je potřeba displej (BtnA, power-on, BtnB/BtnC) — inicializace LCD v M5GFX čeká pevných 120 ms po SLPOUT, což byla většina bdění při probuzení časovačem; IP5306 se obsluhuje vlastní instancí nad `m5::In_I2C`. (2) Flash 80 MHz DIO (jako PIO board). (3) GPIO15 LOW při Wi-Fi. Opravené chyby při tom: `gpio_config()` v IDF 6 pin rezervuje → LEDC odmítl GPIO32 (i v 0.6.5); `&M5.In_I2C` v globálu = nulový ukazatel (pořadí inicializace); tlačítka čtená před `M5.begin()` bez nastaveného vstupu četla „stisknuto“. **Úspora bdění nezměřena** (release nahrán kvůli nočnímu testu); odhad 720 × ~0,1 s × ~60 mA ≈ 1–2 mAh/den — vedle IP5306 (3–15 mA trvale = 70–360 mAh/den) okrajové. Hlavní spotřebič zůstává IP5306 keep-on. |
| 2026-10-09 | FW 0.6.5: **probliknutí displeje při každém probuzení** — M5.begin() inicializuje LCD a nastaví výchozí jas dřív, než ho FW ztlumí. Podsvícení (GPIO32) se drží v LOW přes deep sleep i inicializaci (gpio_hold jako u repra), uvolní se až v `wake()` těsně před rozsvícením. |
| 2026-10-09 | **Vypnutí dvojhmatem A + C 3 s** (FW 0.6.3, podle `Vodomer/m5stack-hall-fw`): odpočet na displeji, flush do flash, IP5306 keep-on pryč + prodleva 8 s, deep sleep → na baterii se napájení odpojí (~desítky µA), zapnutí bočním POWER. Dvojklik POWER nefunguje kvůli keep-on. Na USB jen deep sleep, probudí BtnA. |
| 2026-10-09 | **Pole ThingSpeak (FW 0.6.2):** field1 teplota, field2 RSSI, field3 neúspěšná okna, field4 chybná měření, field5 drift [s/den], **field6 baterie [%]**, **field7 a field8 volné** (vodoměr). Počet odeslaných vzorků jen v textu status: `FW 0.6.2 <reset> samples N`. Nahrazuje „field7 = baterie“ výše. Nástěnka `vodomer/` upravena. |
| 2026-10-09 | Zdroj hodin ve spánku zůstává RC 150 kHz — čas slouží jen pro graf, NTP korekce stačí. Přesnější varianta v `optional.md` (relevantní až při párování s jiným dějem, další iterace). |
| 2026-10-09 | **Stav Wi-Fi na displeji:** nastavené SSID a výsledek posledního okna musí být čitelné (neúspěch / nenastaveno zvýrazněně). |
| 2026-10-09 | Ladění: místo UART příkazů `S`/`P` tlačítko na obrazovce (sync teď). Specifika ESP8266 (RF reboot, vyrovnávací spánek, CM4 programátor, simulátor) se nepřenášejí. |
| 2026-10-09 | **Webová nástěnka:** druhá stránka https://js-storage.github.io/home-temperature/vodomer/ (repo `js-storage/home-temperature`, složka `vodomer/`, kanál 3529108, baterie z field7). Původní nástěnka beze změny, odkaz v README jako druhý. Lokální klon `C:\workspace\mine\home-temperature`. **Aktualizace:** hlavní místo je od teď `docs/index.html` v tomto repu (https://barkingbunny.github.io/vodomer-teplomer/), kopie v `home-temperature/vodomer/` se dočasně udržuje a časem smaže. |
| 2026-10-09 | 1-Wire: zapnutý i interní pull-up (~45k) vedle externího 4k7 — při odpojeném čidle jinak RMT čeká 1 s na timeout při každém probuzení (awake 1144 ms místo 144 ms). |

## Stav a otevřené body

- Hotovo: milník 1 (deep sleep + napájení) a milník 2 (měření + displej);
  od FW 0.4.0 na ESP-IDF 6.1, běží na Basicu — výsledky testů v `poznamky.md`.
- **Otevřeno — go/no-go: noční test na baterii bez USB** (na USB se IP5306
  nikdy nevypíná, USB test nic nedokazuje). Ráno BtnA → `wake #` musí
  odpovídat době běhu.
- Otevřeno: připojit reálný DS18B20 (zatím všechna měření `no-presence`).
- Otevřeno: fáze 3 (Wi-Fi/NTP/ThingSpeak/portál/LittleFS), fáze 4 (výdrž).
- Fire: čeká na obnovu továrního FW (postup v `poznamky.md`).
