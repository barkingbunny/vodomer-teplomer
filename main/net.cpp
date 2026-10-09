#include "net.h"

#include <string.h>

#include <algorithm>
#include <atomic>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "clock.h"
#include "config.h"
#include "log.h"
#include "persist.h"

namespace {

constexpr EventBits_t BIT_GOT_IP = 1 << 0;

EventGroupHandle_t events;
bool wifi_started = false;
std::atomic<bool> connecting{false};  // netConnect ceka na IP - po odpojeni zkusit znovu
std::atomic<uint8_t> last_reason{0};
std::atomic<uint8_t> retries{0};

void onEvent(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, BIT_GOT_IP);
        last_reason = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
        // ESP-IDF po neuspechu samo znovu nezkousi - jedno zavahani (handshake,
        // AP nenalezen pri skenu) by jinak shodilo cely pokus.
        if (connecting) {
            retries++;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, BIT_GOT_IP);
    }
}

// NTP cas (sekundy od 1900) -> unix.
constexpr uint32_t NTP_UNIX_OFFSET = 2208988800UL;

uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// Jeden SNTP dotaz. Na prijem ceka nejdeleji timeout_ms.
bool sntpQuery(const char *host, uint32_t timeout_ms, int64_t &unix_us, uint64_t &mono_us) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = nullptr;
    if (getaddrinfo(host, "123", &hints, &res) != 0 || !res) {
        LOG("[ntp] DNS %s selhal", host);
        return false;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        freeaddrinfo(res);
        return false;
    }
    struct timeval tv = {long(timeout_ms / 1000), long(timeout_ms % 1000 * 1000)};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    // LI=0, VN=4, Mode=3 (klient). Do transmit timestampu dame mono cas -
    // server ho vrati v originate, tim se overi, ze odpoved patri k dotazu.
    uint8_t pkt[48] = {};
    pkt[0] = 0x23;
    uint64_t sent_mono = clockNow();
    memcpy(pkt + 40, &sent_mono, sizeof sent_mono);
    bool ok = sendto(sock, pkt, sizeof pkt, 0, res->ai_addr, res->ai_addrlen) == sizeof pkt;
    freeaddrinfo(res);

    uint8_t rx[48];
    int n = ok ? recv(sock, rx, sizeof rx, 0) : -1;
    uint64_t recv_mono = clockNow();
    close(sock);

    if (n != sizeof rx) {
        LOG("[ntp] %s neodpovedel", host);
        return false;
    }
    uint8_t mode = rx[0] & 7, stratum = rx[1];
    if (mode != 4 || stratum == 0 || stratum > 15 || memcmp(rx + 24, &sent_mono, 8) != 0) {
        LOG("[ntp] %s: neplatna odpoved (mode %u, stratum %u)", host, mode, stratum);
        return false;
    }
    // Cas odeslani odpovedi serverem + polovina doby obehu.
    uint32_t sec = be32(rx + 40), frac = be32(rx + 44);
    int64_t t3 = int64_t(sec - NTP_UNIX_OFFSET) * 1000000LL + int64_t((uint64_t(frac) * 1000000ULL) >> 32);
    unix_us = t3 + int64_t(recv_mono - sent_mono) / 2;
    mono_us = recv_mono;
    LOG("[ntp] %s, stratum %u, obeh %lu ms", host, stratum,
        (unsigned long)((recv_mono - sent_mono) / 1000));
    return true;
}

}  // namespace

bool netInit() {
    static bool done = false;
    if (done) return true;
    if (esp_netif_init() != ESP_OK) return false;
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // udaje drzime v NVS sami
    events = xEventGroupCreate();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr, nullptr);
    done = true;
    return true;
}

