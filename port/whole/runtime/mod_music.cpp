// A mod's music (wish of DevJunior, 19.11.2026: "a mod to change the battle and the exploration songs, closer to the
// remake"): mods/<pack>/music/<name>.ogg | .mp3 | .wav (found by dvd.cpp modFolder, the last pack's of a name in the
// folder's order), named by the zone and the situation that play them: village_explore, village_battle,
// castle_explore, castle_battle, island_explore, island_battle, and explore / battle for any zone without its own.
// runtime/music.cpp (the game's side) says each frame which one is wanted and how loud (re4_mod_music_want); here
// they are decoded as they play (stb_vorbis, minimp3, plain PCM WAV), looped, resampled (linear) to the AI's 32 kHz
// stereo and added to the blocks the AI starts (ai.cpp play, as runtime/mod_sound.cpp's sound), two decks crossfading
// over kFadeSeconds. No file of the disc: without a music folder nothing plays and nothing of the game changes.
// Everything here runs on the game's thread (the frame hook and the AI blocks with the retraces); the wanted track and
// gain cross over atomics anyway.
#include "re4_runtime.h"

#include <atomic>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// the decoders' declarations (their code: mod_music_ogg.c, mod_music_mp3.c, without warnings: theirs, unchanged)
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#include "../../third_party/minimp3/minimp3_ex.h"

namespace {

const unsigned kRate = 32000;
const float kFadeSeconds = 1.6f;
const int kTracks = 32;
const int kChunk = 2048;  // frames decoded at a time

struct Track {
    char name[64];     // the file's name without its type ("village_battle")
    char path[1600];
    bool failed;       // it could not be opened: not tried again (until a pack names it anew)
};
Track gTracks[kTracks];
int gCount;

// a file being played
struct Deck {
    int track = -1;
    int kind = 0;               // 1 OGG, 2 MP3, 3 WAV
    stb_vorbis* ogg = 0;
    mp3dec_ex_t* mp3 = 0;
    FILE* wav = 0;
    long wavData = 0;           // the WAV's samples: their place and size
    long wavBytes = 0;
    long wavRead = 0;
    unsigned channels = 0, rate = 0;
    int16_t buf[kChunk * 2];    // decoded, stereo
    int have = 0, at = 0;       // frames in buf, the next to use
    int16_t cur[2] = {0, 0}, next[2] = {0, 0};
    double frac = 0.0;
    float gain = 0.0f, target = 0.0f;  // the crossfade's share, 0..1 (the volume comes on top)
    bool dead = false;          // ended without a frame (an empty or broken file)
};
Deck gDeck[2];
int gFront = 0;  // the deck that plays the wanted track

std::atomic<int> gWantTrack(-1);
std::atomic<int> gWantGain(0);   // 0..256
std::atomic<int> gLevel(0);      // the louder deck's crossfade share now, 0..256 (for the game's ducking)

void close(Deck& d)
{
    if (d.ogg) {
        stb_vorbis_close(d.ogg);
    }
    if (d.mp3) {
        mp3dec_ex_close(d.mp3);
        free(d.mp3);
    }
    if (d.wav) {
        fclose(d.wav);
    }
    d.ogg = 0;
    d.mp3 = 0;
    d.wav = 0;
    d.track = -1;
    d.kind = 0;
    d.have = d.at = 0;
    d.gain = d.target = 0.0f;
    d.dead = false;
}

unsigned le16(const unsigned char* p) { return p[0] | (p[1] << 8); }
unsigned le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned) p[3] << 24); }

// A PCM 16-bit WAV's format and samples (the file's place)
bool openWav(Deck& d, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
        fclose(f);
        return false;
    }
    unsigned format = 0, bits = 0;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) {
            break;
        }
        const unsigned len = le32(c + 4);
        if (memcmp(c, "fmt ", 4) == 0 && len >= 16) {
            unsigned char b[40];
            const unsigned n = len < sizeof(b) ? len : (unsigned) sizeof(b);
            if (fread(b, 1, n, f) != n) {
                break;
            }
            format = le16(b);
            d.channels = le16(b + 2);
            d.rate = le32(b + 4);
            bits = le16(b + 14);
            if (format == 0xFFFE && n >= 26) {
                format = le16(b + 24);
            }
            fseek(f, (long) (len - n + (len & 1)), SEEK_CUR);
        } else if (memcmp(c, "data", 4) == 0) {
            d.wavData = ftell(f);
            d.wavBytes = (long) len;
            break;
        } else {
            fseek(f, (long) (len + (len & 1)), SEEK_CUR);
        }
    }
    if (format != 1 || bits != 16 || d.channels < 1 || d.channels > 2 || d.rate < 8000 || d.rate > 96000 ||
        !d.wavData) {
        fclose(f);
        return false;
    }
    d.wav = f;
    d.wavRead = 0;
    fseek(f, d.wavData, SEEK_SET);
    return true;
}

