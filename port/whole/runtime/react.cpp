// The Ganados' hit reactions of the 2023 remake (wish of DevJunior, 15.11.2026: "the Ganados' animations": damage to the
// head, body, arms and legs). Compiled with the game's flags into libre4_main (re4_gamehooks_obj). The app's "Remake
// reactions" (the third aim word's bit 15, on by default) switches them; they need a mod pack's melee/react.dat
// (port/tools/re9_finisher_pack.py --react: the remake's chc0_damage_0th_com flinches, fitted to the Ganado's skeleton,
// at 1.5 times their speed and carried at most 1.6 m).
//
// Patch 0925 (em10.cpp Dm_Small, the flinch, where it sets the motion it chose by the hit zone) asks
// re4_react_motion: the remake's flinch of the same zone takes the motion's place. The game's choices stay where a
// motion carries the game's own logic: going down on the knees (0x51), the shield carriers, the chainsaw holders
// (Wep_type 0xC). The game's sequence holds the reaction's events (Free 1 the water splash, 2 "bent over": the kick /
// suplex prompt may come, 4 the early end, 0x80 down; and the voice's SE): a sequence made here puts each of its keys
// at the same share of the remake's motion, so the prompts and the end stay where the game had them.
// Side-specific flinches (an arm, a leg) are the remake's own left and right: the game's mirroring (Mot_attr 0x40)
// is taken off them; on the body's it stays (the game's variety).
#include "types.h"
#include "global.h"
#include "main.h"
#include "dvd.h"
#include "re4_runtime.h"
#include "em.h"
#include "em_sub.h"
#include "em10.h"
#include "motion.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" const char* re4_mod_react_path(void);  // runtime/dvd.cpp

