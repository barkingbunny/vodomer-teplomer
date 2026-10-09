#include "store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "esp_littlefs.h"
#include "esp_rom_crc.h"

#include "config.h"
#include "log.h"

namespace {

constexpr const char *BASE = "/vt";
constexpr const char *DIR_SAMPLES = "/vt/s";
constexpr const char *DIR_SYNC = "/vt/h";
constexpr uint32_t CHUNK_MAGIC = 0x56544331;  // "VTC1"

struct ChunkHdr {
    uint32_t magic;
    uint32_t epoch;
    uint64_t start_mono_us;  // cas slotu 0
    uint16_t count;          // sloty (vcetne SAMPLE_MISSING)
    uint16_t reserved;
    uint32_t crc;            // CRC32 vzorku
};

struct SyncRec {
    uint64_t mono_us;
    int64_t unix_us;
};

struct ChunkFile {
    std::string name;  // jen jmeno v DIR_SAMPLES ("eeeeeeee_ssss")
    uint32_t epoch;
};

bool mount() {
    static bool mounted = false;
    if (mounted) return true;
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = BASE;
    conf.partition_label = "storage";
    conf.format_if_mount_failed = true;  // prvni pouziti / poskozeny FS
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        LOG("[flash] LittleFS nejde pripojit (%d)", int(err));
        return false;
    }
    mkdir(DIR_SAMPLES, 0775);
    mkdir(DIR_SYNC, 0775);
    mounted = true;
    return true;
}

std::string samplePath(const std::string &name) {
    return std::string(DIR_SAMPLES) + "/" + name;
}

std::string syncPath(uint32_t epoch) {
    char buf[32];
    snprintf(buf, sizeof buf, "%s/%08lx", DIR_SYNC, (unsigned long)epoch);
    return buf;
}

// Bloky serazene podle epochy a poradi (jmena maji pevnou delku v hex).
std::vector<ChunkFile> listChunks() {
    std::vector<ChunkFile> out;
    DIR *d = opendir(DIR_SAMPLES);
    if (!d) return out;
    while (struct dirent *e = readdir(d)) {
        unsigned long epoch, seq;
        if (sscanf(e->d_name, "%8lx_%4lx", &epoch, &seq) == 2) {
            out.push_back({e->d_name, uint32_t(epoch)});
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end(),
              [](const ChunkFile &a, const ChunkFile &b) { return a.name < b.name; });
    return out;
}

uint32_t chunkSlots(const std::string &name) {
    struct stat st;
    if (stat(samplePath(name).c_str(), &st) != 0 || st.st_size < long(sizeof(ChunkHdr))) return 0;
    return uint32_t((st.st_size - sizeof(ChunkHdr)) / sizeof(int16_t));
}

bool readChunk(const std::string &name, ChunkHdr &hdr, std::vector<int16_t> &samples) {
    FILE *f = fopen(samplePath(name).c_str(), "rb");
    if (!f) return false;
    bool ok = fread(&hdr, sizeof hdr, 1, f) == 1 && hdr.magic == CHUNK_MAGIC;
    if (ok) {
        samples.resize(hdr.count);
        ok = fread(samples.data(), sizeof(int16_t), hdr.count, f) == hdr.count &&
             esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(samples.data()),
                              hdr.count * sizeof(int16_t)) == hdr.crc;
    }
    fclose(f);
    if (!ok) LOG("[flash] blok %s poskozeny - preskakuji", name.c_str());
    return ok;
}

std::vector<SyncRec> readSyncs(uint32_t epoch) {
    std::vector<SyncRec> out;
    FILE *f = fopen(syncPath(epoch).c_str(), "rb");
    if (!f) return out;
    SyncRec r;
    while (fread(&r, sizeof r, 1, f) == 1) out.push_back(r);
    fclose(f);
    std::sort(out.begin(), out.end(),
              [](const SyncRec &a, const SyncRec &b) { return a.mono_us < b.mono_us; });
    return out;
}

