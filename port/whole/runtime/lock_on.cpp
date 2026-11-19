// The player's lock-on target for the app's "Auto aim" (patch 0918, pl_wep.cpp SearchLockEm; wish of DevJunior,
// 12.11.2026: "with an enemy near it sometimes prefers the ones lying on the ground to the standing ones, sometimes
// goes for the traps first, and it does not reach far"). Compiled with the game's flags into libre4_main.
//
// The game's searchLockEm scores the squared distance with steps for "beyond 4 m", "outside the 50-degree cone",
// "beyond 15 m", up to 10 m, over every lockable character: a bear trap close by beats a Ganado 5 m ahead, and a
// Ganado on the ground beats one standing a little further. Here, over the same characters and the game's own
// checks (lockEmCk: alive, lockable, active, its lock point not behind a wall):
//   - only enemies: no village dog (0x21), crow (0x23), cow (0x26), fish (0x27), chicken (0x28), trap (0x2A), nor
//     anything from 0x3B on (the truck, the armour, the helicopter, doors, boxes, barrels, mines ... cEmMgr::idName);
//   - up to kRange (25 m);
//   - the score: the distance times (1 + kTurn x the turn from Leon's facing, radians), behind him (beyond 100
//     degrees) only when nothing is in front;
//   - lying (its lock point under kDown above its feet: a Ganado knocked down) only when no standing one is in reach.
// The one locked now (lockNext's `now`) is skipped and taken again when nothing else qualifies, as the game does.
// The lock then holds kFrames game frames (the game: 10; the right stick ends it at once, cPlWep::lockMove).
#include "types.h"
#include "global.h"
#include "em.h"
#include "player.h"
#include "math_sub.h"
#include "pl_wep.h"
#include "main.h"

#include <math.h>

extern cPlayer* pPL;
extern "C" void re4_log(const char* fmt, ...);
int lockEmCk(cEm* pEm, Vec* pPos);  // game/pl_wep.cpp

namespace {

const f32 kRange = 25000.0f;  // 25 m
const f32 kTurn = 1.5f;
const f32 kBehind = 1.75f;    // 100 degrees
const f32 kDown = 700.0f;     // 70 cm
const int kFrames = 20;
const f32 kPiF = 3.14159265f;

bool enemy(int id)
{
    switch (id) {
    case 0x21:  // the village dog (runs from Leon)
    case 0x23:  // crow
    case 0x26:  // cow
    case 0x27:  // fish
    case 0x28:  // chicken
    case 0x2A:  // bear trap, trip-wire bomb
        return false;
    default:
        return id > 0xF && id < 0x3B;
    }
}

}  // namespace

extern "C" {

// For the runtime test: the score of one character from `pos` (negative: not a target).
float re4_lock_score(void* character, void* at, int checkWalls)
{
    Vec* pos = (Vec*) at;
    cEm* em = (cEm*) character;
    if (!em || !pPL || !enemy(em->id)) {
        return -1.0f;
    }
    if (checkWalls && lockEmCk(em, pos) == 0) {
        return -1.0f;
    }
    Vec v;
    PSMTXMultVec(em->getPartsPtr(em->lockParts)->mat, &em->lockOfs, &v);
    const f32 d = sqrtf(GetDistance(pos, &v));
    if (d > kRange) {
        return -1.0f;
    }
    const f32 a = fabsf(GetXZAngleLocal(pos, &v, pPL->ang.y));
    f32 score = d * (1.0f + kTurn * a);
    if (a > kBehind) {
        score += 1.0e7f;  // behind him: only when nothing is in front
    }
    if (v.y - em->pos.y < kDown) {
        score += 1.0e6f;  // lying: only when no standing one is in reach
    }
    return score;
}

// Patch 0918 (SearchLockEm with the lock-on on): the best target from `pos`, 0 none.
void* re4_lock_search(void* at, void* now)
{
    Vec* pos = (Vec*) at;
    cEm* best = 0;
    f32 bestScore = 0.0f;
    for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
        cEm* em = EmMgr.fastAt(i);
        if (em == (cEm*) now) {
            continue;
        }
        const f32 s = re4_lock_score(em, pos, 1);
        if (s >= 0.0f && (!best || s < bestScore)) {
            best = em;
            bestScore = s;
        }
    }
    if (!best && now && re4_lock_score(now, pos, 1) >= 0.0f) {
        best = (cEm*) now;
    }
    return best;
}

