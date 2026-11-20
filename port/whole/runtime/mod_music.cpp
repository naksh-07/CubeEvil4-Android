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
#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <type_traits>

// the decoders' declarations (their code: mod_music_ogg.c, mod_music_mp3.c, without warnings: theirs, unchanged)
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#include "../../third_party/minimp3/minimp3_ex.h"

namespace {

// ---- Android's own decoders (libmediandk, Android 5 and later; looked up at run time, none on the Linux builds) ------
// (Mods, 19.11.2026; DevJunior's village_battle.mp3 could not be played by minimp3: a file of a phone's downloader may
// be AAC in an MP4 under the .mp3 name.) Anything the phone plays (AAC / M4A, Opus, FLAC, WebM, MP3 ...) is decoded on a
// thread of its own into a ring the mixer takes from.
struct CodecBufferInfo {
    int32_t offset;
    int32_t size;
    int64_t presentationTimeUs;
    uint32_t flags;
};
struct Ndk {
    bool tried = false, ok = false;
    void* (*exNew)();
    int (*exSetFd)(void*, int, int64_t, int64_t);
    size_t (*exTracks)(void*);
    void* (*exFormat)(void*, size_t);
    int (*exSelect)(void*, size_t);
    ssize_t (*exRead)(void*, uint8_t*, size_t);
    int64_t (*exTime)(void*);
    bool (*exAdvance)(void*);
    int (*exSeek)(void*, int64_t, int);
    int (*exDelete)(void*);
    bool (*fmtString)(void*, const char*, const char**);
    bool (*fmtInt)(void*, const char*, int32_t*);
    int (*fmtDelete)(void*);
    void* (*codecNew)(const char*);
    int (*codecConfigure)(void*, const void*, void*, void*, uint32_t);
    int (*codecStart)(void*);
    int (*codecStop)(void*);
    int (*codecDelete)(void*);
    int (*codecFlush)(void*);
    ssize_t (*codecInput)(void*, int64_t);
    uint8_t* (*codecInBuf)(void*, size_t, size_t*);
    int (*codecQueue)(void*, size_t, long, size_t, uint64_t, uint32_t);
    ssize_t (*codecOutput)(void*, CodecBufferInfo*, int64_t);
    uint8_t* (*codecOutBuf)(void*, size_t, size_t*);
    int (*codecRelease)(void*, size_t, bool);
    void* (*codecOutFormat)(void*);
};
Ndk gNdk;

bool ndkLoad()
{
    if (gNdk.tried) {
        return gNdk.ok;
    }
    gNdk.tried = true;
    void* lib = dlopen("libmediandk.so", RTLD_NOW);
    if (!lib) {
        return false;
    }
    bool all = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<typename std::remove_reference<decltype(fn)>::type>(dlsym(lib, name));
        all = all && fn;
    };
    get(gNdk.exNew, "AMediaExtractor_new");
    get(gNdk.exSetFd, "AMediaExtractor_setDataSourceFd");
    get(gNdk.exTracks, "AMediaExtractor_getTrackCount");
    get(gNdk.exFormat, "AMediaExtractor_getTrackFormat");
    get(gNdk.exSelect, "AMediaExtractor_selectTrack");
    get(gNdk.exRead, "AMediaExtractor_readSampleData");
    get(gNdk.exTime, "AMediaExtractor_getSampleTime");
    get(gNdk.exAdvance, "AMediaExtractor_advance");
    get(gNdk.exSeek, "AMediaExtractor_seekTo");
    get(gNdk.exDelete, "AMediaExtractor_delete");
    get(gNdk.fmtString, "AMediaFormat_getString");
    get(gNdk.fmtInt, "AMediaFormat_getInt32");
    get(gNdk.fmtDelete, "AMediaFormat_delete");
    get(gNdk.codecNew, "AMediaCodec_createDecoderByType");
    get(gNdk.codecConfigure, "AMediaCodec_configure");
    get(gNdk.codecStart, "AMediaCodec_start");
    get(gNdk.codecStop, "AMediaCodec_stop");
    get(gNdk.codecDelete, "AMediaCodec_delete");
    get(gNdk.codecFlush, "AMediaCodec_flush");
    get(gNdk.codecInput, "AMediaCodec_dequeueInputBuffer");
    get(gNdk.codecInBuf, "AMediaCodec_getInputBuffer");
    get(gNdk.codecQueue, "AMediaCodec_queueInputBuffer");
    get(gNdk.codecOutput, "AMediaCodec_dequeueOutputBuffer");
    get(gNdk.codecOutBuf, "AMediaCodec_getOutputBuffer");
    get(gNdk.codecRelease, "AMediaCodec_releaseOutputBuffer");
    get(gNdk.codecOutFormat, "AMediaCodec_getOutputFormat");
    gNdk.ok = all;
    return all;
}

