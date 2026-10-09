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

bool ip5306KeepOn() {
    uint8_t v = M5.Power.Ip5306.readRegister8(IP5306_REG_SYS_CTL0);
    if (v & IP5306_BOOST_OUT_BIT) return true;
    return M5.Power.Ip5306.writeRegister8(IP5306_REG_SYS_CTL0, v | IP5306_BOOST_OUT_BIT) &&
           (M5.Power.Ip5306.readRegister8(IP5306_REG_SYS_CTL0) & IP5306_BOOST_OUT_BIT);
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
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_BACKLIGHT;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_set_level(PIN_BACKLIGHT, 0);
    gpio_config(&io);
    gpio_set_level(PIN_BACKLIGHT, 0);
    gpio_hold_en(PIN_BACKLIGHT);
}

void backlightRelease() {
    gpio_hold_dis(PIN_BACKLIGHT);
}

bool powerInit() {
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
    return M5.Power.getBatteryLevel();
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
    uint8_t v = M5.Power.Ip5306.readRegister8(IP5306_REG_SYS_CTL0);
    M5.Power.Ip5306.writeRegister8(IP5306_REG_SYS_CTL0, v & ~IP5306_BOOST_OUT_BIT);
    v = M5.Power.Ip5306.readRegister8(IP5306_REG_SYS_CTL2);
    M5.Power.Ip5306.writeRegister8(IP5306_REG_SYS_CTL2, v & ~0x0C);

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
    return M5.Power.Ip5306.readRegister8(IP5306_REG_READ0) & IP5306_VIN_BIT;
}
