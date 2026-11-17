// Leon's movements of the 2023 remake (wish of DevJunior, 13.11.2026: "remaster Leon's movements"; phase 1: stand, walk,
// run, walk back). Compiled with the game's flags into libre4_main (re4_gamehooks_obj). The app's "Remake movement"
// (the third aim word's bit 14, on by default) switches it; it needs a mod pack's melee/move.dat
// (port/tools/re9_finisher_pack.py --move: one seamless cycle of each movement from the PC mod's cha0_general).
//
// Patch 0923 (pl_wep.cpp cPlayer::weaponInit, after the weapon's module filled the motion table m_MotTbl) asks
// re4_move_table: with bare hands (weapon 0, the quick slots' "weapon put away"), a handgun (em_sub.cpp HandgunCk) or a
// grenade the
// table's stand (0 / 1), walk (2 / 3), run (6 / 7) and back step (8 / 9) take the remake's motion and a sequence made
// here. The remake's motions are unarmed ones: with a rifle or shotgun in both hands the game's own stay, and so do the
// hurt motions (life under two thirds: m_MotTbl 0x5F..), the turns in place and everything else.
// The game's speed stays: per movement the game's own motion tells how far Leon goes a frame (its root joint, kind 1,
// over its sequence's frames); the remake's cycle is played faster or slower by the square root of the ratio (its
// sequence maps frames in 10.6 fixed point; at most kTimeMin..kTimeMax) and its root motion scaled by the rest, so the
// feet slide as little as the game's pace allows. The footstep sounds are the game's own two of that movement, on the
// frames the remake's feet strike.
//
// The transitions (wish of DevJunior, 16.11.2026: "starts, stops and turns as in the remake"; Leon still turns his body
// to where he goes): move.dat's clips (codes 0x80..0x87 in its table) play between the game's own motions. A frame
// hook (before the routines) sees the game set a movement's motion and puts the remake's clip first: setting off
// into a walk or a run from standing, stopping into the stand from a walk or a run; when a clip ends the movement's
// loop follows (hokan kLoopHokan). With "Free movement" (runtime/free_move.cpp asks re4_move_turn before it turns
// Leon toward the stick) a stick pointing back (beyond kTurnStart) turns him about with the remake's turn: running,
// the jog's 180-degree turn, standing, the turn in place; their root turn (joint kind 0x40) turns Leon, free_move
// leaves his heading alone meanwhile. The clips play at their loop's pace (the walk's or the run's speed and root
// scale; the turns kTurnSpeed); any routine change of the game cuts them (its own motion replaces the clip).
#include "types.h"
#include "global.h"
#include "main.h"
#include "dvd.h"
#include "re4_runtime.h"
#include "player.h"
#include "motion.h"
#include "em.h"
#include "em_sub.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" const char* re4_mod_move_path(void);              // runtime/dvd.cpp
extern "C" int re4_free_move_on(void);                       // runtime/aim_options.cpp
extern cPlayer* pPL;