namespace {

const int kClasses = 12;
const int kPerClass = 4;
const unsigned kMaxBytes = 0x100000;
const int kSeqMax = 160;   // keys of a made sequence at most (the remake's flinches: 27..102 frames)
const int kSeqs = 24;      // sequences made at once (one per flinching Ganado)

struct Seq {
    const void* em;
    u16 data[2 + 2 * kSeqMax];
};

std::atomic<int> gOn(-1);  // the app's switch; -1 none sent yet (the PC: RE4_REMAKE_REACT, else on)
char gLoaded[1600];
u8* gPlace;
u8* gMot[kClasses][kPerClass];
u16 gLast[kClasses][kPerClass];
int gNum[kClasses];
int gCount;
Seq gSeq[kSeqs];
int gNextSeq;
u32 gSeed = 0x6C8E9CF5u;
int gSaid;

u32 be32At(const u8* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

u16 be16At(const u8* p)
{
    return (u16) (p[0] << 8 | p[1]);
}

bool on()
{
    int v = gOn.load();
    if (v < 0) {
        const char* env = getenv("RE4_REMAKE_REACT");
        v = env ? atoi(env) != 0 : 1;
    }
    return v != 0;
}

void load()
{
    const char* path = re4_mod_react_path();
    if (strcmp(path, gLoaded) == 0) {
        return;
    }
    snprintf(gLoaded, sizeof(gLoaded), "%s", path);
    gCount = 0;
    memset(gNum, 0, sizeof(gNum));
    if (gPlace) {
        re4_mod_em_release(gPlace);
        gPlace = 0;
    }
    if (!*path) {
        return;
    }
    FILE* f = fopen(path, "rb");
    long size = -1;
    if (f && fseek(f, 0, SEEK_END) == 0) {
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    if (f && size >= 0x40 && size <= (long) kMaxBytes) {
        gPlace = (u8*) re4_mod_place_alloc((unsigned long) size, "the Ganados' remake reactions (melee/react.dat)");
    }
    u8* const base = gPlace;
    const bool whole = base && fread(base, 1, (size_t) size, f) == (size_t) size;
    if (f) {
        fclose(f);
    }
    const u32 count = whole ? be32At(base) : 0;
    bool ok = count >= 2 && count <= 128 && 0x10 + count * 8 <= (u32) size;
    static u32 at[129];
    for (u32 i = 0; ok && i < count; i++) {
        at[i] = be32At(base + 0x10 + i * 4);
        ok = at[i] >= 0x10 + count * 8 && at[i] < (u32) size && !(at[i] & 3) && (i == 0 || at[i] >= at[i - 1]);
    }
    const u8* tab = ok ? base + at[0] : 0;
    const u32 n = ok && at[1] - at[0] >= 8 && memcmp(tab, "RCT1", 4) == 0 ? be32At(tab + 4) : 0;
    ok = ok && n >= 1 && n <= 64 && 8 + n * 8 <= at[1] - at[0];
    u8* mot[kClasses][kPerClass];
    u16 last[kClasses][kPerClass];
    int num[kClasses];
    memset(num, 0, sizeof(num));
    for (u32 i = 0; ok && i < n; i++) {
        const u8* r = tab + 8 + i * 8;
        const u16 entry = be16At(r + 2), end = be16At(r + 4);
        ok = r[0] < kClasses && entry && entry < count && end >= 2 && end < kSeqMax &&
             be16At(base + at[entry]) == end && base[at[entry] + 2];
        if (ok && num[r[0]] < kPerClass) {
            mot[r[0]][num[r[0]]] = base + at[entry];
            last[r[0]][num[r[0]]++] = end;
        }
    }
    if (!ok) {
        re4_log("react: %s is no archive of the Ganados' reactions (%ld bytes): the game's", path, size);
        return;
    }
    DvdHeader h;
    memset(&h, 0, sizeof(h));
    h.type = TRANS_MRAM;
    h.size = (u32) size;
    h.dest = (u32) (unsigned long) base;
    re4_formats_part_loaded("mods/react.drs", 0, 0, &h, (unsigned) (unsigned long) base);
    memcpy(gMot, mot, sizeof(mot));
    memcpy(gLast, last, sizeof(last));
    memcpy(gNum, num, sizeof(num));
    for (int c = 0; c < kClasses; c++) {
        gCount += gNum[c];
    }
    re4_log("react: %s: %d reactions of the remake in %d classes", path, gCount, kClasses);
}

// The class of runtime/re9_finisher_pack.py's REACTS for Dm_Small's hit type and its chosen motion; -1 the game's
int classOf(cEm* em, int type, void* m0)
{
    void* const kneel = PL_ARC_PTR(em->subArc, 0x51);
    if (m0 == kneel) {
        return -1;
    }
    const bool heavy = m0 == PL_ARC_PTR(em->subArc, 0x37);
    switch (type) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 3;
    case 3:
        return 2;
    case 4:
        return 4;
    case 5:
        return heavy ? 6 : 5;
    case 6:
        return heavy ? 8 : 7;
    case 7:
        return 9;
    case 8:
        return 6;
    case 9:
        return 8;
    case 0xA:
        return 10;
    case 0xB:
        return 11;
    default:
        return -1;
    }
}

// The remake's motion `last` frames long with the game's sequence's events (`game`, 0 none) at the same shares of it
u16* sequence(const void* em, const u16* game, int last)
{
    Seq* s = 0;
    for (Seq& q : gSeq) {
        if (q.em == em) {
            s = &q;
        }
    }
    if (!s) {
        s = &gSeq[gNextSeq];
        gNextSeq = (gNextSeq + 1) % kSeqs;
    }
    s->em = em;
    const int keys = last + 1;
    memset(s->data, 0, sizeof(s->data));
    s->data[0] = (u16) keys;
    SEQUENCE_DATA* out = (SEQUENCE_DATA*) (s->data + 2);
    for (int i = 0; i < keys; i++) {
        out[i].Frame = (u16) (i * 64);
    }
    if (game && game[0] >= 2) {
        const int n = game[0];
        const SEQUENCE_DATA* in = (const SEQUENCE_DATA*) (game + 2);
        for (int j = 0; j < n; j++) {
            if (!in[j].Se && !in[j].Free) {
                continue;
            }
            int i = (int) ((f32) j * (f32) last / (f32) (n - 1) + 0.5f);
            i = i < 0 ? 0 : i > last ? last : i;
            if (in[j].Se && !out[i].Se) {
                out[i].Se = in[j].Se;
            }
            out[i].Free |= in[j].Free;
        }
    }
    return s->data;
}

}  // namespace

extern "C" {

void re4_react_set(int v)
{
    v = v ? 1 : 0;
    if (gOn.exchange(v) != v) {
        re4_log("react: the remake's reactions %s (a mod pack's melee/react.dat)", v ? "on" : "off");
    }
}

// Patch 0925 (em10.cpp Dm_Small, before it sets the flinch it chose): the remake's of the same zone in its place
void re4_react_motion(void* p, int type, void** m0, void** m1, int* flag)
{
    cEm* em = (cEm*) p;
    if (!em || !on()) {
        return;
    }
    load();
    if (!gCount) {
        return;
    }
    FREE_EM10* w = EM10_WK(em);
    if (w->pShield || w->Wep_type == 0xC) {
        return;
    }
    const int c = classOf(em, type, *m0);
    if (c < 0 || !gNum[c]) {
        return;
    }
    gSeed = gSeed * 1664525u + 1013904223u;
    const int k = (int) ((gSeed >> 16) % (u32) gNum[c]);
    *m1 = sequence(em, (const u16*) *m1, gLast[c][k]);
    *m0 = gMot[c][k];
    if (c != 0 && c != 9) {
        *flag &= ~0x40;  // an arm's or a leg's own side
    }
    if (!(gSaid & (1 << c))) {
        gSaid |= 1 << c;
        re4_log("react: hit type %d: the remake's flinch class %d (%d frames)", type, c, (int) gLast[c][k]);
    }
}

// For the tests: the reactions the active archive holds
int re4_react_count(void)
{
    load();
    return gCount;
}

}  // extern "C"
