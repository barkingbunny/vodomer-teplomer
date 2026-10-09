#include "diag.h"

#include <M5Unified.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "clock.h"
#include "config.h"
#include "log.h"

namespace {

struct DiagPin {
    gpio_num_t pin;
    bool adc;          // ma ADC (kanal se zjisti z cisla pinu)
    bool pulls;        // ma interni pull-up/down (GPIO34-39 ne)
    bool in_use;       // pouziva ho jina periferie - nemenit smer, jen pully
    const char *note;
};

// Volne piny na liste Basicu + I2C jako reference (pull-upy tam urcite jsou).
// Vynechano: 1/3 UART, 18/19/23 SPI LCD, 14/27/32/33 LCD, 4 SD, 25 repro,
// 37/38/39 tlacitka, 6-11 flash.
constexpr DiagPin PINS[] = {
    {GPIO_NUM_26, true, true, false, ""},
    {GPIO_NUM_0, true, true, false, "strap"},
    {GPIO_NUM_2, true, true, false, "strap"},
    {GPIO_NUM_12, true, true, false, "strap"},
    {GPIO_NUM_13, true, true, false, ""},
    {GPIO_NUM_15, true, true, false, "strap"},
    {GPIO_NUM_5, false, true, false, "1-Wire"},
    {GPIO_NUM_16, false, true, false, ""},
    {GPIO_NUM_17, false, true, false, ""},
    {GPIO_NUM_34, true, false, false, "in-only"},
    {GPIO_NUM_35, true, false, false, "in-only"},
    {GPIO_NUM_36, true, false, false, "in-only"},
    {GPIO_NUM_21, false, true, true, "I2C ref"},
    {GPIO_NUM_22, false, true, true, "I2C ref"},
};
constexpr int PIN_COUNT = sizeof PINS / sizeof PINS[0];

constexpr float R_INT_K = 45.0f;      // nominalni interni pull-up/down
constexpr int VDD_MV = 3300;
constexpr int SETTLE_MS = 20;         // ustaleni (i pres pripadny kondenzator)
constexpr uint32_t DIAG_TIMEOUT_MS = 5 * 60 * 1000;
constexpr uint32_t REFRESH_MS = 2000;

// ADC jednotky a kalibrace (index 0 = ADC1, 1 = ADC2). cal == nullptr ->
// kalibrace nedostupna, prepocet jen linearne (horsi presnost).
adc_oneshot_unit_handle_t adc_unit[2];
adc_cali_handle_t adc_cal[2];

struct Result {
    int pd;  // s internim pull-down: mV (ADC) nebo uroven 0/1
    int pu;  // s internim pull-up
};

void setPulls(gpio_num_t pin, bool up, bool down) {
    // IDF smeruje RTC piny na rtc_gpio pully - funguje i v rezimu ADC.
    if (up) gpio_pullup_en(pin); else gpio_pullup_dis(pin);
    if (down) gpio_pulldown_en(pin); else gpio_pulldown_dis(pin);
}

void adcInit() {
    for (int u = 0; u < 2; u++) {
        adc_unit_t id = u == 0 ? ADC_UNIT_1 : ADC_UNIT_2;
        adc_oneshot_unit_init_cfg_t ucfg = {};
        ucfg.unit_id = id;
        if (adc_oneshot_new_unit(&ucfg, &adc_unit[u]) != ESP_OK) adc_unit[u] = nullptr;

        adc_cali_line_fitting_config_t ccfg = {};
        ccfg.unit_id = id;
        ccfg.atten = ADC_ATTEN_DB_12;
        ccfg.bitwidth = ADC_BITWIDTH_12;
        ccfg.default_vref = 1100;  // stare kusy nemaji Vref v eFuse
        if (adc_cali_create_scheme_line_fitting(&ccfg, &adc_cal[u]) != ESP_OK) adc_cal[u] = nullptr;
    }
}

void adcDeinit() {
    for (int u = 0; u < 2; u++) {
        if (adc_cal[u]) adc_cali_delete_scheme_line_fitting(adc_cal[u]);
        if (adc_unit[u]) adc_oneshot_del_unit(adc_unit[u]);
        adc_cal[u] = nullptr;
        adc_unit[u] = nullptr;
    }
}

// ADC jednotka a kanal pinu; false kdyz pin ADC nema nebo jednotka nejde.
bool adcChannel(gpio_num_t pin, int &u, adc_channel_t &ch) {
    adc_unit_t id;
    if (adc_oneshot_io_to_channel(pin, &id, &ch) != ESP_OK) return false;
    u = id == ADC_UNIT_1 ? 0 : 1;
    return adc_unit[u] != nullptr;
}

int readMv(const DiagPin &p) {
    int u;
    adc_channel_t ch;
    if (!adcChannel(p.pin, u, ch)) return -1;
    constexpr int N = 16;
    int sum = 0;
    for (int i = 0; i < N; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc_unit[u], ch, &raw) != ESP_OK) return -1;
        sum += raw;
    }
    int mv = 0;
    if (!adc_cal[u] || adc_cali_raw_to_voltage(adc_cal[u], sum / N, &mv) != ESP_OK) {
        mv = sum / N * 3300 / 4095;
    }
    return mv;
}