namespace {

const int kMoves = 4;
const int kSlots[kMoves] = {0, 2, 6, 8};  // m_MotTbl: stand, walk, run, back step (the sequence in the next slot)
const char* const kNames[kMoves] = {"stand", "walk", "run", "back step"};
// The head's turn toward an enemy (cPlNeck) blends the body with the game's own "looking right / left" variant of the
// stand, the walk and the run (m_MotTbl 0x3F / 0x39 / 0x41 and the next slot): two different walks mixed, out of step
// (device report 13.11.2026: the walk hitched and repeated). With the remake's motion in a slot its variants are
// taken out (cPlNeck::init leaves invalid ones alone) and the blend's rate held at zero.
const int kNeck[kMoves] = {0x3F, 0x39, 0x41, 0};
const f32 kTimeMin = 0.75f, kTimeMax = 1.35f;
const unsigned kMaxBytes = 0x80000;  // (with the transitions: 0.16 MB)
const int kSeqMax = 255;

struct Move {
    u8* mot;        // the remake's motion (0 none)
    u8 flags;       // 1: matched to the game's speed
    u16 last;       // its last frame (the first pose again)
    u16 foot[2];    // the frames the right and the left foot strike (0xFFFF none)
    f32 scale;      // the scale its root motion has now
    f32 time;       // its playing speed (1: the remake's own)
    void* orig;     // the game's motion and sequence it stands in for now
    void* origSeq;
    void* neck[2];  // the game's looking variants taken out
    bool neckOut;
    u16 seq[2 + 2 * (kSeqMax + 1)];
};

std::atomic<int> gOn(-1);  // the app's switch; -1 none sent yet (the PC: RE4_REMAKE_MOVE, else on)
char gLoaded[1600];
u8* gPlace;
Move gMove[kMoves];
int gCount;
// the transitions' clips: 0 / 1 setting off into / stopping from a walk, 2 / 3 a run, 4 / 5 the run's turn about to the
// left / right, 6 / 7 the turn about standing (move.dat codes 0x80 + i)
// 8..11 the sprint: its loop, setting off into it from standing, stopping from it, going into it from the jog
const int kClips = 12;
const char* const kClipNames[kClips] = {"walk start",   "walk stop",      "run start",  "run stop",
                                        "run turn left", "run turn right", "turn left",  "turn right",
                                        "sprint",        "sprint start",   "sprint stop", "sprint from the jog"};
// The sprint (wish of DevJunior, 17.11.2026: "jog with the stick, really run with the run button, as the remake"):
// with the remake's movements and Free movement, the stick pushed beyond kJogTilt runs (the game's run routine:
// the remake's jog), the run key sprints (its dash, kSprintSpeed times the jog's pace, its own start, stop and the
// way into it from the jog); a light push walks. aim_options.cpp re4_run_held asks re4_move_sprint.
const f32 kJogTilt = 0.6f;
const f32 kSprintSpeed = 1.1f;
bool gSprintKey;  // the run key held (the sprint)
Move gClip[kClips];
int gPlaying = -1;     // the clip Leon plays (-1 none)
int gWasMoving = 0;    // the movement he was in (1 walk, 3 run; 0 none): the stop's clip
const f32 kTurnStart = 2.4f;   // radians the stick points away from him for a turn about (about 140 degrees)
const f32 kTurnSpeed = 1.35f;  // the turns' speed against the remake's own (its 0.9 / 1.5 s too slow beside the game)
const int kClipHokan = 5;      // frames a clip blends in
const int kLoopHokan = 6;      // frames the loop after it blends in
int gApplied = -1;         // what the table holds: 1 the remake's, 0 the game's, -1 unknown

u32 be32At(const u8* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

u16 be16At(const u8* p)
{
    return (u16) (p[0] << 8 | p[1]);
}

u16 get16(const u8* p)
{
    u16 v;
    memcpy(&v, p, 2);
    return v;
}

f32 keyF32(const u8* p)
{
    const u32 u = be32At(p);
    f32 v;
    memcpy(&v, &u, 4);
    return v;
}

void putKeyF32(u8* p, f32 v)
{
    u32 u;
    memcpy(&u, &v, 4);
    p[0] = (u8) (u >> 24), p[1] = (u8) (u >> 16), p[2] = (u8) (u >> 8), p[3] = (u8) u;
}

int keyBytes(int fmt)
{
    static const int kBytes[16] = {12, 8, 6, 0, 10, 6, 4, 0, 9, 5, 3, 0, 0, 0, 0, 4};
    return kBytes[fmt & 15];
}

bool on()
{
    int v = gOn.load();
    if (v < 0) {
        const char* env = getenv("RE4_REMAKE_MOVE");
        v = env ? atoi(env) != 0 : 1;
    }
    return v != 0;
}

// The root position's curve of a converted motion (its joint of kind 1: three axes of u16 count, u16 frames, keys whose
// values stay big-endian), 0 without one; the offsets are relative until the game first plays the motion
u8* rootCurve(u8* m, int* fmt)
{
    const unsigned n = m[2];
    const u32 table = (3 + n * 3 + 3) & ~3u;
    for (unsigned j = 0; j < n; j++) {
        const u16 kind = get16(m + 3 + j * 2);
        if ((kind & 0xFF) != 1) {
            continue;
        }
        u32 off;
        memcpy(&off, m + table + 4 + j * 4, 4);
        *fmt = kind >> 12;
        return (s32) off >= 0 ? m + off : (u8*) (unsigned long) off;
    }
    return 0;
}

// How far the root goes over the motion on the floor (x, z: the last key against the first); false when it cannot be read
bool travel(u8* m, f32* dx, f32* dz)
{
    int fmt = 0;
    u8* p = m ? rootCurve(m, &fmt) : 0;
    if (!p || fmt > 2) {
        return false;
    }
    f32 d[3] = {0.0f, 0.0f, 0.0f};
    for (int axis = 0; axis < 3; axis++) {
        const unsigned count = get16(p);
        if (count == 0 || count > 0x400) {
            return false;
        }
        const u8* keys = p + 2 + count * 2;
        d[axis] = keyF32(keys + (count - 1) * keyBytes(fmt)) - keyF32(keys);
        p = (u8*) keys + count * keyBytes(fmt);
    }
    *dx = d[0], *dz = d[2];
    return d[0] == d[0] && d[2] == d[2] && fabsf(d[0]) < 100000.0f && fabsf(d[2]) < 100000.0f;
}

// The remake's root motion (ours: f32 keys with f32 slopes) scaled on the floor
void scaleRoot(Move& mv, f32 to)
{
    int fmt = 0;
    u8* p = rootCurve(mv.mot, &fmt);
    if (!p || fmt != 0 || mv.scale <= 0.0f || to <= 0.0f || to == mv.scale) {
        return;
    }
    const f32 k = to / mv.scale;
    for (int axis = 0; axis < 3; axis++) {
        const unsigned count = get16(p);
        u8* keys = p + 2 + count * 2;
        if (axis != 1) {
            for (unsigned i = 0; i < count * 3; i++) {
                putKeyF32(keys + i * 4, keyF32(keys + i * 4) * k);
            }
        }
        p = keys + count * 12;
    }
    mv.scale = to;
}

void load()
{
    const char* path = re4_mod_move_path();
    if (strcmp(path, gLoaded) == 0) {
        return;
    }
    snprintf(gLoaded, sizeof(gLoaded), "%s", path);
    gCount = 0;
    for (Move& m : gMove) {
        m.mot = 0;
    }
    for (Move& m : gClip) {
        m.mot = 0;
    }
    gPlaying = -1;
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
        gPlace = (u8*) re4_mod_place_alloc((unsigned long) size, "Leon's remake movements (melee/move.dat)");
    }
    u8* const base = gPlace;
    const bool whole = base && fread(base, 1, (size_t) size, f) == (size_t) size;
    if (f) {
        fclose(f);
    }
    const u32 count = whole ? be32At(base) : 0;
    bool ok = count >= 2 && count <= 64 && 0x10 + count * 8 <= (u32) size;
    u32 at[65];
    for (u32 i = 0; ok && i < count; i++) {
        at[i] = be32At(base + 0x10 + i * 4);
        ok = at[i] >= 0x10 + count * 8 && at[i] < (u32) size && !(at[i] & 3) && (i == 0 || at[i] >= at[i - 1]);
    }
    const u8* tab = ok ? base + at[0] : 0;
    const u32 n = ok && at[1] - at[0] >= 8 && memcmp(tab, "MOV1", 4) == 0 ? be32At(tab + 4) : 0;
    ok = ok && n >= 1 && n <= 32 && 8 + n * 12 <= at[1] - at[0];
    Move found[kMoves];
    memset(found, 0, sizeof(found));
    static Move clips[kClips];
    memset(clips, 0, sizeof(clips));
    for (u32 i = 0; ok && i < n; i++) {
        const u8* r = tab + 8 + i * 12;
        const u16 entry = be16At(r + 2), last = be16At(r + 4);
        if (entry == 0 || entry >= count || last < 2 || be16At(base + at[entry]) != last || !base[at[entry] + 2]) {
            ok = false;
            break;
        }
        if (r[0] >= 0x80 && r[0] < 0x80 + kClips && last <= kSeqMax) {
            Move& c = clips[r[0] - 0x80];
            c.mot = base + at[entry];
            c.last = last;
            c.scale = 1.0f;
            c.time = 1.0f;
        }
        for (int k = 0; k < kMoves; k++) {
            if (kSlots[k] == r[0]) {
                found[k].mot = base + at[entry];
                found[k].flags = r[1];
                found[k].last = last;
                found[k].foot[0] = be16At(r + 6);
                found[k].foot[1] = be16At(r + 8);
                found[k].scale = 1.0f;
            }
        }
    }
    if (!ok) {
        re4_log("move: %s is no archive of Leon's movements (%ld bytes): the game's", path, size);
        return;
    }
    DvdHeader h;
    memset(&h, 0, sizeof(h));
    h.type = TRANS_MRAM;
    h.size = (u32) size;
    h.dest = (u32) (unsigned long) base;
    re4_formats_part_loaded("mods/move.drs", 0, 0, &h, (unsigned) (unsigned long) base);
    for (int k = 0; k < kMoves; k++) {
        gMove[k].mot = found[k].mot;
        gMove[k].flags = found[k].flags;
        gMove[k].last = found[k].last;
        gMove[k].foot[0] = found[k].foot[0];
        gMove[k].foot[1] = found[k].foot[1];
        gMove[k].scale = 1.0f;
        gMove[k].orig = gMove[k].origSeq = 0;
        if (gMove[k].mot) {
            gCount++;
            re4_log("move: %s: the remake's %s, %u frames", path, kNames[k], (unsigned) gMove[k].last);
        }
    }
    int nClips = 0;
    for (int i = 0; i < kClips; i++) {
        gClip[i].mot = clips[i].mot;
        gClip[i].last = clips[i].last;
        gClip[i].scale = 1.0f;
        gClip[i].time = 1.0f;
        nClips += gClip[i].mot ? 1 : 0;
    }
    if (nClips) {
        re4_log("move: %s: %d of the remake's transitions (starts, stops, turns)", path, nClips);
    }
    gApplied = -1;
}

// The remake's movement k in place of the game's (`mot`, `seq`): its root motion scaled and its sequence made
void fit(int k, void* mot, void* seq)
{
    Move& mv = gMove[k];
    const f32 frames = (f32) mv.last;
    f32 time = 1.0f, scale = 1.0f, pace = 0.0f, own = 0.0f;
    if (mv.flags & 1) {
        f32 gx = 0.0f, gz = 0.0f, ox = 0.0f, oz = 0.0f;
        const u8* gm = (const u8*) mot;
        const unsigned gFrames = seq ? get16((const u8*) seq) : (unsigned) (get16(gm) & 0x3FFF);
        scaleRoot(mv, 1.0f);
        if (gFrames && travel((u8*) mot, &gx, &gz) && travel(mv.mot, &ox, &oz)) {
            pace = sqrtf(gx * gx + gz * gz) / (f32) gFrames;
            own = sqrtf(ox * ox + oz * oz) / frames;
        }
        if (pace > 5.0f && own > 5.0f) {
            time = sqrtf(pace / own);
            time = time < kTimeMin ? kTimeMin : time > kTimeMax ? kTimeMax : time;
        }
    }
    int keys = (int) (frames / time + 0.5f);
    keys = keys < 2 ? 2 : keys > kSeqMax ? kSeqMax : keys;
    if (pace > 5.0f && own > 5.0f) {
        scale = pace * (f32) keys / (own * frames);
        scaleRoot(mv, scale);
    }
    mv.time = time;
    // the footsteps: the game's own two sounds of this movement, in their order
    u8 se[2] = {(u8) (k == 1 ? 3 : 1), (u8) (k == 1 ? 4 : 2)};
    if (seq) {
        const u8* s = (const u8*) seq;
        int got = 0;
        for (unsigned i = 0; i < get16(s) && got < 2; i++) {
            if (s[4 + i * 4 + 2]) {
                se[got++] = s[4 + i * 4 + 2];
            }
        }
    }
    memset(mv.seq, 0, sizeof(mv.seq));
    mv.seq[0] = (u16) keys;
    SEQUENCE_DATA* key = (SEQUENCE_DATA*) (mv.seq + 2);
    for (int i = 0; i < keys; i++) {
        key[i].Frame = (u16) ((f32) i * frames / (f32) keys * 64.0f + 0.5f);
    }
    for (int foot = 0; foot < 2; foot++) {
        if (mv.foot[foot] != 0xFFFF && k != 0) {
            int at = (int) ((f32) mv.foot[foot] * (f32) keys / frames + 0.5f);
            key[at >= keys ? keys - 1 : at].Se = se[foot];
        }
    }
    re4_log("move: the remake's %s for weapon %d: the game's pace %.0f a frame, the cycle's %.0f: played at %.2f (%d "
            "frames), its steps scaled %.2f",
            kNames[k], (int) pG->weapon_no, (double) pace, (double) own, (double) time, keys, (double) scale);
}

// A transition's clip at its loop's pace (the walk's for 0 / 1, the run's for 2..5, kTurnSpeed for the turns): its
// root motion scaled as the loop's, its sequence made (no events)
void fitClip(int i)
{
    Move& c = gClip[i];
    if (!c.mot) {
        return;
    }
    const Move* base = i < 2 ? &gMove[1] : i < 6 || i >= 8 ? &gMove[2] : 0;
    f32 time = base && base->mot ? base->time : 1.0f;
    if (i >= 4 && i < 8) {
        time *= kTurnSpeed;
    }
    if (i == 8 || i == 9 || i == 11) {
        time *= kSprintSpeed;
    }
    scaleRoot(c, base && base->mot ? base->scale : 1.0f);
    const f32 frames = (f32) c.last;
    int keys = (int) (frames / time + 0.5f);
    keys = keys < 2 ? 2 : keys > kSeqMax ? kSeqMax : keys;
    c.time = time;
    memset(c.seq, 0, sizeof(c.seq));
    c.seq[0] = (u16) keys;
    SEQUENCE_DATA* key = (SEQUENCE_DATA*) (c.seq + 2);
    for (int k = 0; k < keys; k++) {
        key[k].Frame = (u16) ((f32) k * frames / (f32) (keys - 1) * 64.0f + 0.5f);
    }
}

// The throwables (read.cpp groups them for the other characters: hand, incendiary and flash grenades, the eggs), held in
// one hand as a handgun is (wish of DevJunior, 13.11.2026: "with grenades the remake's movements go away")
bool throwable(int no)
{
    return no == 0x13 || no == 0x16 || no == 0x17 || no == 0x19 || no == 0x1F || no == 0x20;
}

bool fits()
{
    return pG && pG->pl_type == 0 && (pG->weapon_no == 0 || HandgunCk(pG->weapon_no) || throwable(pG->weapon_no));
}

// The table's four movements: the remake's (the game's kept aside) or the game's back
void apply(cPlayer* pl, bool fresh)
{
    if (!pl || !pl->m_MotTbl) {
        return;
    }
    load();
    const bool want = on() && gCount && fits();
    void** tbl = pl->m_MotTbl;
    for (int k = 0; k < kMoves; k++) {
        Move& mv = gMove[k];
        const int slot = kSlots[k];
        const bool ours = tbl[slot] == mv.mot && mv.mot;
        if (fresh || !ours) {
            if (!ours) {
                mv.orig = tbl[slot];
                mv.origSeq = tbl[slot + 1];
            }
        }
        if (want && mv.mot && mv.orig) {
            fit(k, mv.orig, mv.origSeq);
            tbl[slot] = mv.mot;
            tbl[slot + 1] = mv.seq;
            if (kNeck[k] && (fresh || !mv.neckOut)) {
                mv.neck[0] = tbl[kNeck[k]];
                mv.neck[1] = tbl[kNeck[k] + 1];
                mv.neckOut = true;
            }
            if (kNeck[k]) {
                tbl[kNeck[k]] = tbl[kNeck[k] + 1] = 0;
            }
        } else if (ours) {
            tbl[slot] = mv.orig;
            tbl[slot + 1] = mv.origSeq;
            if (kNeck[k] && mv.neckOut) {
                tbl[kNeck[k]] = mv.neck[0];
                tbl[kNeck[k] + 1] = mv.neck[1];
            }
            mv.neckOut = false;
        } else if (fresh) {
            mv.neckOut = false;
        }
    }
    if (want) {
        for (int i = 0; i < kClips; i++) {
            fitClip(i);
        }
    }
    gPlaying = -1;
    gApplied = want ? 1 : 0;
}

bool isClip(const void* m)
{
    for (const Move& c : gClip) {
        if (c.mot && m == (const void*) c.mot) {
            return true;
        }
    }
    return false;
}

void play(cPlayer* pl, int i)
{
    Move& c = gClip[i];
    MotionSetCore(pl, &pl->Motion, c.mot, c.seq, kClipHokan, 5, 0);
    if (pl->pMotionB) {
        pl->pMotionB->Brate = 0.0f;
    }
    gPlaying = i;
    static int said;
    if (!(said & (1 << i))) {
        said |= 1 << i;
        re4_log("move: the remake's %s (%u frames at %.2f)", kClipNames[i], (unsigned) c.seq[0], (double) c.time);
    }
}

// Before the routines: the clips around the game's own movements (see the head)
void transitions(cPlayer* pl)
{
    const void* cur = (const void*) pl->Motion.pMot;
    const int r1 = pl->r_no_1;
    if (pl->r_no_0 != 0 || r1 > 4) {
        gPlaying = -1;
        gWasMoving = 0;
        return;
    }
    if (gPlaying == 8 && cur == (const void*) gClip[8].mot && (r1 != 3 || !gSprintKey)) {
        // the sprint let go: back to the jog (the run routine), its stop when the game stands him
        if (r1 == 3 && gMove[2].mot) {
            MotionSetCore(pl, &pl->Motion, gMove[2].mot, gMove[2].seq, 8, 5, 0);
            gPlaying = -1;
            gWasMoving = 3;
            return;
        }
    }
    if (gPlaying >= 0) {
        Move& c = gClip[gPlaying];
        if (cur != (const void*) c.mot) {
            gPlaying = -1;  // the game set its own motion: the clip is cut
            if (c.mot == gClip[8].mot) {
                gWasMoving = 4;  // the sprint's stop when the game stands him
            }
        } else {
            if (gPlaying == 8) {
                return;  // the sprint's loop plays round until the key or the run ends (below)
            }
            if (pl->Motion.Seq_frame >= (f32) c.seq[0] - 2.0f) {
                const int k = r1 == 1 ? 1 : r1 == 3 ? 2 : r1 == 2 ? 3 : 0;
                if (r1 == 3 && gSprintKey && gClip[8].mot) {
                    play(pl, 8);
                    gWasMoving = 4;
                    return;
                }
                if (gMove[k].mot) {
                    MotionSetCore(pl, &pl->Motion, gMove[k].mot, gMove[k].seq, kLoopHokan, 5, 0);
                }
                gPlaying = -1;
                gWasMoving = r1 == 1 || r1 == 3 ? r1 : 0;
            }
            return;
        }
    }
    const bool fresh = pl->Motion.Seq_frame < 3.0f;
    if (cur == (const void*) gMove[1].mot && gMove[1].mot) {
        if (r1 == 1 && fresh && gWasMoving == 0 && gClip[0].mot) {
            play(pl, 0);
        }
        gWasMoving = 1;
    } else if (cur == (const void*) gMove[2].mot && gMove[2].mot) {
        if (r1 == 3 && gSprintKey && gClip[8].mot) {
            // into the sprint: from standing its start, from the jog its way in (or its loop)
            play(pl, fresh && gWasMoving == 0 && gClip[9].mot ? 9 : gClip[11].mot ? 11 : 8);
            gWasMoving = 4;
            return;
        }
        if (r1 == 3 && fresh && gWasMoving == 0 && gClip[2].mot) {
            play(pl, 2);
        }
        gWasMoving = 3;
    } else if (cur == (const void*) gMove[0].mot && gMove[0].mot) {
        if (r1 == 0 && fresh && gWasMoving) {
            const int i = gWasMoving == 4 ? (gClip[10].mot ? 10 : 3) : gWasMoving == 3 ? 3 : 1;
            if (gClip[i].mot) {
                play(pl, i);
            }
        }
        gWasMoving = 0;
    } else {
        gWasMoving = 0;  // another motion (aiming, the back step, an action): no stop's clip after it
    }
}

// The switch or the pack changed while Leon walks: the table follows (in the field's plain routines only)
void watch(unsigned)
{
    if (!pPL || !pG || pG->pl_type != 0 || gApplied < 0) {
        return;
    }
    load();
    const bool want = on() && fits() && gCount;
    if (gApplied == 1 && pPL->pMotionB) {  // one of the remake's motions plays: no blend with a game's variant
        for (const Move& mv : gMove) {
            if (mv.mot && (void*) pPL->Motion.pMot == (void*) mv.mot) {
                pPL->pMotionB->Brate = 0.0f;
            }
        }
        if (isClip((const void*) pPL->Motion.pMot)) {
            pPL->pMotionB->Brate = 0.0f;
        }
    }
    if (gApplied == 1 && want) {
        transitions(pPL);
    } else {
        gPlaying = -1;
        gWasMoving = 0;
    }
    if ((want ? 1 : 0) != gApplied && pPL->r_no_0 == 0 && pPL->r_no_1 == 0) {
        apply(pPL, false);
        pPL->r_no_2 = 0;  // the footwork sets its motion anew
        pPL->r_no_3 = 0;
    }
}

}  // namespace