bool netConnect(uint32_t timeout_ms, int8_t &rssi, uint8_t &reason) {
    reason = 0;
    char ssid[WIFI_SSID_MAX + 1], pass[WIFI_PASS_MAX + 1];
    if (!persistGetWifi(ssid, pass)) {
        LOG("[wifi] nejsou ulozene udaje");
        return false;
    }
    if (!netInit()) {
        LOG("[wifi] inicializace selhala");
        return false;
    }

    uint32_t t0 = uptimeMs();
    wifi_config_t wc = {};
    memcpy(wc.sta.ssid, ssid, strlen(ssid));
    memcpy(wc.sta.password, pass, strlen(pass));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;  // prijmout i otevrenou sit
    // Firemni sit ma vic AP se stejnym SSID: projit vsechny kanaly a vzit
    // nejsilnejsi (vychozi rychly sken bere prvni nalezeny, klidne vzdaleny).
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.failure_retry_cnt = 2;
    xEventGroupClearBits(events, BIT_GOT_IP);
    last_reason = 0;
    retries = 0;
    connecting = true;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (!wifi_started) {
        esp_wifi_start();
        wifi_started = true;
    }
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(events, BIT_GOT_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    connecting = false;
    if (!(bits & BIT_GOT_IP)) {
        reason = last_reason;
        LOG("[wifi] timeout pripojeni k \"%s\" (duvod %u %s, pokusu %u)", ssid, unsigned(reason),
            netReasonText(reason), unsigned(retries) + 1);
        esp_wifi_disconnect();
        return false;
    }
    wifi_ap_record_t ap;
    rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    LOG("[wifi] pripojeno k \"%s\" za %lu ms, RSSI %d dBm", ssid, (unsigned long)(uptimeMs() - t0),
        int(rssi));
    return true;
}

bool netNtp(uint32_t timeout_ms, int64_t &unix_us, uint64_t &mono_us) {
    uint32_t t0 = uptimeMs();
    const char *servers[] = {NTP_SERVER1, NTP_SERVER2};
    for (int round = 0; round < 2; round++) {
        for (const char *host : servers) {
            uint32_t spent = uptimeMs() - t0;
            if (spent >= timeout_ms) break;
            uint32_t left = timeout_ms - spent;
            if (sntpQuery(host, std::min<uint32_t>(left, 2500), unix_us, mono_us)) return true;
        }
    }
    LOG("[ntp] timeout");
    return false;
}

const char *netReasonText(uint8_t reason) {
    switch (reason) {
        case 0: return "no response";
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "AP not found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "wrong password?";
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_CONNECTION_FAIL: return "AP rejected";
        case WIFI_REASON_BEACON_TIMEOUT: return "weak signal";
        default: return "error";
    }
}

void netOff() {
    if (!wifi_started) return;
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifi_started = false;
}

bool netStartAp(const char *ssid) {
    if (!netInit()) return false;
    wifi_config_t wc = {};
    size_t len = strlen(ssid);
    memcpy(wc.ap.ssid, ssid, len);
    wc.ap.ssid_len = uint8_t(len);
    wc.ap.channel = 1;
    wc.ap.authmode = WIFI_AUTH_OPEN;  // heslo AP: viz optional.md
    wc.ap.max_connection = 4;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &wc);
    if (!wifi_started) {
        if (esp_wifi_start() != ESP_OK) return false;
        wifi_started = true;
    }
    return true;
}

int netApStations() {
    wifi_sta_list_t list;
    return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

void netMacSuffix(char *buf, int len) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(buf, len, "%02X%02X", mac[4], mac[5]);
}

int netScan(NetScanEntry *out, int max) {
    if (!netInit()) return 0;
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL || mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    if (!wifi_started) {
        esp_wifi_start();
        wifi_started = true;
    }
    if (esp_wifi_scan_start(nullptr, true) != ESP_OK) return 0;
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    n = std::min<uint16_t>(n, 30);
    wifi_ap_record_t recs[30];
    esp_wifi_scan_get_ap_records(&n, recs);  // uz serazene podle RSSI

    int cnt = 0;
    for (int i = 0; i < n && cnt < max; i++) {
        const char *s = reinterpret_cast<const char *>(recs[i].ssid);
        if (!s[0]) continue;  // skryta sit
        bool dup = false;
        for (int j = 0; j < cnt; j++) dup |= strcmp(out[j].ssid, s) == 0;
        if (dup) continue;
        strncpy(out[cnt].ssid, s, sizeof out[cnt].ssid - 1);
        out[cnt].ssid[sizeof out[cnt].ssid - 1] = '\0';
        out[cnt].rssi = recs[i].rssi;
        cnt++;
    }
    return cnt;
}
