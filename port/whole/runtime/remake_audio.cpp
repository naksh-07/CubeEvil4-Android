#include "remake_audio.h"
#include "weapon_sfx.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

extern "C" void re4_log(const char* fmt, ...);

namespace {

struct XwbEntry {
    uint32_t formatTag{};
    uint32_t channels{};
    uint32_t rate{};
    uint32_t playOffset{};
    uint32_t playLength{};
};

struct LoadedTrack {
    int index{-1};
    uint32_t rate{48000};
    uint32_t channels{2};
    std::vector<int16_t> samples; // interleaved L, R
    size_t frames{0};
};

std::mutex gConfigMutex;
std::string gXwbPath;
uint32_t gWaveBaseOffset{0};
std::vector<XwbEntry> gEntries;
std::atomic<bool> gEnabled{false};
std::atomic<bool> gAvailable{false};

std::mutex gPlaybackMutex;
std::shared_ptr<const LoadedTrack> gActiveTrack;
int gRequestedTrack{-1};
int gLoadingTrack{-1};
double gPhase{0.0};
std::atomic<int> gStreamVol{127};
std::atomic<int> gMusicPercent{100};
std::atomic<int> gPaused{0};
std::atomic<int> gPlaying{0};

uint32_t readU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void startLoadTrack(int trackIndex, std::string path, uint32_t waveBase, XwbEntry entry)
{
    std::thread([trackIndex, path = std::move(path), waveBase, entry]() {
        if (entry.formatTag != 0 || entry.playLength == 0) {
            return;
        }
        const uint32_t ch = entry.channels ? entry.channels : 2;
        const size_t bytesPerFrame = ch * 2;
        const size_t totalFrames = entry.playLength / bytesPerFrame;
        if (totalFrames == 0) {
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        file.seekg(static_cast<std::streamoff>(waveBase) + entry.playOffset);
        std::vector<uint8_t> raw(entry.playLength);
        if (!file.read(reinterpret_cast<char*>(raw.data()), entry.playLength)) {
            return;
        }

        auto track = std::make_shared<LoadedTrack>();
        track->index = trackIndex;
        track->rate = entry.rate ? entry.rate : 48000;
        track->channels = ch;
        track->frames = totalFrames;
        track->samples.resize(totalFrames * 2);

        if (ch >= 2) {
            for (size_t i = 0; i < totalFrames; ++i) {
                const size_t p = i * ch * 2;
                track->samples[i * 2] = static_cast<int16_t>(raw[p] | (static_cast<uint16_t>(raw[p + 1]) << 8));
                track->samples[i * 2 + 1] = static_cast<int16_t>(raw[p + 2] | (static_cast<uint16_t>(raw[p + 3]) << 8));
            }
        } else {
            for (size_t i = 0; i < totalFrames; ++i) {
                const size_t p = i * 2;
                const int16_t s = static_cast<int16_t>(raw[p] | (static_cast<uint16_t>(raw[p + 1]) << 8));
                track->samples[i * 2] = s;
                track->samples[i * 2 + 1] = s;
            }
        }

        std::lock_guard<std::mutex> lock(gPlaybackMutex);
        if (gRequestedTrack == trackIndex) {
            gActiveTrack = track;
            gPhase = 0.0;
            gLoadingTrack = -1;
            gPlaying.store(1, std::memory_order_relaxed);
            re4_log("remake_audio: loaded track %d (%zu frames, %u Hz)", trackIndex, totalFrames, track->rate);
        }
    }).detach();
}

} // namespace

extern "C" {

void re4_remake_audio_configure(int enabled, const char* xwbPath)
{
    std::lock_guard<std::mutex> lock(gConfigMutex);
    const std::string path = xwbPath ? xwbPath : "";
    if (!path.empty()) {
        const size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            re4_weapon_sfx_set_dir((path.substr(0, slash) + "/weapons").c_str());
        }
    }

    if (path != gXwbPath) {
        gXwbPath = path;
        gEntries.clear();
        gWaveBaseOffset = 0;
        gAvailable.store(false, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> pLock(gPlaybackMutex);
            gActiveTrack.reset();
            gRequestedTrack = -1;
            gLoadingTrack = -1;
            gPlaying.store(0, std::memory_order_relaxed);
        }

        if (!path.empty()) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                uint8_t hdr[64];
                if (file.read(reinterpret_cast<char*>(hdr), sizeof(hdr)) &&
                    hdr[0] == 'W' && hdr[1] == 'B' && hdr[2] == 'N' && hdr[3] == 'D') {
                    const uint32_t seg0Off = readU32(hdr + 12);
                    const uint32_t seg0Len = readU32(hdr + 16);
                    const uint32_t seg1Off = readU32(hdr + 20);
                    const uint32_t seg1Len = readU32(hdr + 24);
                    const uint32_t seg4Off = readU32(hdr + 44);

                    if (seg0Len >= 80) {
                        file.seekg(seg0Off);
                        std::vector<uint8_t> bank(seg0Len);
                        if (file.read(reinterpret_cast<char*>(bank.data()), seg0Len)) {
                            const uint32_t count = readU32(bank.data() + 4);
                            const uint32_t elemSize = readU32(bank.data() + 72);
                            if (count > 0 && count <= 1024 && elemSize >= 24 && seg1Len >= count * elemSize) {
                                file.seekg(seg1Off);
                                std::vector<uint8_t> meta(count * elemSize);
                                if (file.read(reinterpret_cast<char*>(meta.data()), meta.size())) {
                                    gEntries.resize(count);
                                    for (uint32_t i = 0; i < count; ++i) {
                                        const uint8_t* e = meta.data() + i * elemSize;
                                        const uint32_t fmt = readU32(e + 4);
                                        gEntries[i].formatTag = fmt & 0x3;
                                        gEntries[i].channels = (fmt >> 2) & 0x7;
                                        gEntries[i].rate = (fmt >> 5) & 0x3FFFF;
                                        gEntries[i].playOffset = readU32(e + 8);
                                        gEntries[i].playLength = readU32(e + 12);
                                    }
                                    gWaveBaseOffset = seg4Off;
                                    gAvailable.store(true, std::memory_order_relaxed);
                                    re4_log("remake_audio: configured %s (%u tracks)", path.c_str(), count);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    const bool want = (enabled != 0) && gAvailable.load(std::memory_order_relaxed);
    if (gEnabled.exchange(want) != want) {
        re4_log("remake_audio: %s", want ? "ENABLED (Remake 2023)" : "DISABLED (Original 2005)");
    }
    if (!want) {
        std::lock_guard<std::mutex> pLock(gPlaybackMutex);
        gActiveTrack.reset();
        gRequestedTrack = -1;
        gLoadingTrack = -1;
        gPlaying.store(0, std::memory_order_relaxed);
    }
}

void re4_remake_audio_set_active(int trackIndex, int streamVol, int musicPercent, int paused)
{
    gStreamVol.store(std::clamp(streamVol, 0, 127), std::memory_order_relaxed);
    gMusicPercent.store(std::clamp(musicPercent, 0, 100), std::memory_order_relaxed);
    gPaused.store(paused ? 1 : 0, std::memory_order_relaxed);

    if (!gEnabled.load(std::memory_order_relaxed) || !gAvailable.load(std::memory_order_relaxed) || trackIndex < 0) {
        std::lock_guard<std::mutex> pLock(gPlaybackMutex);
        gRequestedTrack = -1;
        gActiveTrack.reset();
        gPlaying.store(0, std::memory_order_relaxed);
        return;
    }

    std::lock_guard<std::mutex> pLock(gPlaybackMutex);
    if (gRequestedTrack == trackIndex) {
        return;
    }
    gRequestedTrack = trackIndex;
    gActiveTrack.reset();
    gPlaying.store(0, std::memory_order_relaxed);

    std::lock_guard<std::mutex> cLock(gConfigMutex);
    if (static_cast<size_t>(trackIndex) < gEntries.size() && gLoadingTrack != trackIndex) {
        gLoadingTrack = trackIndex;
        startLoadTrack(trackIndex, gXwbPath, gWaveBaseOffset, gEntries[trackIndex]);
    }
}

int re4_remake_audio_is_playing(void)
{
    return gEnabled.load(std::memory_order_relaxed) && gPlaying.load(std::memory_order_relaxed);
}

void re4_remake_audio_mix(int16_t* out, int frames, int channels)
{
    if (!out || frames <= 0 || channels < 2) {
        return;
    }
    if (!gEnabled.load(std::memory_order_relaxed) || gPaused.load(std::memory_order_relaxed)) {
        return;
    }

    std::shared_ptr<const LoadedTrack> track;
    double phase = 0.0;
    {
        std::lock_guard<std::mutex> lock(gPlaybackMutex);
        track = gActiveTrack;
        phase = gPhase;
    }

    if (!track || track->frames == 0) {
        return;
    }

    const int vol = gStreamVol.load(std::memory_order_relaxed);
    const int pct = gMusicPercent.load(std::memory_order_relaxed);
    const double gain = (static_cast<double>(vol) / 127.0) * (static_cast<double>(pct) / 100.0);
    if (gain <= 0.0001) {
        return;
    }

    const double step = static_cast<double>(track->rate) / 32000.0;
    const size_t total = track->frames;
    const int16_t* pcm = track->samples.data();

    for (int i = 0; i < frames; ++i) {
        if (phase >= static_cast<double>(total)) {
            phase -= static_cast<double>(total);
        }
        const size_t idx0 = static_cast<size_t>(phase);
        const size_t idx1 = (idx0 + 1 < total) ? (idx0 + 1) : 0;
        const double frac = phase - static_cast<double>(idx0);

        const double l = (pcm[idx0 * 2] * (1.0 - frac) + pcm[idx1 * 2] * frac) * gain;
        const double r = (pcm[idx0 * 2 + 1] * (1.0 - frac) + pcm[idx1 * 2 + 1] * frac) * gain;

        const int32_t mixL = static_cast<int32_t>(out[i * channels + 0]) + static_cast<int32_t>(l);
        const int32_t mixR = static_cast<int32_t>(out[i * channels + 1]) + static_cast<int32_t>(r);

        out[i * channels + 0] = static_cast<int16_t>(std::clamp(mixL, -32768, 32767));
        out[i * channels + 1] = static_cast<int16_t>(std::clamp(mixR, -32768, 32767));

        phase += step;
    }

    std::lock_guard<std::mutex> lock(gPlaybackMutex);
    if (gActiveTrack == track) {
        gPhase = phase;
    }
}

} // extern "C"