// Unix cas vzorku z historie syncu jeho epochy: mezi dvema syncy linearne
// (oba konce presne z NTP), pred prvnim / za poslednim extrapolace s driftem.
bool sampleUnixUs(const std::vector<SyncRec> &h, int32_t drift_ppb, uint64_t mono_us,
                  int64_t &unix_us) {
    if (h.empty()) return false;
    auto extrapolate = [&](const SyncRec &r) {
        int64_t d = int64_t(mono_us) - int64_t(r.mono_us);
        return r.unix_us + d + d * drift_ppb / 1000000000LL;
    };
    if (mono_us <= h.front().mono_us) {
        unix_us = extrapolate(h.front());
        return true;
    }
    for (size_t i = 0; i + 1 < h.size(); i++) {
        const SyncRec &a = h[i], &b = h[i + 1];
        if (mono_us <= b.mono_us && b.mono_us > a.mono_us) {
            // d * d_real by v int64 pretekl; double ma 53 bitu = us presnost.
            double k = double(mono_us - a.mono_us) / double(b.mono_us - a.mono_us);
            unix_us = a.unix_us + int64_t(k * double(b.unix_us - a.unix_us));
            return true;
        }
    }
    unix_us = extrapolate(h.back());
    return true;
}

// Vzorky jednoho useku (blok nebo RTC buffer) -> fn(). false = fn prerusila.
bool emit(const int16_t *samples, uint32_t n, uint64_t start_mono_us,
          const std::vector<SyncRec> &h, int32_t drift_ppb, StoreSampleFn fn, void *ctx,
          uint32_t &no_time) {
    for (uint32_t i = 0; i < n; i++) {
        if (samples[i] == SAMPLE_MISSING) continue;
        int64_t unix_us;
        if (!sampleUnixUs(h, drift_ppb, start_mono_us + uint64_t(i) * SAMPLE_PERIOD_US, unix_us)) {
            no_time++;
            continue;
        }
        if (!fn(ctx, (unix_us + 500000) / 1000000, samples[i])) return false;
    }
    return true;
}

// Historie epoch, ke kterym uz nejsou zadne bloky (krome aktualni), smazat.
void dropOrphanSyncs(uint32_t current_epoch, const std::vector<ChunkFile> &chunks) {
    DIR *d = opendir(DIR_SYNC);
    if (!d) return;
    std::vector<uint32_t> orphans;
    while (struct dirent *e = readdir(d)) {
        unsigned long epoch;
        if (sscanf(e->d_name, "%8lx", &epoch) != 1 || epoch == current_epoch) continue;
        bool used = std::any_of(chunks.begin(), chunks.end(),
                                [&](const ChunkFile &c) { return c.epoch == epoch; });
        if (!used) orphans.push_back(uint32_t(epoch));
    }
    closedir(d);
    for (uint32_t e : orphans) unlink(syncPath(e).c_str());
}

}  // namespace

bool storeFlush(RtcState &s) {
    if (s.h.persisted >= s.h.count) return true;
    if (!mount()) return false;

    uint16_t n = s.h.count - s.h.persisted;
    const int16_t *data = s.samples + s.h.persisted;

    // Kruh 30 dni: nejdriv udelat misto smazanim nejstarsich bloku.
    std::vector<ChunkFile> chunks = listChunks();
    uint32_t total = n;
    for (const ChunkFile &c : chunks) total += chunkSlots(c.name);
    size_t removed = 0;
    while (total > STORE_MAX_SAMPLES && removed < chunks.size()) {
        total -= chunkSlots(chunks[removed].name);
        unlink(samplePath(chunks[removed].name).c_str());
        LOG("[flash] kruh 30 dni: smazan nejstarsi blok %s", chunks[removed].name.c_str());
        removed++;
    }
    if (removed) {
        chunks.erase(chunks.begin(), chunks.begin() + removed);
        dropOrphanSyncs(s.h.epoch, chunks);
    }

    ChunkHdr hdr = {};
    hdr.magic = CHUNK_MAGIC;
    hdr.epoch = s.h.epoch;
    hdr.start_mono_us = s.h.buf_start_mono_us + uint64_t(s.h.persisted) * SAMPLE_PERIOD_US;
    hdr.count = n;
    hdr.crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(data), n * sizeof(int16_t));

    char name[24];
    snprintf(name, sizeof name, "%08lx_%04x", (unsigned long)s.h.epoch, unsigned(s.h.chunk_seq));
    FILE *f = fopen(samplePath(name).c_str(), "wb");
    bool ok = f && fwrite(&hdr, sizeof hdr, 1, f) == 1 &&
              fwrite(data, sizeof(int16_t), n, f) == n;
    if (f) ok = (fclose(f) == 0) && ok;
    if (!ok) {
        LOG("[flash] zapis bloku %s selhal", name);
        unlink(samplePath(name).c_str());
        return false;
    }
    s.h.chunk_seq++;
    s.h.persisted = s.h.count;
    LOG("[flash] blok %s: %u slotu, ve flash celkem %lu", name, unsigned(n), (unsigned long)total);
    return true;
}