const unsigned kRing = 1u << 16;  // frames (stereo) decoded ahead by Android's decoder: about 1.4 s at 48 kHz

// one file through Android's decoder: its thread fills the ring, the mixer takes from it
struct Media {
    int fd = -1;
    void* ex = 0;
    void* codec = 0;
    pthread_t thread;
    bool running = false;
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<unsigned> wr{0}, rd{0};
    std::atomic<unsigned> rate{0}, channels{0};
    std::atomic<int> encoding{2};  // 2 PCM 16-bit, 4 float
    int16_t ring[kRing * 2];
};

void* mediaThread(void* arg)
{
    Media& m = *(Media*) arg;
    bool inputEnd = false;
    unsigned long long total = 0;
    while (!m.stop.load()) {
        if (kRing - (m.wr.load() - m.rd.load()) < 16384) {
            usleep(4000);
            continue;
        }
        if (!inputEnd) {
            const ssize_t in = gNdk.codecInput(m.codec, 2000);
            if (in >= 0) {
                size_t cap = 0;
                uint8_t* buf = gNdk.codecInBuf(m.codec, (size_t) in, &cap);
                const ssize_t n = buf ? gNdk.exRead(m.ex, buf, cap) : -1;
                if (n < 0) {
                    gNdk.codecQueue(m.codec, (size_t) in, 0, 0, 0, 4 /* END_OF_STREAM */);
                    inputEnd = true;
                } else {
                    const int64_t t = gNdk.exTime(m.ex);
                    gNdk.codecQueue(m.codec, (size_t) in, 0, (size_t) n, (uint64_t) (t < 0 ? 0 : t), 0);
                    gNdk.exAdvance(m.ex);
                }
            }
        }
        CodecBufferInfo info;
        const ssize_t out = gNdk.codecOutput(m.codec, &info, 2000);
        if (out == -2) {  // INFO_OUTPUT_FORMAT_CHANGED
            void* f = gNdk.codecOutFormat(m.codec);
            int32_t v = 0;
            if (f && gNdk.fmtInt(f, "sample-rate", &v) && v > 0) {
                m.rate.store((unsigned) v);
            }
            if (f && gNdk.fmtInt(f, "channel-count", &v) && v > 0) {
                m.channels.store((unsigned) v);
            }
            if (f && gNdk.fmtInt(f, "pcm-encoding", &v)) {
                m.encoding.store(v);
            }
            if (f) {
                gNdk.fmtDelete(f);
            }
            continue;
        }
        if (out < 0) {
            continue;
        }
        size_t size = 0;
        const uint8_t* data = gNdk.codecOutBuf(m.codec, (size_t) out, &size);
        const unsigned ch = m.channels.load() ? m.channels.load() : 2;
        const bool fl = m.encoding.load() == 4;
        const unsigned bytes = fl ? 4u : 2u;
        if (data && info.size > 0 && (size_t) info.offset + (size_t) info.size <= size) {
            const uint8_t* p = data + info.offset;
            const unsigned frames = (unsigned) info.size / (bytes * ch);
            unsigned w = m.wr.load();
            for (unsigned i = 0; i < frames && w - m.rd.load() < kRing; i++, w++) {
                int16_t l, r;
                if (fl) {
                    float a, b;
                    memcpy(&a, p + (size_t) i * ch * 4, 4);
                    memcpy(&b, p + ((size_t) i * ch + (ch > 1 ? 1 : 0)) * 4, 4);
                    a = a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a;
                    b = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;
                    l = (int16_t) (a * 32767.0f);
                    r = (int16_t) (b * 32767.0f);
                } else {
                    memcpy(&l, p + (size_t) i * ch * 2, 2);
                    memcpy(&r, p + ((size_t) i * ch + (ch > 1 ? 1 : 0)) * 2, 2);
                }
                m.ring[(w % kRing) * 2] = l;
                m.ring[(w % kRing) * 2 + 1] = r;
            }
            total += frames;
            m.wr.store(w);
        }
        gNdk.codecRelease(m.codec, (size_t) out, false);
        if (info.flags & 4) {  // the end: from the start again (a file that gave nothing at all has failed)
            if (!total) {
                m.failed.store(true);
                break;
            }
            gNdk.exSeek(m.ex, 0, 0 /* PREVIOUS_SYNC */);
            gNdk.codecFlush(m.codec);
            inputEnd = false;
        }
    }
    return 0;
}