extern "C" {

void re4_move_set(int v)
{
    v = v ? 1 : 0;
    if (gOn.exchange(v) != v) {
        re4_log("move: the remake's movements %s (a mod pack's melee/move.dat)", v ? "on" : "off");
    }
}

// Patch 0923 (cPlayer::weaponInit): the weapon's module has filled the motion table
void re4_move_table(void* p)
{
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        re4_add_frame_hook(watch);
    }
    cPlayer* pl = (cPlayer*) p;
    if (!pG || pG->pl_type != 0) {
        return;
    }
    apply(pl, true);
}

// runtime/free_move.cpp (before it turns Leon toward the stick; `diff` the stick's direction against his heading,
// radians, positive to his left; `armed`: the stick held firmly back for a few frames, free_move.cpp): 1 = a turn
// about plays (he keeps his heading, the clip turns him)
int re4_move_turn(void* p, float diff, int armed)
{
    cPlayer* pl = (cPlayer*) p;
    if (!pl || gApplied != 1 || !on() || pl->r_no_0 != 0) {
        return 0;
    }
    const void* cur = (const void*) pl->Motion.pMot;
    if (gPlaying >= 0 && gPlaying != 8) {
        return gPlaying >= 4 && gPlaying < 8 && cur == (const void*) gClip[gPlaying].mot;
    }
    if (!armed || fabsf(diff) < kTurnStart) {
        return 0;
    }
    const bool left = diff > 0.0f;
    if (pl->r_no_1 == 3 && gMove[2].mot && gClip[left ? 4 : 5].mot &&
        (cur == (const void*) gMove[2].mot || (gClip[8].mot && cur == (const void*) gClip[8].mot))) {
        play(pl, left ? 4 : 5);
        return 1;
    }
    if (pl->r_no_1 == 0 && gClip[left ? 6 : 7].mot &&
        ((gMove[0].mot && cur == (const void*) gMove[0].mot) || cur == (const void*) gClip[1].mot ||
         cur == (const void*) gClip[3].mot)) {
        play(pl, left ? 6 : 7);
        return 1;
    }
    return 0;
}

// For the tests: the movements the active archive holds
int re4_move_count(void)
{
    load();
    return gCount;
}

// aim_options.cpp re4_run_held (the game's run checks, patch 0156): -1 without the sprint (the game's / "Always
// run"'s way), else whether Leon runs: the run key (the sprint) or the stick pushed beyond kJogTilt (the jog)
int re4_move_sprint(int held)
{
    gSprintKey = held != 0;
    if (gApplied != 1 || !on() || !gClip[8].mot || !re4_free_move_on()) {
        return -1;
    }
    const f32 sx = (f32) Key.stickX / 56.0f, sy = (f32) Key.stickY / 56.0f;
    return held || sx * sx + sy * sy >= kJogTilt * kJogTilt ? 1 : 0;
}

// For the tests: the transitions' clips the active archive holds
int re4_move_clips(void)
{
    load();
    int n = 0;
    for (const Move& c : gClip) {
        n += c.mot ? 1 : 0;
    }
    return n;
}

}  // extern "C"
