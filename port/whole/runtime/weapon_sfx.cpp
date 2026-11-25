#include "weapon_sfx.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

extern "C" void re4_log(const char* fmt, ...);

namespace {

constexpr int kMaxWeaponId = 64;
constexpr int kVoices = 4;
constexpr unsigned kMaxFrames = 32000 * 6; // cap at 6s per gunshot

struct WeaponWave {
    int16_t* pcm{nullptr};
    unsigned frames{0};
    bool tried{false};
};

struct Voice {
    const int16_t* pcm{nullptr};
    unsigned frames{0};
    std::atomic<int> pos{-1};
    std::atomic<int> gain{0};
};

std::mutex gMutex;
std::string gWeaponsDir;
WeaponWave gBank[kMaxWeaponId];
Voice gVoices[kVoices];
int gNextVoice = 0;

std::atomic<int64_t> gSuppressUntilMs{0};

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int16_t clamp16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

// Reads a 16-bit PCM WAV (32 kHz stereo fast-path, or resamples from any rate/mono).
bool loadWavFile(const char* path, int16_t** outPcm, unsigned* outFrames)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[12];
    if (std::fread(hdr, 1, 12, f) != 12 || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        return false;
    }

    uint16_t fmtTag = 0, ch = 0, bits = 0;
    uint32_t rate = 0, dataSize = 0;
    long dataOffset = -1;
    uint8_t chunk[8];
    while (std::fread(chunk, 1, 8, f) == 8) {
        const uint32_t sz = le32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fmt[16];
            if (std::fread(fmt, 1, 16, f) != 16) break;
            fmtTag = le16(fmt);
            ch = le16(fmt + 2);
            rate = le32(fmt + 4);
            bits = le16(fmt + 14);
            if (sz > 16) std::fseek(f, sz - 16 + (sz & 1), SEEK_CUR);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataOffset = std::ftell(f);
            dataSize = sz;
            break;
        } else {
            std::fseek(f, sz + (sz & 1), SEEK_CUR);
        }
    }

    if (fmtTag != 1 || bits != 16 || (ch != 1 && ch != 2) || rate < 8000 || rate > 96000 ||
        dataOffset < 0 || dataSize < static_cast<uint32_t>(ch * 2)) {
        std::fclose(f);
        return false;
    }

    const unsigned srcFrames = dataSize / (ch * 2);
    auto* raw = static_cast<int16_t*>(std::malloc(srcFrames * ch * sizeof(int16_t)));
    if (!raw) {
        std::fclose(f);
        return false;
    }
    std::fseek(f, dataOffset, SEEK_SET);
    if (std::fread(raw, ch * 2, srcFrames, f) != srcFrames) {
        std::free(raw);
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    if (rate == 32000 && ch == 2) {
        const unsigned capped = srcFrames > kMaxFrames ? kMaxFrames : srcFrames;
        *outPcm = raw;
        *outFrames = capped;
        return true;
    }

    unsigned dstFrames = static_cast<unsigned>((static_cast<uint64_t>(srcFrames) * 32000ULL) / rate);
    if (dstFrames > kMaxFrames) dstFrames = kMaxFrames;
    if (dstFrames == 0) {
        std::free(raw);
        return false;
    }
    auto* dst = static_cast<int16_t*>(std::malloc(dstFrames * 2 * sizeof(int16_t)));
    if (!dst) {
        std::free(raw);
        return false;
    }
    for (unsigned i = 0; i < dstFrames; ++i) {
        const double srcPos = (static_cast<double>(i) * rate) / 32000.0;
        const unsigned i0 = static_cast<unsigned>(srcPos);
        const unsigned i1 = (i0 + 1 < srcFrames) ? i0 + 1 : i0;
        const double frac = srcPos - i0;
        const int l0 = raw[i0 * ch];
        const int l1 = raw[i1 * ch];
        const int r0 = ch == 2 ? raw[i0 * 2 + 1] : l0;
        const int r1 = ch == 2 ? raw[i1 * 2 + 1] : l1;
        dst[i * 2] = static_cast<int16_t>(l0 + (l1 - l0) * frac);
        dst[i * 2 + 1] = static_cast<int16_t>(r0 + (r1 - r0) * frac);
    }
    std::free(raw);
    *outPcm = dst;
    *outFrames = dstFrames;
    return true;
}

