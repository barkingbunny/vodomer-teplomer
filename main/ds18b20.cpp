#include "ds18b20.h"

#include "config.h"
#include "log.h"
#include "onewire_crc.h"

namespace {

constexpr uint8_t CMD_SKIP_ROM       = 0xCC;
constexpr uint8_t CMD_CONVERT_T      = 0x44;
constexpr uint8_t CMD_READ_SCRATCH   = 0xBE;
constexpr uint8_t CMD_WRITE_SCRATCH  = 0x4E;

// Konfiguracni registr: R1=0, R0=1 -> 10 bit.
constexpr uint8_t CONFIG_10BIT = 0x3F;
constexpr uint8_t CONFIG_MASK  = 0x60;

// Vychozi hodnota teplotniho registru po zapnuti napajeni.
constexpr int16_t RAW_POWER_ON = 0x0550;

bool select(DsBus &ow) {
    if (!ow.handle()) return false;
    esp_err_t err = onewire_bus_reset(ow.handle());
    if (err != ESP_OK) {
        // ESP_ERR_NOT_FOUND = nikdo neodpovedel (bezne). Jina chyba = linka
        // drzena v LOW (chybi pull-up, zkrat): RMT prijem po 1 s timeoutu zustane
        // viset a dalsi pokus by selhal hned - sbernici vytvorit znovu.
        if (err != ESP_ERR_NOT_FOUND) ow.reopen();
        return false;
    }
    uint8_t cmd = CMD_SKIP_ROM;
    return onewire_bus_write_bytes(ow.handle(), &cmd, 1) == ESP_OK;
}

bool send(DsBus &ow, const uint8_t *data, uint8_t len) {
    return onewire_bus_write_bytes(ow.handle(), data, len) == ESP_OK;
}

}  // namespace

DsBus::DsBus() {
    open();
}

DsBus::~DsBus() {
    close();
}

void DsBus::reopen() {
    close();
    open();
}

void DsBus::open() {
    onewire_bus_config_t bus_cfg = {};
    bus_cfg.bus_gpio_num = gpio_num_t(PIN_ONEWIRE);
    // Hlavni pull-up 4k7 je externi. Interni (~45k) paralelne nevadi a pri
    // odpojenem cidle drzi linku v HIGH -> rychle "no presence" misto 1 s
    // timeoutu RMT pri kazdem probuzeni (setri baterii).
    bus_cfg.flags.en_pull_up = true;
    onewire_bus_rmt_config_t rmt_cfg = {};
    rmt_cfg.max_rx_bytes = 10;  // scratchpad 9 B
    if (onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &bus_) != ESP_OK) {
        bus_ = nullptr;
        LOG("[ds] RMT sbernici nelze vytvorit");
    }
}

void DsBus::close() {
    if (bus_) onewire_bus_del(bus_);
    bus_ = nullptr;
}

int dsLastBits = 10;

const char *dsStatusName(DsStatus st) {
    switch (st) {
        case DsStatus::Ok:          return "ok";
        case DsStatus::NoPresence:  return "no-presence";
        case DsStatus::CrcError:    return "crc";
        case DsStatus::PowerOn85:   return "power-on-85";
        case DsStatus::WrongConfig: return "wrong-config";
    }
    return "?";
}

bool dsSetResolution10(DsBus &ow) {
    if (!select(ow)) return false;
    // TH, TL - alarmy nepouzivame.
    const uint8_t cmd[] = {CMD_WRITE_SCRATCH, 0x4B, 0x46, CONFIG_10BIT};
    // Do EEPROM cidla se nekopiruje: je napajene trvale a pri kazdem cteni
    // kontrolujeme konfiguraci, takze ztratu nastaveni pozname.
    return send(ow, cmd, sizeof cmd);
}

bool dsStartConversion(DsBus &ow) {
    if (!select(ow)) return false;
    // Cidlo je napajene ze 3V3, silny pull-up behem prevodu neni potreba.
    const uint8_t cmd = CMD_CONVERT_T;
    return send(ow, &cmd, 1);
}

DsStatus dsRead(DsBus &ow, int16_t &raw, bool &config_ok) {
    if (!select(ow)) return DsStatus::NoPresence;
    const uint8_t cmd = CMD_READ_SCRATCH;
    if (!send(ow, &cmd, 1)) return DsStatus::NoPresence;

    uint8_t sp[9] = {};
    if (onewire_bus_read_bytes(ow.handle(), sp, sizeof sp) != ESP_OK) return DsStatus::NoPresence;
    uint8_t crc = onewire_crc8(0, sp, 8);
    if (crc != sp[8]) {
        LOG("[ds] scratchpad %02X %02X %02X %02X %02X %02X %02X %02X %02X, crc %02X",
            sp[0], sp[1], sp[2], sp[3], sp[4], sp[5], sp[6], sp[7], sp[8], crc);
        return DsStatus::CrcError;
    }

    raw = int16_t(uint16_t(sp[1]) << 8 | sp[0]);
    if (raw == RAW_POWER_ON) return DsStatus::PowerOn85;

    // Rozliseni podle skutecne konfigurace. Nektere klony DS18B20 (ROM 28 FF ...)
    // zapis rozliseni ignoruji a meri vzdy 12 bit - mereni je i tak platne.
    int bits = 9 + ((sp[4] >> 5) & 3);
    config_ok = (sp[4] & CONFIG_MASK) == (CONFIG_10BIT & CONFIG_MASK);
    if (!config_ok) dsLastBits = bits;

    // Nizsi bity jsou pri mensim rozliseni nedefinovane.
    raw &= ~int16_t((1 << (12 - bits)) - 1);
    return DsStatus::Ok;
}
