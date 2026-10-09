#include "display.h"

#include <M5Unified.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "persist.h"
#include "power.h"
#include "timebase.h"

namespace {

// Rozlozeni 320x240.
constexpr int GX0 = 12, GX1 = 308;   // graf vodorovne
constexpr int GY0 = 100, GY1 = 192;  // graf svisle
constexpr int Y_WIFI = 212, Y_FOOTER = 228;

bool lit = false;  // displej rozsviceny

// Posledni platny vzorek (raw 1/16 C), false kdyz zadny neni.
bool lastSample(const RtcState &s, int16_t &raw) {
    for (int i = int(s.h.count) - 1; i >= 0; i--) {
        if (s.samples[i] != SAMPLE_MISSING) {
            raw = s.samples[i];
            return true;
        }
    }
    return false;
}

// Minuta lokalniho casu (pro prekresleni hodin), -1 kdyz cas neni znamy.
int currentMinute(const RtcState &s) {
    struct tm tm;
    bool approx;
    return timebaseLocal(s, tm, approx) ? tm.tm_hour * 60 + tm.tm_min : -1;
}

void drawHeader(const RtcState &s) {
    auto &d = M5.Display;
    d.fillRect(0, 0, 320, 18, TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(GX0, 6);
    d.print("TEMPERATURE");
    char bat[16];
    snprintf(bat, sizeof bat, "BAT %d%%", powerBatteryLevel());
    d.setCursor(GX1 - d.textWidth(bat), 6);
    d.print(bat);

    // Hodiny uprostred; "~" = jen odhad (cas buildu), "--:--" = neznamy.
    struct tm tm;
    bool approx = false;
    char clk[8];
    if (timebaseLocal(s, tm, approx)) {
        snprintf(clk, sizeof clk, "%s%02d:%02d", approx ? "~" : "", tm.tm_hour, tm.tm_min);
    } else {
        snprintf(clk, sizeof clk, "--:--");
    }
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor((320 - d.textWidth(clk)) / 2, 6);
    d.print(clk);
}

void drawCurrent(const RtcState &s) {
    auto &d = M5.Display;
    int16_t raw;
    char val[16];
    if (lastSample(s, raw)) {
        snprintf(val, sizeof val, "%.2f", raw / 16.0f);
    } else {
        snprintf(val, sizeof val, "--.-");
    }
    d.setTextSize(6);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    int w = d.textWidth(val) + 18 + d.textWidth("C");
    int x = (320 - w) / 2;
    d.setCursor(x, 26);
    d.print(val);
    // Znak stupne v zakladnim fontu neni - kolecko se dokresli.
    d.drawCircle(d.getCursorX() + 8, 30, 4, TFT_WHITE);
    d.drawCircle(d.getCursorX() + 8, 30, 3, TFT_WHITE);
    d.setCursor(d.getCursorX() + 18, 26);
    d.print("C");
}

void drawGraph(const RtcState &s) {
    auto &d = M5.Display;
    d.drawRect(GX0 - 1, GY0 - 1, GX1 - GX0 + 2, GY1 - GY0 + 2, TFT_DARKGREY);

    d.setTextSize(1);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(GX0, GY1 + 6);
    d.printf("-%lu h", (unsigned long)GRAPH_HOURS);
    d.setCursor(GX1 - d.textWidth("now"), GY1 + 6);
    d.print("now");

    // Poslednich GRAPH_SAMPLES slotu, nejnovejsi vpravo.
    int first = int(s.h.count) - int(GRAPH_SAMPLES);  // index slotu u leveho okraje
    int16_t rmin = INT16_MAX, rmax = INT16_MIN;
    int valid = 0;
    for (int j = 0; j < GRAPH_SAMPLES; j++) {
        int i = first + j;
        if (i < 0 || i >= int(s.h.count) || s.samples[i] == SAMPLE_MISSING) continue;
        if (s.samples[i] < rmin) rmin = s.samples[i];
        if (s.samples[i] > rmax) rmax = s.samples[i];
        valid++;
    }
    if (valid < 2) {
        d.setCursor(GX0 + 8, (GY0 + GY1) / 2 - 4);
        d.print("collecting data...");
        return;
    }

    // Rozsah minimalne 1 C, at graf pri stabilni teplote neskace sumem.
    if (rmax - rmin < 16) {
        int16_t mid = (rmax + rmin) / 2;
        rmin = mid - 8;
        rmax = mid + 8;
    }

    d.setCursor(GX0 + 4, GY0 + 4);
    d.printf("max %.2f", rmax / 16.0f);
    d.setCursor(GX0 + 4, GY1 - 12);
    d.printf("min %.2f", rmin / 16.0f);

    int px = -1, py = -1;
    for (int j = 0; j < GRAPH_SAMPLES; j++) {
        int i = first + j;
        if (i < 0 || i >= int(s.h.count) || s.samples[i] == SAMPLE_MISSING) {
            px = -1;  // mezera v datech se nespojuje carou
            continue;
        }
        int x = GX0 + j * (GX1 - GX0) / (GRAPH_SAMPLES - 1);
        int y = GY1 - int(int32_t(s.samples[i] - rmin) * (GY1 - GY0) / (rmax - rmin));
        if (px >= 0) d.drawLine(px, py, x, y, TFT_CYAN);
        else d.drawPixel(x, y, TFT_CYAN);
        px = x;
        py = y;
    }
}

// Stav Wi-Fi: nastavene SSID a vysledek posledniho okna. Problem cervene,
// at je na prvni pohled videt, ze data neodchazeji.
void drawWifi(const RtcState &s) {
    auto &d = M5.Display;
    char ssid[WIFI_SSID_MAX + 1] = "";
    persistGetStr("ssid", ssid, sizeof ssid);
    char line[80];
    uint16_t color = TFT_GREEN;
    if (!ssid[0]) {
        snprintf(line, sizeof line, "WiFi: NOT SET - hold B at power-on");
        color = TFT_RED;
    } else {
        const char *res = "";
        switch (s.h.last_result) {
            case SyncResult::None:       res = "not synced yet"; color = TFT_YELLOW; break;
            case SyncResult::Ok:         res = "OK"; break;
            case SyncResult::NoWifi:     res = "FAIL:"; color = TFT_RED; break;
            case SyncResult::NoNtp:      res = "NO TIME (NTP)"; color = TFT_RED; break;
            case SyncResult::UploadFail: res = "UPLOAD FAILED"; color = TFT_RED; break;
        }
        // Radek ma 296 px = 49 znaku; SSID zkratit, at se vejde i vysledek.
        snprintf(line, sizeof line, "WiFi \"%.16s\": %s", ssid, res);
        size_t n = strlen(line);
        if (s.h.last_result == SyncResult::NoWifi) {
            // Duvod z ESP-IDF (cislo pro dohledani v wifi_err_reason_t).
            snprintf(line + n, sizeof line - n, " %s (%u)", netReasonText(s.h.wifi_reason),
                     unsigned(s.h.wifi_reason));
        } else if (s.h.last_result == SyncResult::Ok && s.h.last_rssi) {
            snprintf(line + n, sizeof line - n, " %d dBm", int(s.h.last_rssi));
        }
    }
    d.setTextSize(1);
    d.setTextColor(color, TFT_BLACK);
    d.setCursor(GX0, Y_WIFI);
    d.print(line);

    // Posledni uspesne odeslani (mistni cas) vpravo - pri chybe pripojeni
    // by se s textem duvodu nevesel, chyba je dulezitejsi.
    if (s.h.last_ok_unix_us && s.h.last_result != SyncResult::NoWifi) {
        time_t t = time_t(s.h.last_ok_unix_us / 1000000);
        struct tm lt;
        localtime_r(&t, &lt);
        char sent[24];
        snprintf(sent, sizeof sent, "sent %02d:%02d", lt.tm_hour, lt.tm_min);
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.setCursor(GX1 - d.textWidth(sent), Y_WIFI);
        d.print(sent);
    }
}

void drawFooter(const RtcState &s) {
    auto &d = M5.Display;
    d.setTextSize(1);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(GX0, Y_FOOTER);
    d.printf("err %u  fail %u  wake #%lu%s", s.h.err_count, s.h.failed_windows,
             (unsigned long)s.h.boot_count, (s.h.flags & FLAG_USB_POWER) ? "  USB" : "");
    const char *hint = "[B] off  [C] sync";
    d.setCursor(GX1 - d.textWidth(hint), Y_FOOTER);
    d.print(hint);
}

// Odpocet vypnuti pres graf: "hold A+C to shut down" + zbyvajici sekundy.
void drawOffHint(uint32_t held_ms) {
    auto &d = M5.Display;
    constexpr int W = 220, H = 70;
    int x = (320 - W) / 2, y = 90;
    d.fillRoundRect(x, y, W, H, 6, TFT_MAROON);
    d.drawRoundRect(x, y, W, H, 6, TFT_RED);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE, TFT_MAROON);
    const char *t = "hold A+C to SHUT DOWN";
    d.setCursor(160 - d.textWidth(t) / 2, y + 10);
    d.print(t);
    char buf[16];
    uint32_t left = held_ms < OFF_HOLD_MS ? OFF_HOLD_MS - held_ms : 0;
    snprintf(buf, sizeof buf, "%lu", (unsigned long)((left + 999) / 1000));
    d.setTextSize(4);
    d.setCursor(160 - d.textWidth(buf) / 2, y + 28);
    d.print(buf);
}

void wake() {
    if (lit) return;
    M5.Display.wakeup();
    M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
    lit = true;
}

void drawScreen(const RtcState &s) {
    auto &d = M5.Display;
    d.clear(TFT_BLACK);
    drawHeader(s);
    drawCurrent(s);
    drawGraph(s);
    drawWifi(s);
    drawFooter(s);
}

}  // namespace

