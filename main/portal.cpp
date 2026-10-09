#include "portal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <string>

#include "esp_http_server.h"
#include "lwip/sockets.h"

#include "clock.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "persist.h"

namespace {

constexpr int SCAN_MAX = 20;

// Stav sdileny s HTTP handlery (bezi v tasku httpd).
std::atomic<uint32_t> last_activity_ms{0};
std::atomic<bool> client_seen{false};
std::atomic<bool> saved{false};
std::atomic<bool> quit{false};
uint32_t start_ms = 0;
std::string options;  // <option> pro formular (sken pred spustenim AP)

void activity() {
    last_activity_ms = uptimeMs();
    client_seen = true;
}

std::string htmlEscape(const char *in) {
    std::string out;
    for (; *in; in++) {
        switch (*in) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default: out += *in;
        }
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = char(tolower(c));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Hodnota pole z application/x-www-form-urlencoded tela.
std::string formField(const std::string &body, const char *name) {
    std::string key = std::string(name) + "=";
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t end = body.find('&', pos);
        if (end == std::string::npos) end = body.size();
        if (body.compare(pos, key.size(), key) == 0) {
            std::string out;
            for (size_t i = pos + key.size(); i < end; i++) {
                char c = body[i];
                if (c == '+') {
                    out += ' ';
                } else if (c == '%' && i + 2 < end && hexVal(body[i + 1]) >= 0 &&
                           hexVal(body[i + 2]) >= 0) {
                    out += char(hexVal(body[i + 1]) * 16 + hexVal(body[i + 2]));
                    i += 2;
                } else {
                    out += c;
                }
            }
            return out;
        }
        pos = end + 1;
    }
    return "";
}

const char PAGE_HEAD[] =
    "<!doctype html><html lang=cs><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Vodoměr</title><style>"
    "body{font-family:sans-serif;max-width:26em;margin:1em auto;padding:0 1em}"
    "input,select,button{width:100%;font-size:1.1em;margin:.3em 0 .9em;padding:.4em;box-sizing:border-box}"
    ".note{color:#666;font-size:.9em}"
    "</style></head><body><h2>Vodoměr – nastavení Wi-Fi</h2>";

esp_err_t sendHtml(httpd_req_t *req, const std::string &html) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html.data(), ssize_t(html.size()));
}

esp_err_t onRoot(httpd_req_t *req) {
    activity();
    uint32_t elapsed = uptimeMs() - start_ms;
    uint32_t max_left = elapsed < PORTAL_MAX_MS ? (PORTAL_MAX_MS - elapsed) / 1000 : 0;
    char cur[WIFI_SSID_MAX + 1] = "";
    persistGetStr("ssid", cur, sizeof cur);

    std::string p = PAGE_HEAD;
    if (cur[0]) {
        p += "<p>Teď nastaveno: <b>" + htmlEscape(cur) + "</b></p>";
    }
    p += "<form method=post action=/save><label>Síť</label><select name=s>";
    p += options;
    p += "</select><label>…nebo SSID ručně</label><input name=m maxlength=32>"
         "<label>Heslo</label><input name=p type=password maxlength=64>"
         "<button>Uložit</button></form>"
         "<form method=post action=/exit><button>Ukončit bez změny</button></form>"
         "<p class=note>Portál se vypne po ";
    p += std::to_string(PORTAL_IDLE_MS / 60000);
    p += " min nečinnosti, nejpozději za ";
    p += std::to_string(max_left / 60) + " min " + std::to_string(max_left % 60);
    p += " s. Každé načtení stránky časovač nečinnosti obnoví.</p></body></html>";
    return sendHtml(req, p);
}

esp_err_t onSave(httpd_req_t *req) {
    activity();
    char buf[512];
    int len = req->content_len < sizeof buf - 1 ? int(req->content_len) : int(sizeof buf - 1);
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, size_t(len - got));
        if (r <= 0) break;
        got += r;
    }
    std::string body(buf, size_t(got));
    std::string ssid = formField(body, "m");
    if (ssid.empty()) ssid = formField(body, "s");
    std::string pass = formField(body, "p");
    if (ssid.empty() || ssid.size() > WIFI_SSID_MAX || pass.size() > WIFI_PASS_MAX ||
        ssid.find('\0') != std::string::npos || pass.find('\0') != std::string::npos) {
        httpd_resp_set_status(req, "400 Bad Request");
        return sendHtml(req, std::string(PAGE_HEAD) + "<p>Neplatné SSID nebo heslo.</p>"
                             "<p><a href=/>Zpět</a></p></body></html>");
    }
    persistSetWifi(ssid.c_str(), pass.c_str());
    LOG("[portal] ulozeno SSID \"%s\"", ssid.c_str());
    sendHtml(req, std::string(PAGE_HEAD) + "<p>Uloženo. Zařízení se teď připojí k síti <b>" +
                      htmlEscape(ssid.c_str()) + "</b> a tahle síť Vodoměr zmizí.</p></body></html>");
    saved = true;
    return ESP_OK;
}

esp_err_t onExit(httpd_req_t *req) {
    sendHtml(req, std::string(PAGE_HEAD) +
                      "<p>Portál ukončen, nastavení beze změny. Zařízení měří dál.</p></body></html>");
    LOG("[portal] ukoncen uzivatelem");
    quit = true;
    return ESP_OK;
}

