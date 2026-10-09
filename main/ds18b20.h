#pragma once

// DS18B20 na sbernici 1-Wire (PIN_ONEWIRE), jedno cidlo, Skip ROM.
// Fyzicka vrstva v HW pres RMT (komponenta espressif/onewire_bus) - timing
// nezavisi na prerusenich, ve fazi 3 ho nerozhodi ani Wi-Fi.

#include <stdint.h>

#include "onewire_bus.h"

enum class DsStatus : uint8_t {
    Ok,
    NoPresence,   // na resetu nikdo neodpovedel
    CrcError,     // scratchpad s chybnym CRC
    PowerOn85,    // vychozi hodnota po zapnuti cidla (prevod neprobehl)
    WrongConfig,  // nepouzito, rezerva
};

// Sbernice otevrena po dobu zivota objektu (RMT kanaly se uvolni v destruktoru,
// pred deep sleep). Prevod v cidle bezi dal i po zavreni sbernice.
class DsBus {
public:
    DsBus();
    ~DsBus();
    DsBus(const DsBus &) = delete;
    DsBus &operator=(const DsBus &) = delete;

    onewire_bus_handle_t handle() const { return bus_; }

    // Zavrit a znovu otevrit (po zaseknuti RMT prijmu, viz ds18b20.cpp).
    void reopen();

private:
    void open();
    void close();

    onewire_bus_handle_t bus_ = nullptr;
};

// Skutecne rozliseni cidla z posledniho cteni (pro log, kdyz klon ignoruje 10 bit).
extern int dsLastBits;

const char *dsStatusName(DsStatus st);

// Zapise 10 bit do scratchpadu (bez kopie do EEPROM). false = cidlo neodpovida.
bool dsSetResolution10(DsBus &ow);

// Spusti prevod a hned se vrati (nececka) - ESP mezitim spi.
bool dsStartConversion(DsBus &ow);

// Precte vysledek posledniho prevodu. config_ok = cidlo ma nastavenych 10 bit.
DsStatus dsRead(DsBus &ow, int16_t &raw, bool &config_ok);