int readPin(const DiagPin &p) {
    return p.adc ? readMv(p) : gpio_get_level(p.pin);
}

Result measurePin(const DiagPin &p) {
    // Konfigurace kanalu ADC prepne pad do analogoveho rezimu a vypne pully.
    int u;
    adc_channel_t ch;
    if (p.adc && adcChannel(p.pin, u, ch)) {
        adc_oneshot_chan_cfg_t ccfg = {};
        ccfg.atten = ADC_ATTEN_DB_12;
        ccfg.bitwidth = ADC_BITWIDTH_12;
        adc_oneshot_config_channel(adc_unit[u], ch, &ccfg);
    } else if (!p.in_use) {
        gpio_set_direction(p.pin, GPIO_MODE_INPUT);
    }

    Result r;
    if (p.pulls) {
        setPulls(p.pin, false, true);
        delayMs(SETTLE_MS);
        r.pd = readPin(p);
        setPulls(p.pin, true, false);
        delayMs(SETTLE_MS);
        r.pu = readPin(p);
    } else {
        delayMs(SETTLE_MS);
        r.pd = r.pu = readPin(p);
    }

    // Uklid: zpet do digitalniho rezimu jako vstup bez pullu.
    if (p.in_use) {
        setPulls(p.pin, true, false);  // I2C ma interni pull-up zapnuty
    } else {
        if (rtc_gpio_is_valid_gpio(p.pin)) rtc_gpio_deinit(p.pin);
        gpio_set_direction(p.pin, GPIO_MODE_INPUT);
        if (p.pulls) setPulls(p.pin, false, false);
    }
    // GPIO15 drzi M5Unified v LOW (ruseni Wi-Fi s M5GO bottomem).
    if (p.pin == GPIO_NUM_15) {
        gpio_set_direction(p.pin, GPIO_MODE_OUTPUT);
        gpio_set_level(p.pin, 0);
    }
    return r;
}