void mediaClose(Media*& m)
{
    if (!m) {
        return;
    }
    if (m->running) {
        m->stop.store(true);
        pthread_join(m->thread, 0);
    }
    if (m->codec) {
        gNdk.codecStop(m->codec);
        gNdk.codecDelete(m->codec);
    }
    if (m->ex) {
        gNdk.exDelete(m->ex);
    }
    if (m->fd >= 0) {
        ::close(m->fd);
    }
    delete m;
    m = 0;
}

// the file's first audio track through Android's decoder; 0 when the phone cannot play it (or on Linux)
Media* mediaOpen(const char* path, const char** why)
{
    if (!ndkLoad()) {
        *why = "no Android decoder here";
        return 0;
    }
    Media* m = new Media();
    struct stat st;
    m->fd = ::open(path, O_RDONLY);
    if (m->fd < 0 || fstat(m->fd, &st) != 0) {
        *why = "it cannot be opened";
        mediaClose(m);
        return 0;
    }
    m->ex = gNdk.exNew();
    if (!m->ex || gNdk.exSetFd(m->ex, m->fd, 0, (int64_t) st.st_size) != 0) {
        *why = "Android does not know its container";
        mediaClose(m);
        return 0;
    }
    const size_t n = gNdk.exTracks(m->ex);
    for (size_t i = 0; i < n && !m->codec; i++) {
        void* f = gNdk.exFormat(m->ex, i);
        const char* mime = 0;
        if (f && gNdk.fmtString(f, "mime", &mime) && mime && strncmp(mime, "audio/", 6) == 0) {
            int32_t v = 0;
            if (gNdk.fmtInt(f, "sample-rate", &v)) {
                m->rate.store((unsigned) v);
            }
            if (gNdk.fmtInt(f, "channel-count", &v)) {
                m->channels.store((unsigned) v);
            }
            gNdk.exSelect(m->ex, i);
            m->codec = gNdk.codecNew(mime);
            if (m->codec && (gNdk.codecConfigure(m->codec, f, 0, 0, 0) != 0 || gNdk.codecStart(m->codec) != 0)) {
                gNdk.codecDelete(m->codec);
                m->codec = 0;
            }
            re4_log("music mod: %s through Android's decoder (%s, %u Hz, %u channels)%s", path, mime,
                    m->rate.load(), m->channels.load(), m->codec ? "" : ": no decoder for it");
        }
        if (f) {
            gNdk.fmtDelete(f);
        }
    }
    if (!m->codec) {
        *why = "no audio track Android can decode";
        mediaClose(m);
        return 0;
    }
    if (pthread_create(&m->thread, 0, mediaThread, m) != 0) {
        *why = "no thread";
        mediaClose(m);
        return 0;
    }
    m->running = true;
    return m;
}