bool hasType(const char* path, const char* type)
{
    const size_t n = strlen(path), t = strlen(type);
    return n > t && strcasecmp(path + n - t, type) == 0;
}

bool open(Deck& d, int track)
{
    close(d);
    if (gTracks[track].failed) {
        return false;
    }
    const char* path = gTracks[track].path;
    if (hasType(path, ".ogg")) {
        int err = 0;
        d.ogg = stb_vorbis_open_filename(path, &err, 0);
        if (d.ogg) {
            const stb_vorbis_info info = stb_vorbis_get_info(d.ogg);
            d.channels = (unsigned) info.channels;
            d.rate = info.sample_rate;
            d.kind = 1;
        }
    } else if (hasType(path, ".mp3")) {
        d.mp3 = (mp3dec_ex_t*) calloc(1, sizeof(mp3dec_ex_t));
        if (d.mp3 && mp3dec_ex_open(d.mp3, path, MP3D_SEEK_TO_SAMPLE) == 0) {
            d.channels = (unsigned) d.mp3->info.channels;
            d.rate = (unsigned) d.mp3->info.hz;
            d.kind = 2;
        } else {
            free(d.mp3);
            d.mp3 = 0;
        }
    } else if (hasType(path, ".wav") && openWav(d, path)) {
        d.kind = 3;
    }
    if (!d.kind || d.channels < 1 || d.rate < 8000 || d.rate > 96000) {
        re4_log("music mod: %s cannot be played (OGG Vorbis, MP3 or 16-bit PCM WAV)", path);
        close(d);
        gTracks[track].failed = true;
        return false;
    }
    d.track = track;
    d.frac = 0.0;
    d.cur[0] = d.cur[1] = d.next[0] = d.next[1] = 0;
    re4_log("music mod: %s (%u Hz, %u channels)", gTracks[track].name, d.rate, d.channels);
    return true;
}

// The next decoded chunk (from the start again at the end); false when the file gives nothing
bool decode(Deck& d)
{
    for (int pass = 0; pass < 2; pass++) {
        int frames = 0;
        if (d.kind == 1) {
            frames = stb_vorbis_get_samples_short_interleaved(d.ogg, 2, d.buf, kChunk * 2);
            if (frames <= 0) {
                stb_vorbis_seek_start(d.ogg);
            }
        } else if (d.kind == 2) {
            static int16_t raw[kChunk * 2];
            const size_t want = (size_t) kChunk * d.channels;
            const size_t got = mp3dec_ex_read(d.mp3, raw, want);
            frames = (int) (got / d.channels);
            for (int i = 0; i < frames; i++) {
                d.buf[i * 2] = raw[i * d.channels];
                d.buf[i * 2 + 1] = raw[i * d.channels + (d.channels > 1 ? 1 : 0)];
            }
            if (frames <= 0) {
                mp3dec_ex_seek(d.mp3, 0);
            }
        } else if (d.kind == 3) {
            static int16_t raw[kChunk * 2];
            const long left = d.wavBytes - d.wavRead;
            long bytes = (long) kChunk * d.channels * 2;
            bytes = bytes < left ? bytes : left;
            const size_t got = bytes > 0 ? fread(raw, 1, (size_t) bytes, d.wav) : 0;
            d.wavRead += (long) got;
            frames = (int) (got / (2 * d.channels));
            for (int i = 0; i < frames; i++) {
                const unsigned char* s = (const unsigned char*) &raw[i * d.channels];
                const int16_t l = (int16_t) le16(s);
                const int16_t r = d.channels > 1 ? (int16_t) le16(s + 2) : l;
                d.buf[i * 2] = l;
                d.buf[i * 2 + 1] = r;
            }
            if (frames <= 0) {
                fseek(d.wav, d.wavData, SEEK_SET);
                d.wavRead = 0;
            }
        }
        if (frames > 0) {
            d.have = frames;
            d.at = 0;
            return true;
        }
    }
    return false;
}

