#include "persist.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#include "log.h"

namespace {

constexpr const char *NS = "vt";

bool openNs(nvs_open_mode_t mode, nvs_handle_t &h) {
    return persistInit() && nvs_open(NS, mode, &h) == ESP_OK;
}

}  // namespace

bool persistInit() {
    static bool ok = false;
    if (ok) return true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG("[nvs] jina verze / plne NVS - mazu");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ok = err == ESP_OK;
    if (!ok) LOG("[nvs] nedostupne (%d)", int(err));
    return ok;
}

bool persistGetU32(const char *key, uint32_t &value) {
    nvs_handle_t h;
    if (!openNs(NVS_READONLY, h)) return false;
    bool ok = nvs_get_u32(h, key, &value) == ESP_OK;
    nvs_close(h);
    return ok;
}

void persistSetU32(const char *key, uint32_t value) {
    uint32_t old;
    if (persistGetU32(key, old) && old == value) return;  // flash zbytecne neopotrebovavat
    nvs_handle_t h;
    if (!openNs(NVS_READWRITE, h)) return;
    nvs_set_u32(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

bool persistGetStr(const char *key, char *buf, size_t len) {
    nvs_handle_t h;
    if (!openNs(NVS_READONLY, h)) return false;
    size_t n = len;
    bool ok = nvs_get_str(h, key, buf, &n) == ESP_OK;
    nvs_close(h);
    if (!ok && len) buf[0] = '\0';
    return ok;
}

void persistSetStr(const char *key, const char *value) {
    nvs_handle_t h;
    if (!openNs(NVS_READWRITE, h)) return;
    nvs_set_str(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

bool persistGetWifi(char (&ssid)[WIFI_SSID_MAX + 1], char (&pass)[WIFI_PASS_MAX + 1]) {
    if (!persistGetStr("ssid", ssid, sizeof ssid) || ssid[0] == '\0') return false;
    persistGetStr("pass", pass, sizeof pass);  // otevrena sit = prazdne heslo
    return true;
}

void persistSetWifi(const char *ssid, const char *pass) {
    persistSetStr("ssid", ssid);
    persistSetStr("pass", pass);
}

bool persistHasWifi() {
    char ssid[WIFI_SSID_MAX + 1];
    return persistGetStr("ssid", ssid, sizeof ssid) && ssid[0] != '\0';
}
