# HW: desky, piny, zapojení

Strojově čitelný popis HW pro tento FW. Pozadí a zdůvodnění: `navrh-m5stack-fire.md`
(odd. 1–3), rozhodnutí: `zadani.md`.

## Desky

| | M5Stack Fire | M5Stack Basic (náš kus) |
|---|---|---|
| MCU | ESP32-D0WDQ6 v3, 16 MB flash, 4 MB PSRAM | ESP32-D0WDQ6 **v1.0**, **4 MB flash**, bez PSRAM |
| USB-UART | CH9102, **COM5** | CP2104, **COM10** |
| MAC | 08:3a:f2:43:a6:58 | 98:f4:ab:6c:1c:34 |
| baterie | ~500 mAh (M5GO bottom) | **jen Battery modul ~700 mAh** (článek bottomu mrtvý, odpojen 2026-10-09) |
| IP5306 | I2C verze (0x75) | **I2C verze potvrzena** (keep-on zápis prošel, 2026-10-09) |
| build | (FW 0.4.0+ jen Basic) | `tools\build.ps1 [-Release] -Flash` |
| stav | vrací se do továrního FW (UIFlow) | **cílová deska projektu** |

Battery modul se stohuje mezi core a bottom (M-Bus), paralelně k baterii
bottomu. FW ho nevidí — jen větší kapacita.

**Stav 2026-10-09:** vestavěný článek bottomu (~110 mAh) byl mrtvý — pod 2,8 V
a napětí dál klesalo. Je odpojený, deska běží **jen z Battery modulu (~700 mAh)**.
Hluboce vybitý Li-pol (< ~3,0 V) znovu nenabíjet (riziko), vyměnit. Úroveň
baterie z IP5306 (field7, displej) teď odpovídá jen modulu.

## Piny (společné pro obě desky)

| pin | funkce | poznámka |
|---|---|---|
| GPIO5 | **1-Wire DS18B20 (DQ)** | `PIN_ONEWIRE` v `main/config.h`; HW přes RMT, + interní pull-up. Strap pin (SDIO timing, výchozí HIGH) — pull-up nevadí |
| GPIO2 | volný | **ne pro 1-Wire:** pull-up by při nahrávání (G0 v LOW) zablokoval bootloader |
| GPIO26 | volný | do FW 0.4.0 1-Wire |
| GPIO39 | BtnA | **budí z deep sleep** (ext0, aktivní LOW) |
| GPIO38 | BtnB | jen při zapnutém displeji (zhasnout hned) |
| GPIO25 | reproduktor (DAC1) | **držet LOW i ve spánku** (gpio_hold_en), jinak bzučí |
| GPIO32 | podsvícení LCD | řeší M5GFX (setBrightness) |
| GPIO15 | SK6812 LED pásky (jen Fire) | nechat tmavé (`led_brightness = 0`) |
| GPIO16/17 | PSRAM (jen Fire) | **nepoužívat** (na Fire blokuje Port C) |
| GPIO36 | volný (Grove Port B žlutý) | input-only, pro 1-Wire nepoužitelný |
| GPIO37 | BtnC | **držet při startu = diagnostika pull-upů** (`main/diag.h`) |

## Diagnostika pull-upů (FW 0.3.0+)

Držet BtnC při zapnutí / resetu, nebo držet BtnC a stisknout BtnA. Deska proměří
volné piny lišty (0, 2, 5, 12, 13, 15, 16, 17, 26, 34–36) a jako referenci I2C
21/22. Odhad odporu: interní pull-down ~45k (rozptyl 30–80k) + ADC — slouží pro
rozhodnutí „je / není“, ne pro přesnou hodnotu. Piny bez ADC (5, 16, 17, 21, 22)
jen digitálně (pull-up < ~15k), 34–36 nemají interní pully (jen napětí).

## Zapojení DS18B20

Tři vodiče (VDD, DQ, GND), napájení trvalé. **Pull-up 4k7 z DQ na 3,3 V —
nikdy na 5 V** (GPIO ESP32 nejsou 5V tolerantní; čidlu nevadí DQ na 3,3 V
i při VDD 5 V, jeho VIH je 2,2 V).

**Basic (aktuální cesta):** boční 2,54mm dutinková lišta na spodním dílu —
DuPont vodiče: GND→GND, 3V3→VDD čidla, **G5→DQ**, 4k7 mezi DQ a 3V3
(od FW 0.4.1; G5 je na druhé straně lišty než G26).

**Fire (kdyby se k němu vracelo):** Grove Port B (HY2.0-4P, DuPont se nevejde —
Grove2DuPont redukce nebo rozstřižený Grove kabel): černý=GND, červený=5V→VDD,
**bílý=G26→DQ**, žlutý (G36) nezapojovat. 3,3 V pro pull-up jen z M-Bus
(u Fire schovaný pod bottomem → proto přechod na Basic).

Pozor na hotový Seeed Grove DS18B20 modul: signál má na žlutém vodiči (G36,
input-only) a pull-up na 5 V — přímo použít nejde.

## IP5306 (napájecí boost, I2C 0x75)

- Při malém odběru (deep sleep) **sám vypne výstup** po ~30–60 s → při každém
  bootu nastavit keep-on: `SYS_CTL0 (0x00)` bit 1 (`0x02`), s ověřením zápisu
  (`main/power.cpp`). M5Unified to v `M5.begin()` dělá taky; náš kód je pojistka.
- Keep-on = boost běží trvale → klidový odběr jednotky mA (odhad 3–15 mA,
  dosud nezměřeno). Výdrž = kapacita / tento proud.
- Stav baterie jen po 25 % (`M5.Power.getBatteryLevel()`); při USB hlásí 100.
- **Na USB se IP5306 nikdy nevypíná** — bateriové chování lze testovat jen
  s odpojeným USB.

## Vypnutí desky (FW 0.6.3+)

**Dvojhmat: na rozsvíceném displeji držet BtnA + BtnC 3 s** (odpočet na
displeji). FW zapíše neuložené vzorky do flash, pustí keep-on IP5306
(`SYS_CTL0` bit 1), zkrátí prodlevu vypnutí na 8 s (`SYS_CTL2` bity 2–3 = 00)
a usne bez časovače → IP5306 při malém odběru do ~8 s odpojí napájení.
Spotřeba pak jen klidový proud IP5306 (desítky µA). Zapnutí: **boční tlačítko
POWER** (= power-on → portál 3 min, BtnB ho zavře; data z flash se odešlou
při prvním syncu).

- **Na USB IP5306 nevypíná** — deska zůstane v deep sleep, probudí ji BtnA
  (studený start bez portálu, měří dál).
- Vypínání samotným IP5306 (dvojklik / podržení POWER) **nefunguje**, protože
  FW drží keep-on, aby deska přežila deep sleep. Stejné řešení má
  `c:\workspace\mine\Vodomer\m5stack-hall-fw` (A+C 2 s).

## Obnova Fire do továrního stavu

1. Stáhnout oficiální UIFlow bin (16MB basic variantu) z
   https://github.com/m5stack/uiflow_micropython/releases
2. `esptool --chip esp32 --port COM5 --baud 460800 erase_flash`
3. `esptool --chip esp32 --port COM5 --baud 460800 write_flash --flash_mode dio
   --flash_size 16MB 0x0 <bin>`
   (921600 Bd na CH9102 selhává — nepoužívat; alternativa: M5Burner GUI)