// Textove vyhodnoceni (anglicky - jde i na displej).
void classify(const DiagPin &p, const Result &r, char *buf, size_t n) {
    if (p.adc && (r.pd < 0 || r.pu < 0)) {
        snprintf(buf, n, "ADC error");
    } else if (!p.pulls) {
        // Bez internich pullu jde jen o napeti; plovouci pin muze ukazat cokoli.
        if (r.pd > 2800) snprintf(buf, n, "HIGH - pull-up?");
        else if (r.pd < 300) snprintf(buf, n, "LOW / floating");
        else snprintf(buf, n, "floating?");
    } else if (!p.adc) {
        bool up = r.pd == 1, down = r.pu == 0;
        if (up && down) snprintf(buf, n, "driven?");
        else if (up) snprintf(buf, n, "PULL-UP <~15k");
        else if (down) snprintf(buf, n, "PULL-DOWN <~15k");
        else snprintf(buf, n, "none");
    } else {
        bool up = r.pd > 300, down = r.pu < 2800;
        if (up && down) {
            snprintf(buf, n, "divider/driven");
        } else if (up) {
            if (r.pd >= 3050) snprintf(buf, n, "PULL-UP <~4k");
            else snprintf(buf, n, "PULL-UP ~%.0fk", R_INT_K * (VDD_MV - r.pd) / r.pd);
        } else if (down) {
            if (r.pu <= 250) snprintf(buf, n, "PULL-DOWN <~4k");
            else snprintf(buf, n, "PULL-DOWN ~%.0fk", R_INT_K * r.pu / (VDD_MV - r.pu));
        } else {
            snprintf(buf, n, "none");
        }
    }
}

void fmtValue(const DiagPin &p, int v, char *buf, size_t n) {
    if (!p.adc) snprintf(buf, n, "%s", v ? "H" : "L");
    else if (v < 0) snprintf(buf, n, "err");
    else snprintf(buf, n, "%d.%02d", v / 1000, v % 1000 / 10);
}

void scanAndShow(uint32_t pass) {
    auto &d = M5.Display;
    d.startWrite();
    d.fillScreen(TFT_BLACK);
    d.setFont(&fonts::Font2);
    d.setTextSize(1);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setCursor(4, 0);
    d.printf("PULL-UP SCAN #%lu   pd / pu [V]   [B] exit", (unsigned long)pass);

    LOG("[diag] --- pruchod %lu (pd = s internim pull-down, pu = s pull-up) ---",
        (unsigned long)pass);
    for (int i = 0; i < PIN_COUNT; i++) {
        const DiagPin &p = PINS[i];
        Result r = measurePin(p);
        char pd[12], pu[12], verdict[24];
        fmtValue(p, r.pd, pd, sizeof pd);
        fmtValue(p, r.pu, pu, sizeof pu);
        classify(p, r, verdict, sizeof verdict);

        int y = 18 + i * 15;
        bool hit = strncmp(verdict, "PULL", 4) == 0;
        d.setTextColor(hit ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
        d.setCursor(4, y);
        d.printf("G%d", p.pin);
        d.setCursor(36, y);
        d.print(pd);
        d.setCursor(76, y);
        d.print(p.pulls ? pu : "-");
        d.setCursor(120, y);
        d.print(verdict);
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.setCursor(250, y);
        d.print(p.note);

        LOG("[diag] G%-2d pd %-5s pu %-5s %-18s %s", p.pin, pd, p.pulls ? pu : "-", verdict, p.note);
    }
    d.endWrite();
}

}  // namespace

bool diagRequested() {
    return gpio_get_level(GPIO_NUM_37) == 0;  // BtnC, aktivni LOW
}

void diagRun() {
    LOG("[diag] start - BtnB = konec");
    adcInit();

    auto &d = M5.Display;
    d.wakeup();
    d.setBrightness(DISPLAY_BRIGHTNESS);

    uint32_t t0 = uptimeMs(), last = 0, pass = 0;
    bool first = true;
    while (uptimeMs() - t0 < DIAG_TIMEOUT_MS) {
        M5.update();
        if (M5.BtnB.wasPressed()) break;
        if (first || uptimeMs() - last >= REFRESH_MS) {
            first = false;
            last = uptimeMs();
            scanAndShow(++pass);
        }
        delayMs(10);
    }

    adcDeinit();
    LOG("[diag] konec");
    d.setFont(&fonts::Font0);
    d.fillScreen(TFT_BLACK);
    d.setBrightness(0);
    d.sleep();
}
