# Vodomer-teplomer

Nízkoenergetický teploměr (DS18B20) na M5Stack **Basic** (ESP32), displej jen
na stisk tlačítka, jinak deep sleep. Port z projektu `ESP-WROOM-02_temp_meas`
(`c:\workspace\mine\programovani\ESP-WROOM-02\ESP-WROOM-02_temp_meas\` —
tam je závazná specifikace fáze 3: ThingSpeak, NTP/drift, portál).

- **Zadání a decision log (závazné): `zadani.md`.** Pracovní poznámky a
  výsledky testů: `poznamky.md`.
- **HW, piny, zapojení, COM porty: `hw.md`.** Než sáhneš na piny, napájení
  nebo spánek, přečti si ho. Architektura a zdůvodnění: `navrh-m5stack-fire.md`
  (psáno pro Fire, platí i pro Basic až na napájení/PSRAM).
- **Framework: čisté ESP-IDF 6.1** (od FW 0.4.0, bez Arduina a PlatformIO),
  instalace `C:\Espressif` (EIM). Zdrojáky v `main/`, komponenty z registru
  v `main/idf_component.yml` (M5Unified, onewire_bus).
- Build: `tools\build.ps1` (debug), `-Release`, `-Flash` (COM10), `-Monitor`
  (bez resetu desky). Spouštět z PowerShellu, ne z Git Bash (IDF odmítá MSYS).
  Build, `sdkconfig` i `managed_components` (junction) jsou mimo Google Drive
  v `%LOCALAPPDATA%\idf-build\vodomer-teplomer`.
- Vodoměr (TMAG5273) NENÍ zadání — neimplementovat bez výslovného pokynu.
- `optional.md`: odložené nápady (heslo AP portálu, přesnější hodiny) — **není zadání**,
  neimplementovat bez pokynu, projít před release.
- Jazyk: dokumentace a komentáře česky (bez diakritiky v kódu, displej font
  neumí české znaky), **UI displeje anglicky**.
- Složka se synchronizuje do Google Drive — žádné secrets (API klíče řešit
  soubor mimo repo + `tools/secrets.cmake`, viz zadani.md).
