# Vodomer-teplomer

Nízkoenergetický teploměr (DS18B20) na **M5Stack Basic** (ESP32), čisté
**ESP-IDF 6.1**. Měří každé 2 min, spí v deep sleep, 3× denně (na USB každé
2 h) pošle data na ThingSpeak. Displej se rozsvítí jen po stisku tlačítka.

## Měřicí stránka (nástěnka)

**https://barkingbunny.github.io/vodomer-teplomer/** — hlavní místo
(zdroj: [docs/index.html](docs/index.html), GitHub Pages z větve `main`,
složka `/docs`).

Dočasně běží i stará kopie https://js-storage.github.io/home-temperature/vodomer/
(repo `js-storage/home-temperature`); udržují se obě, ta druhá se časem smaže.

Stránka je jeden statický soubor: čte veřejný kanál ThingSpeak 3529108 přímo
z prohlížeče, bez klíčů. Graf 24 h / 7 dní / vše, min/max/průměr, epizody nad
prahem, stav zařízení (poslední odeslání, Wi-Fi, chyby, drift, baterie).
Jiný kanál: `?ch=<id>`, demo data: `?demo`.

- Zadání a decision log: [zadani.md](zadani.md) · HW a zapojení: [hw.md](hw.md)
  · poznámky z testů: [poznamky.md](poznamky.md) · odložené nápady: [optional.md](optional.md)

## Zprovoznění od nuly (Windows)

### 1. ESP-IDF 6.1

```powershell
winget install Espressif.EIM-CLI
eim install -i v6.1 -t esp32 -p C:\Espressif
```

`tools\build.ps1` si prostředí aktivuje sám
(`C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`).

### 2. Klíče ThingSpeak (mimo repozitář)

Build bez nich skončí chybou. Vytvoř soubor
**`%USERPROFILE%\.thingspeak\vodomer-teplomer.env`**:

```
THINGSPEAK_CHANNEL_ID=1234567
THINGSPEAK_WRITE_API_KEY=XXXXXXXXXXXXXXXX
THINGSPEAK_READ_API_KEY=XXXXXXXXXXXXXXXX
```

- `CHANNEL_ID` a `WRITE_API_KEY` načte `tools/secrets.cmake` a zapíše je do
  hlavičky `vt_secrets.h` v build adresáři (mimo projekt). Do repozitáře ani
  do výpisu překladu se nedostanou.
- `READ_API_KEY` FW nepotřebuje (jen pro skripty / soukromý kanál).
- Pole kanálu: field1 teplota, field2 RSSI, field3 neúspěšná okna, field4 chybná
  měření, field5 drift [s/den], field6 baterie [%], field7–8 volné.
  Nástěnka čte veřejný kanál bez klíče.

### 3. Build a nahrání

```powershell
tools\build.ps1                    # debug build (log na UART)
tools\build.ps1 -Flash             # + nahrát (COM10, přepnout parametrem -Port)
tools\build.ps1 -Release -Flash    # ostrá verze bez logu
tools\build.ps1 -Monitor           # sériový monitor bez resetu desky
```

Spouštět z **PowerShellu** (z Git Bash idf.py odmítne MSYS). Build,
`sdkconfig` i stažené komponenty (`managed_components` je junction) jdou do
`%LOCALAPPDATA%\idf-build\vodomer-teplomer`.

### 4. Zapojení

DS18B20 na boční lištu: `G5` → DQ, `3V3` → VDD, `GND` → GND, **4k7 mezi DQ
a 3V3**. Podrobně v [hw.md](hw.md).

## Ovládání

| akce | co udělá |
|---|---|
| BtnA | rozsvítí displej na 20 s (teplota, graf 8 h, baterie, stav Wi-Fi) |
| BtnB | zhasne |
| BtnC (na displeji) | synchronizace hned (Wi-Fi, NTP, odeslání) |
| BtnA + BtnC 3 s | vypnutí (odpočet); zapnutí bočním POWER |
| BtnB držet + BtnA | konfigurační portál Wi-Fi (AP `Vodomer-XXXX`, http://192.168.4.1) |
| BtnC držet + BtnA | diagnostika pull-upů na GPIO |

Portál se otevře i po každém zapnutí napájení (3 min, BtnB zavře).