const WeaponWave* getOrLoadWeapon(int weaponNo)
{
    if (weaponNo <= 0 || weaponNo >= kMaxWeaponId) return nullptr;
    std::lock_guard<std::mutex> lock(gMutex);
    WeaponWave& slot = gBank[weaponNo];
    if (!slot.tried) {
        slot.tried = true;
        const char* baseDir = !gWeaponsDir.empty()
                                  ? gWeaponsDir.c_str()
                                  : "/storage/emulated/0/Android/data/com.ast.cubeevil4/files/mods/audio/weapons";
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/wep%02d.wav", baseDir, weaponNo);
        if (loadWavFile(path, &slot.pcm, &slot.frames)) {
            re4_log("weapon_sfx: loaded wep%02d.wav (%u frames)", weaponNo, slot.frames);
        }
    }
    return slot.pcm && slot.frames > 0 ? &slot : nullptr;
}

}  // namespace

extern "C" {

void re4_weapon_sfx_set_dir(const char* dir)
{
    std::lock_guard<std::mutex> lock(gMutex);
    const std::string next = dir ? dir : "";
    if (next == gWeaponsDir) return;
    gWeaponsDir = next;
    for (int i = 0; i < kMaxWeaponId; ++i) {
        if (gBank[i].pcm) {
            std::free(gBank[i].pcm);
            gBank[i].pcm = nullptr;
        }
        gBank[i].frames = 0;
        gBank[i].tried = false;
    }
    for (auto& v : gVoices) {
        v.pos.store(-1);
        v.pcm = nullptr;
        v.frames = 0;
    }
}

void re4_weapon_sfx_fire(int weaponNo, int effectsVolumePercent)
{
    const WeaponWave* w = getOrLoadWeapon(weaponNo);
    if (!w) return;
    const int clampedVol = effectsVolumePercent < 0 ? 0 : (effectsVolumePercent > 100 ? 100 : effectsVolumePercent);
    const int gain = clampedVol * 256 / 100;
    if (gain > 0) {
        Voice& v = gVoices[gNextVoice % kVoices];
        gNextVoice = (gNextVoice + 1) % kVoices;
        v.pos.store(-1);
        v.pcm = w->pcm;
        v.frames = w->frames;
        v.gain.store(gain);
        v.pos.store(0);
    }
    gSuppressUntilMs.store(nowMs() + 180, std::memory_order_relaxed);
}

int re4_weapon_sfx_suppress(int blk2No)
{
    if (blk2No > 2) return 0;
    const int64_t until = gSuppressUntilMs.load(std::memory_order_relaxed);
    if (until > 0 && nowMs() <= until) {
        gSuppressUntilMs.store(0, std::memory_order_relaxed);
        return 1;
    }
    return 0;
}

void re4_weapon_sfx_mix(int16_t* lr, unsigned frames)
{
    if (!lr || frames == 0) return;
    for (auto& v : gVoices) {
        int start = v.pos.load(std::memory_order_relaxed);
        if (start < 0 || !v.pcm) continue;
        const int16_t* pcm = v.pcm;
        const unsigned total = v.frames;
        const int gain = v.gain.load(std::memory_order_relaxed);
        int pos = start;
        for (unsigned i = 0; i < frames && static_cast<unsigned>(pos) < total; ++i, ++pos) {
            lr[i * 2] = clamp16(lr[i * 2] + ((pcm[pos * 2] * gain) >> 8));
            lr[i * 2 + 1] = clamp16(lr[i * 2 + 1] + ((pcm[pos * 2 + 1] * gain) >> 8));
        }
        v.pos.compare_exchange_strong(start, static_cast<unsigned>(pos) < total ? pos : -1);
    }
}

}  // extern "C"