// Patch 0918 (lockInit, lockNext): the lock's frames.
int re4_lock_frames(int game)
{
    return game > kFrames ? game : kFrames;
}

// Patch 0932 (pl_wep.cpp PlSetLockPitch, the auto aim's start, patch 0174): the point the start pitch is measured
// from, Leon's chest (his lock part, as a Ganado's is its own) instead of his gun hand: at the aim's start the hand
// still hangs at his hip, and 1.5 m away a Ganado's chest lay 13 degrees up from there (DevJunior, 19.11.2026: "the
// reticle at times goes above the enemies").
void* re4_lock_aim_origin(void* player)
{
    cPlayer* pl = (cPlayer*) player;
    return &pl->getPartsPtr(pl->lockParts)->world;
}

// Patch 0932 (PlWepAutoTrack, the auto aim following its target): the pitch step (blend units, the caller clamps it
// to +-0.05 as before). Aiming a gun (the set state, the laser up) it follows the laser itself: the weapon's marker
// line (cObjWep::getMarkerPos) against the line from its muzzle to the lock point, half of the difference a frame
// (blend 1 = 45 degrees, as the game's step and patch 0174 count it; measured on the gun itself the count does not
// matter much). Else (the gun coming up, the knife) from his chest. The aim's centre (cPlWep::pitch) goes with it:
// the game left it at the start pitch, and once the lock's frames ran out PlWepLockRand's sway pulled the aim back
// there.
float re4_lock_track_pitch(void* player, void* target, float game)
{
    cPlayer* pl = (cPlayer*) player;
    const Vec* tgt = (const Vec*) target;
    if (!pl || !tgt || pl != pPL || !pl->Wep) {
        return game;
    }
    const f32 now = m3r;
    f32 e;
    cObjWep* gun = pl->Wep->m_pWep;
    const bool laser = gun && pl->r_no_0 == 0 && pl->r_no_1 == 6 && pl->r_no_2 == 1 && pl->r_no_3 != 0
        && pG->weapon_no != 9 && pG->weapon_no != 10;
    if (laser) {
        Vec lpos;
        Vec lcross;
        gun->getMarkerPos(&lpos, &lcross);
        const f32 ax = lcross.x - lpos.x, ay = lcross.y - lpos.y, az = lcross.z - lpos.z;
        const f32 tx = tgt->x - lpos.x, ty = tgt->y - lpos.y, tz = tgt->z - lpos.z;
        const f32 aFlat = sqrtf(ax * ax + az * az), tFlat = sqrtf(tx * tx + tz * tz);
        if (aFlat < 1.0f || tFlat < 100.0f) {
            return 0.0f;  // straight up or down, or the target at the muzzle: hold
        }
        const f32 err = atan2f(ty, tFlat) - atan2f(ay, aFlat);
        e = err * (4.0f / kPiF) * 0.5f;
    } else {
        Vec& from = pl->getPartsPtr(pl->lockParts)->world;
        const f32 dx = tgt->x - from.x, dy = tgt->y - from.y, dz = tgt->z - from.z;
        e = atan2f(dy, sqrtf(dx * dx + dz * dz)) * (4.0f / kPiF) - now;
    }
    e = e > 0.05f ? 0.05f : e < -0.05f ? -0.05f : e;
    f32 next = now + e;
    next = next > 1.0f ? 1.0f : next < -1.0f ? -1.0f : next;
    pl->Wep->pitch = next * (kPiF / 2.0f);
    static bool said = false;
    if (!said && laser) {
        said = true;
        re4_log("aim: the auto aim follows its laser to the lock point (pitch now %.1f degrees, step %.3f; the "
                "game's step %.3f)", (double) (now * 45.0f), (double) e, (double) game);
    }
    return e;
}

}  // extern "C"