DisplayExit displayRun(RtcState &s, bool (*tick)(RtcState &)) {
    auto &d = M5.Display;
    if (!lit) {
        d.wakeup();
        drawScreen(s);  // nejdriv nakreslit, pak rozsvitit
        wake();
    } else {
        drawScreen(s);
    }

    DisplayExit exit = DisplayExit::Timeout;
    uint32_t t0 = uptimeMs();
    int shown_min = currentMinute(s);
    uint32_t combo_t0 = 0;     // od kdy jsou A i C drzene, 0 = nejsou
    uint32_t hint_shown = 0;
    // Po zrusenem dvojhmatu tlacitka chvili ignorovat: pusteni A a C neprijde
    // naraz a to druhe by se jinak pocitalo jako samostatny stisk (C = sync).
    constexpr uint32_t COMBO_MASK_MS = 300;
    uint32_t mask_until = 0;
    while (uptimeMs() - t0 < DISPLAY_TIMEOUT_MS) {
        M5.update();
        uint32_t now = uptimeMs();

        // Dvojhmat A + C: odpocet, po OFF_HOLD_MS vypnout. Drzeni prodluzuje
        // zobrazeni; pusteni odpocet zrusi a vrati obrazovku.
        if (M5.BtnA.isPressed() && M5.BtnC.isPressed()) {
            if (!combo_t0) {
                combo_t0 = now;
                hint_shown = 0;
            }
            uint32_t held = now - combo_t0;
            if (held >= OFF_HOLD_MS) {
                exit = DisplayExit::PowerOff;
                break;
            }
            if (!hint_shown || now - hint_shown >= 100) {
                hint_shown = now;
                drawOffHint(held);
            }
            t0 = now;
        } else if (combo_t0) {
            combo_t0 = 0;
            mask_until = now + COMBO_MASK_MS;
            drawScreen(s);
        }
        // Dokud je drzene cokoli po zrusenem dvojhmatu, maska bezi dal.
        if (mask_until && (M5.BtnA.isPressed() || M5.BtnC.isPressed())) {
            mask_until = now + COMBO_MASK_MS;
        }
        bool masked = combo_t0 || (mask_until && int32_t(now - mask_until) < 0);
        if (!masked) mask_until = 0;

        if (!masked && M5.BtnB.wasPressed()) break;
        // Sync az po pusteni C - stisk C muze byt zacatek dvojhmatu.
        if (!masked && M5.BtnC.wasReleased() && !M5.BtnA.isPressed()) {
            exit = DisplayExit::SyncNow;
            break;
        }
        if (tick(s)) drawScreen(s);  // mereni na mrizce behem zobrazeni
        int m = currentMinute(s);
        if (m != shown_min) {
            shown_min = m;
            drawHeader(s);  // hodiny prepnuly minutu
        }
        delayMs(10);
    }
    return exit;
}