void step(Deck& d)
{
    d.cur[0] = d.next[0];
    d.cur[1] = d.next[1];
    if (d.at >= d.have && !decode(d)) {
        d.dead = true;
        d.next[0] = d.next[1] = 0;
        return;
    }
    d.next[0] = d.buf[d.at * 2];
    d.next[1] = d.buf[d.at * 2 + 1];
    d.at++;
}

int16_t clamp16(int v)
{
    return (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

}  // namespace

extern "C" {

// dvd.cpp modFolder: a pack's mods/<pack>/music/<name>.<type>; a later pack's file of the same name replaces it
void re4_mod_music_add(const char* name, const char* path)
{
    char key[64];
    snprintf(key, sizeof(key), "%s", name);
    if (char* dot = strrchr(key, '.')) {
        *dot = 0;
    }
    for (char* c = key; *c; c++) {
        *c = (char) ((*c >= 'A' && *c <= 'Z') ? *c + 32 : *c);
    }
    int i = 0;
    while (i < gCount && strcmp(gTracks[i].name, key) != 0) {
        i++;
    }
    if (i == gCount) {
        if (gCount == kTracks) {
            re4_log("music mod: more than %d tracks, %s left out", kTracks, path);
            return;
        }
        gCount++;
    }
    snprintf(gTracks[i].name, sizeof(gTracks[i].name), "%s", key);
    snprintf(gTracks[i].path, sizeof(gTracks[i].path), "%s", path);
    gTracks[i].failed = false;
}

// runtime/music.cpp: the track of that name, -1 none
int re4_mod_music_find(const char* name)
{
    for (int i = 0; i < gCount; i++) {
        if (strcmp(gTracks[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int re4_mod_music_count(void)
{
    return gCount;
}

// runtime/music.cpp, each game frame: the track to play (-1 none) and its gain (0..256, the app's music volume in it)
void re4_mod_music_want(int track, int gain)
{
    gWantTrack.store(track >= 0 && track < gCount ? track : -1);
    gWantGain.store(gain < 0 ? 0 : gain > 256 ? 256 : gain);
}

// The music's crossfade share now (0..256, its volume aside): how much the game's own music gives way
// (runtime/music.cpp)
int re4_mod_music_level(void)
{
    return gLevel.load();
}

// ai.cpp play: on top of the game's block, 32 kHz stereo (L, R)
void re4_mod_music_mix(short* lr, unsigned frames)
{
    const int want = gWantTrack.load();
    const float gain = (float) gWantGain.load() / 256.0f;
    Deck& front = gDeck[gFront];
    if (want != front.track) {
        if (want >= 0) {
            // the new one on the other deck, the old one fading out there
            Deck& back = gDeck[gFront ^ 1];
            if (back.track == want && !back.dead) {
                gFront ^= 1;
            } else {
                close(back);
                if (open(back, want)) {
                    gFront ^= 1;
                }
            }
        }
        gDeck[gFront ^ 1].target = 0.0f;
    }
    if (gDeck[gFront].track == want && want >= 0) {
        gDeck[gFront].target = 1.0f;
    } else {
        gDeck[gFront].target = 0.0f;
    }
    const float fade = 1.0f / (kFadeSeconds * (float) kRate);
    int level = 0;
    for (Deck& d : gDeck) {
        if (d.track < 0 || d.dead) {
            continue;
        }
        const double by = (double) d.rate / (double) kRate;
        for (unsigned i = 0; i < frames; i++) {
            d.gain += d.gain < d.target ? fade : d.gain > d.target ? -fade : 0.0f;
            if (fabsf(d.gain - d.target) < fade) {
                d.gain = d.target;
            }
            const float a = (float) d.frac;
            const float l = d.cur[0] + (d.next[0] - d.cur[0]) * a;
            const float r = d.cur[1] + (d.next[1] - d.cur[1]) * a;
            lr[i * 2] = clamp16(lr[i * 2] + (int) (l * d.gain * gain));
            lr[i * 2 + 1] = clamp16(lr[i * 2 + 1] + (int) (r * d.gain * gain));
            d.frac += by;
            while (d.frac >= 1.0 && !d.dead) {
                d.frac -= 1.0;
                step(d);
            }
            if (d.dead) {
                break;
            }
        }
        const int g = (int) (d.gain * 256.0f);
        level = g > level ? g : level;
        if (d.gain <= 0.0f && d.target <= 0.0f && &d != &gDeck[gFront]) {
            close(d);  // faded out
        }
    }
    gLevel.store(level);
}

}  // extern "C"