// what the file's first bytes say it is
const char* sniff(const char* path, unsigned char* head, size_t n)
{
    memset(head, 0, n);
    FILE* f = fopen(path, "rb");
    if (!f) {
        return "unreadable";
    }
    const size_t got = fread(head, 1, n, f);
    fclose(f);
    if (got < 12) {
        return "too short";
    }
    if (memcmp(head, "OggS", 4) == 0) {
        return "ogg";
    }
    if (memcmp(head, "RIFF", 4) == 0 && memcmp(head + 8, "WAVE", 4) == 0) {
        return "wav";
    }
    if (memcmp(head, "ID3", 3) == 0 || (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)) {
        return "mp3";
    }
    if (memcmp(head + 4, "ftyp", 4) == 0) {
        return "mp4";  // AAC in an MP4 / M4A
    }
    if (head[0] == 0x1A && head[1] == 0x45 && head[2] == 0xDF && head[3] == 0xA3) {
        return "webm";
    }
    if (memcmp(head, "fLaC", 4) == 0) {
        return "flac";
    }
    return "unknown";
}

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
    int kind = 0;               // 1 OGG, 2 MP3, 3 WAV, 4 Android's decoder
    Media* media = 0;
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
std::atomic<int> gCut(0);        // 1: silent at once, no fade (a remake radio call; re4_mod_music_cut)

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
    mediaClose(d.media);
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
    // by what the file is, not by its name (a phone's downloader may give an AAC file the name .mp3)
    unsigned char head[16];
    const char* type = sniff(path, head, sizeof(head));
    int mp3err = 0;
    if (strcmp(type, "ogg") == 0) {
        int err = 0;
        d.ogg = stb_vorbis_open_filename(path, &err, 0);
        if (d.ogg) {
            const stb_vorbis_info info = stb_vorbis_get_info(d.ogg);
            d.channels = (unsigned) info.channels;
            d.rate = info.sample_rate;
            d.kind = 1;
        }
    } else if (strcmp(type, "mp3") == 0) {
        d.mp3 = (mp3dec_ex_t*) calloc(1, sizeof(mp3dec_ex_t));
        if (d.mp3 && (mp3err = mp3dec_ex_open(d.mp3, path, MP3D_SEEK_TO_SAMPLE)) == 0) {
            d.channels = (unsigned) d.mp3->info.channels;
            d.rate = (unsigned) d.mp3->info.hz;
            d.kind = 2;
        } else {
            free(d.mp3);
            d.mp3 = 0;
        }
    } else if (strcmp(type, "wav") == 0 && openWav(d, path)) {
        d.kind = 3;
    }
    const char* why = "";
    if (!d.kind || d.channels < 1 || d.rate < 8000 || d.rate > 96000) {
        if (d.kind) {
            close(d);
        }
        // everything else (and what these could not read: Opus in Ogg, a broken MP3 ...) through Android's decoder
        d.media = mediaOpen(path, &why);
        if (d.media) {
            d.kind = 4;
            d.channels = 2;
            d.rate = d.media->rate.load() ? d.media->rate.load() : 44100;
        }
    }
    if (!d.kind || d.rate < 8000 || d.rate > 96000) {
        re4_log("music mod: %s cannot be played (looks like %s: %02x %02x %02x %02x %02x %02x %02x %02x; minimp3 %d; "
                "Android: %s)", path, type, head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7],
                mp3err, why);
        close(d);
        gTracks[track].failed = true;
        return false;
    }
    d.track = track;
    d.frac = 0.0;
    d.cur[0] = d.cur[1] = d.next[0] = d.next[1] = 0;
    re4_log("music mod: %s (%s, %u Hz, %u channels)", gTracks[track].name, d.kind == 4 ? "Android's decoder" : type,
            d.rate, d.channels);
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
        if (d.kind == 4) {
            Media& m = *d.media;
            const unsigned rd = m.rd.load(), avail = m.wr.load() - rd;
            frames = (int) (avail < (unsigned) kChunk ? avail : (unsigned) kChunk);
            for (int i = 0; i < frames; i++) {
                d.buf[i * 2] = m.ring[((rd + i) % kRing) * 2];
                d.buf[i * 2 + 1] = m.ring[((rd + i) % kRing) * 2 + 1];
            }
            m.rd.store(rd + (unsigned) frames);
            if (m.rate.load() >= 8000 && m.rate.load() <= 96000) {
                d.rate = m.rate.load();  // AAC with SBR: the decoder says the true rate once it runs
            }
            if (!frames) {
                if (m.failed.load()) {
                    return false;
                }
                frames = 64;  // not decoded yet (the start): a moment of silence, not the end
                memset(d.buf, 0, sizeof(int16_t) * 2 * 64);
            }
            d.have = frames;
            d.at = 0;
            return true;
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

// runtime/music.cpp: 1 = the music falls silent at once, without the crossfade (a radio call: DevJunior,
// 20.11.2026, "the calls should stop the mod's songs dead"); the track stays open at gain 0 and fades back in
// where it was when it is wanted again
void re4_mod_music_cut(int on)
{
    gCut.store(on ? 1 : 0);
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
    if (gCut.load()) {
        for (Deck& d : gDeck) {
            d.gain = d.target = 0.0f;
        }
    }
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
