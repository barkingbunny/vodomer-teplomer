#include "power.h"

#include <M5Unified.h>

#include "driver/gpio.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "log.h"

namespace {

// IP5306 (I2C 0x75): bit 1 v SYS_CTL0 = "boost keep on" - nevypinat vystup
// pri malem odberu. Bez toho se deska v deep sleep do minuty vypne.
// M5Unified to v M5.begin() nastavuje taky; tohle je pojistka + kontrola.
constexpr uint8_t IP5306_REG_SYS_CTL0 = 0x00;
constexpr uint8_t IP5306_BOOST_OUT_BIT = 0x02;

// Vlastni instance nad vnitrni I2C (Basic: GPIO21/22, port 0) - IP5306 tak
// jde cist i bez M5.begin(), ktery na probuzenich bez displeje nevolame
// (jeho inicializace LCD ceka 120 ms, viz main.cpp). Vychozi sbernice je
// globalni m5::In_I2C - NE M5.In_I2C: to je reference nastavovana az
// konstruktorem objektu M5 a poradi inicializace globalu neni zarucene
// (= nulovy ukazatel a pad hned po startu).
m5::IP5306_Class ip5306;

bool ip5306KeepOn() {
    uint8_t v = ip5306.readRegister8(IP5306_REG_SYS_CTL0);
    if (v & IP5306_BOOST_OUT_BIT) return true;
    return ip5306.writeRegister8(IP5306_REG_SYS_CTL0, v | IP5306_BOOST_OUT_BIT) &&
           (ip5306.readRegister8(IP5306_REG_SYS_CTL0) & IP5306_BOOST_OUT_BIT);
}

}  // namespace

void speakerOff() {
    // Nejdriv pin nastavit jako vystup v LOW a az pak uvolnit hold ze spanku -
    // uroven se tak ani na okamzik nezmeni. (gpio_reset_pin by zapnul pull-up
    // a skok LOW->HIGH->LOW byl v repru slyset jako lupnuti pri kazdem probuzeni.)
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_SPEAKER;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_set_level(PIN_SPEAKER, 0);
    gpio_config(&io);
    gpio_set_level(PIN_SPEAKER, 0);
    gpio_hold_dis(PIN_SPEAKER);
}

void backlightHold() {
    // Po probuzeni hold ze spanku jeste plati -> pin je LOW. Nastavit vystup
    // LOW a hold znovu zapnout (po power-on zadny nebyl), M5GFX ho pres hold
    // nerozsviti. gpio_hold_dis az v backlightRelease().
    // Pozor: NE gpio_config() - ta si v IDF 6 vystupni pin rezervuje a LEDC
    // (PWM podsviceni v M5GFX) ho pak odmitne ("GPIO 32 is not usable").
    gpio_set_level(PIN_BACKLIGHT, 0);
    gpio_set_direction(PIN_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BACKLIGHT, 0);
    gpio_hold_en(PIN_BACKLIGHT);
}

void backlightRelease() {
    gpio_hold_dis(PIN_BACKLIGHT);
}

bool powerInit() {
    // Idempotentni - M5.begin() (kdyz se vola) ji inicializuje stejne.
    m5::In_I2C.begin(I2C_NUM_0, GPIO_NUM_21, GPIO_NUM_22);
    bool ok = ip5306KeepOn();
    if (!ok) LOG("[pwr] IP5306 keep-on SELHAL - deska se ve spanku muze vypnout!");
    return ok;
}

void powerBeforeSleep() {
    // RTC piny - hold je udrzi LOW i v deep sleep. Podsviceni uz je zhasnute
    // (setBrightness(0) = LEDC duty 0), hold zafixuje LOW.
    gpio_hold_en(PIN_SPEAKER);
    gpio_hold_en(PIN_BACKLIGHT);
}

int powerBatteryLevel() {
    return ip5306.getBatteryLevel();
}

[[noreturn]] void powerShutdown() {
    // Pockat na pusteni tlacitek - stisk A by desku hned probudil.
    do {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnC.isPressed());

    // SYS_CTL0 bit 1 = keep-on (nevypinat pri malem odberu) -> pustit.
    // SYS_CTL2 bity 2-3 = prodleva vypnuti pri malem odberu, 00 = 8 s.
    constexpr uint8_t IP5306_REG_SYS_CTL2 = 0x02;
    uint8_t v = ip5306.readRegister8(IP5306_REG_SYS_CTL0);
    ip5306.writeRegister8(IP5306_REG_SYS_CTL0, v & ~IP5306_BOOST_OUT_BIT);
    v = ip5306.readRegister8(IP5306_REG_SYS_CTL2);
    ip5306.writeRegister8(IP5306_REG_SYS_CTL2, v & ~0x0C);

    M5.Display.setBrightness(0);
    M5.Display.sleep();
    gpio_hold_en(PIN_SPEAKER);
    gpio_hold_en(PIN_BACKLIGHT);
    LOG("[pwr] vypinam");
    LOG_FLUSH();
    // Bez casovace: na baterii IP5306 odpoji napajeni, na USB probudi BtnA.
    esp_sleep_enable_ext0_wakeup(PIN_BTN_A, 0);
    esp_deep_sleep_start();
}

bool powerOnUsb() {
    constexpr uint8_t IP5306_REG_READ0 = 0x70;
    constexpr uint8_t IP5306_VIN_BIT = 0x08;
    return ip5306.readRegister8(IP5306_REG_READ0) & IP5306_VIN_BIT;
}