bool displayPortal(const char *ap_ssid, uint32_t left_s, int stations) {
    auto &d = M5.Display;
    bool first = !lit;
    wake();
    if (first) d.clear(TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setCursor(GX0, 10);
    d.print("WI-FI SETUP");
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(GX0, 50);
    d.print("1. connect to Wi-Fi:");
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setCursor(GX0 + 24, 74);
    d.printf("%s   ", ap_ssid);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(GX0, 108);
    d.print("2. open in browser:");
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setCursor(GX0 + 24, 132);
    d.print("192.168.4.1");
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor(GX0, 172);
    d.printf("time left %lu:%02lu  ", (unsigned long)(left_s / 60), (unsigned long)(left_s % 60));
    d.setTextSize(1);
    d.setCursor(GX0, 200);
    d.printf("connected devices: %d   ", stations);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    const char *hint = "[B] cancel";
    d.setCursor(GX1 - d.textWidth(hint), Y_FOOTER);
    d.print(hint);

    M5.update();
    return M5.BtnB.wasPressed();
}

void displayMessage(const char *title, const char *line) {
    auto &d = M5.Display;
    wake();
    d.clear(TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setCursor(GX0, 90);
    d.print(title);
    d.setTextSize(1);
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor(GX0, 120);
    d.print(line);
}

void displayOff() {
    if (!lit) return;
    auto &d = M5.Display;
    d.setBrightness(0);
    d.sleep();
    lit = false;
    // Pockat na pusteni BtnA, jinak by ext0 probudil desku hned zpatky.
    do {
        M5.update();
        delayMs(10);
    } while (M5.BtnA.isPressed());
}