// Captive portal: vsechno ostatni presmerovat na formular. Telefony se tu
// ptaji samy (detekce captive portalu) - pocita se to jako aktivita.
esp_err_t onNotFound(httpd_req_t *req, httpd_err_code_t) {
    activity();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

// --- Captive DNS: na kazdy dotaz typu A odpovi 192.168.4.1.
int dnsOpen() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return -1;
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        close(sock);
        return -1;
    }
    fcntl(sock, F_SETFL, O_NONBLOCK);
    return sock;
}

void dnsPoll(int sock) {
    uint8_t buf[512];
    struct sockaddr_in from;
    socklen_t flen = sizeof from;
    int n = recvfrom(sock, buf, sizeof buf - 16, 0, reinterpret_cast<sockaddr *>(&from), &flen);
    if (n < 12 || (buf[2] & 0x80)) return;  // kratke nebo neni dotaz
    uint16_t qd = uint16_t(buf[4] << 8 | buf[5]);
    // Konec otazky: jmeno (labely) + typ + trida.
    int p = 12;
    while (p < n && buf[p]) p += buf[p] + 1;
    p += 5;
    if (qd != 1 || p > n) return;
    uint16_t qtype = uint16_t(buf[p - 4] << 8 | buf[p - 3]);

    buf[2] = 0x84 | (buf[2] & 0x01);  // odpoved, autoritativni, RD z dotazu
    buf[3] = 0x80;                    // RA, bez chyby
    buf[6] = 0;
    buf[7] = qtype == 1 ? 1 : 0;      // ANCOUNT
    buf[8] = buf[9] = buf[10] = buf[11] = 0;
    n = p;
    if (qtype == 1) {
        const uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
        memcpy(buf + n, ans, sizeof ans);
        n += sizeof ans;
    }
    sendto(sock, buf, n, 0, reinterpret_cast<sockaddr *>(&from), flen);
}

}  // namespace

bool portalRun(const PortalHooks &hooks) {
    char suffix[8];
    netMacSuffix(suffix, sizeof suffix);
    std::string ap = std::string(PORTAL_SSID_PREFIX) + suffix;

    // Seznam siti pro formular (pred spustenim AP, sken trva ~2 s).
    NetScanEntry nets[SCAN_MAX];
    int n = netScan(nets, SCAN_MAX);
    options.clear();
    for (int i = 0; i < n; i++) {
        std::string s = htmlEscape(nets[i].ssid);
        options += "<option value=\"" + s + "\">" + s + " (" + std::to_string(nets[i].rssi) +
                   " dBm)</option>";
    }

    if (!netStartAp(ap.c_str())) {
        LOG("[portal] AP nejde spustit");
        return false;
    }
    LOG("[portal] AP \"%s\" bezi, http://192.168.4.1, %d siti v okoli", ap.c_str(), n);

    start_ms = uptimeMs();
    last_activity_ms = start_ms;
    client_seen = false;
    saved = false;
    quit = false;

    httpd_handle_t server = nullptr;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    if (httpd_start(&server, &cfg) == ESP_OK) {
        httpd_uri_t root = {"/", HTTP_GET, onRoot, nullptr};
        httpd_uri_t save = {"/save", HTTP_POST, onSave, nullptr};
        httpd_uri_t exit_ = {"/exit", HTTP_POST, onExit, nullptr};
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &save);
        httpd_register_uri_handler(server, &exit_);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, onNotFound);
    } else {
        LOG("[portal] HTTP server nejde spustit");
    }
    int dns = dnsOpen();

    const char *why = "";
    int stations = 0;
    uint32_t last_show = 0;
    bool first_show = true;
    while (!saved && !quit) {
        if (dns >= 0) dnsPoll(dns);
        if (hooks.tick) hooks.tick();

        int now_stations = netApStations();
        if (now_stations > stations) activity();  // nove pripojene zarizeni
        stations = now_stations;

        uint32_t now = uptimeMs();
        uint32_t elapsed = now - start_ms;
        if (elapsed >= PORTAL_MAX_MS) {
            why = "strop";
            break;
        }
        if (!client_seen && elapsed >= PORTAL_CONNECT_MS) {
            why = "nikdo se nepripojil";
            break;
        }
        if (client_seen && now - last_activity_ms >= PORTAL_IDLE_MS) {
            why = "necinnost";
            break;
        }
        if (hooks.show && (first_show || now - last_show >= 1000)) {
            first_show = false;
            last_show = now;
            uint32_t left = client_seen ? PORTAL_IDLE_MS - (now - last_activity_ms)
                                        : PORTAL_CONNECT_MS - elapsed;
            left = std::min(left, PORTAL_MAX_MS - elapsed);
            if (hooks.show(ap.c_str(), left / 1000, stations)) {
                why = "zrusen tlacitkem";
                quit = true;
                break;
            }
        }
        delayMs(10);
    }

    if (saved) delayMs(300);  // dorucit odpoved prohlizeci
    if (server) httpd_stop(server);
    if (dns >= 0) close(dns);
    netOff();
    LOG("[portal] konec po %lu s: %s", (unsigned long)((uptimeMs() - start_ms) / 1000),
        saved ? "udaje ulozeny" : quit && !*why ? "ukoncen" : why);
    return saved;
}