void storeAddSync(const RtcState &s, uint64_t mono_us, int64_t unix_us) {
    if (!mount()) return;
    FILE *f = fopen(syncPath(s.h.epoch).c_str(), "ab");
    if (!f) return;
    SyncRec r = {mono_us, unix_us};
    fwrite(&r, sizeof r, 1, f);
    fclose(f);
}

bool storeForEach(const RtcState &s, StoreSampleFn fn, void *ctx) {
    uint32_t no_time = 0;
    bool ok = true;

    if (mount()) {
        uint32_t loaded_epoch = UINT32_MAX;
        std::vector<SyncRec> h;
        ChunkHdr hdr;
        std::vector<int16_t> samples;
        for (const ChunkFile &c : listChunks()) {
            if (c.epoch != loaded_epoch) {
                h = readSyncs(c.epoch);
                loaded_epoch = c.epoch;
            }
            if (!readChunk(c.name, hdr, samples)) continue;
            if (!emit(samples.data(), hdr.count, hdr.start_mono_us, h, s.h.drift_ppb, fn, ctx,
                      no_time)) {
                ok = false;
                break;
            }
        }
    }

    if (ok && s.h.persisted < s.h.count) {
        std::vector<SyncRec> h = readSyncs(s.h.epoch);
        // Aktualni sync je i v RTC (kdyby zapis historie do flash selhal).
        if (s.h.flags & FLAG_TIME_VALID) h.push_back({s.h.sync_mono_us, s.h.sync_unix_us});
        std::sort(h.begin(), h.end(),
                  [](const SyncRec &a, const SyncRec &b) { return a.mono_us < b.mono_us; });
        h.erase(std::unique(h.begin(), h.end(),
                            [](const SyncRec &a, const SyncRec &b) { return a.mono_us == b.mono_us; }),
                h.end());
        ok = emit(s.samples + s.h.persisted, s.h.count - s.h.persisted,
                  s.h.buf_start_mono_us + uint64_t(s.h.persisted) * SAMPLE_PERIOD_US, h,
                  s.h.drift_ppb, fn, ctx, no_time);
    }
    if (no_time) LOG("[flash] %lu vzorku bez casu (epocha bez NTP) - neodeslany", (unsigned long)no_time);
    return ok;
}

void storeClearSent(RtcState &s) {
    s.h.persisted = s.h.count;
    if (!mount()) return;
    std::vector<ChunkFile> chunks = listChunks();
    for (const ChunkFile &c : chunks) unlink(samplePath(c.name).c_str());
    dropOrphanSyncs(s.h.epoch, {});

    // Historie aktualni epochy: staci posledni sync jako kotva pro dalsi vzorky.
    if (s.h.flags & FLAG_TIME_VALID) {
        FILE *f = fopen(syncPath(s.h.epoch).c_str(), "wb");
        if (f) {
            SyncRec r = {s.h.sync_mono_us, s.h.sync_unix_us};
            fwrite(&r, sizeof r, 1, f);
            fclose(f);
        }
    }
    LOG("[flash] odeslano - smazano %u bloku", unsigned(chunks.size()));
}
