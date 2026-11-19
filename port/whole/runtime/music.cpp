// The music mod's choice (wish of DevJunior, 19.11.2026: "change the battle and the exploration songs, closer to the
// remake"; his choices: battle and exploration music, chosen by zone and situation, his own OGG / MP3 files): each
// game frame this picks a pack's track (runtime/mod_music.cpp plays it) and lets the game's music sequences give way
// as much as the track sounds (patch 0929, re4_music_duck). Without a music folder in the mods nothing changes.
//
// - The zone: the stage of the room (pG->room_id >> 8): 1 village, 2 castle, 3 island, others "other".
// - Battle: an enemy within kNear of Leon for kBattleIn frames; it lasts until none has been within kFar for
//   kBattleOut frames. Enemies: ids 0x10..0x3F, alive, active; not the merchant (0x18), the chickens (0x28) and the
//   truck / mine carts (0x3B). The track: <zone>_battle, else battle.
// - Exploration: in a room where an enemy was seen since it was entered (the save rooms, the merchant's and the
//   rooms cleared before keep the game's music), while the game itself plays room music (a BGM slot running and
//   audible): <zone>_explore, else explore. A room the game keeps silent stays silent.
// - Nothing (the game's own music back) during cutscenes and movies (STA_EVENT, STA_MOVIE_ON), Leon's death and the
//   sub screens' fade (STA_DIEDEMO, STA_SUSPEND), and when the player is not Leon.
// The gain: the app's music volume (AUDIO page, re4_sound_volume) times kGain.
#include "types.h"
#include "global.h"
#include "main.h"
#include "re4_runtime.h"
#include "player.h"
#include "em.h"
#include "snd.h"
#include "snd_drv.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" int re4_sound_volume(int music);                   // runtime/sound_volume.cpp
extern cPlayer* pPL;

namespace {

const f32 kNear = 12000.0f;   // mm
const f32 kFar = 20000.0f;
const int kBattleIn = 15;     // frames (half a second)
const int kBattleOut = 240;   // frames (eight seconds)
const f32 kGain = 0.85f;
// (Mods, 19.11.2026) a short break of the conditions keeps the track: the prompts of the kicks and finishers, a menu
// opened a moment (DevJunior's log: the battle track off and on again every few seconds during a fight)
const int kHold = 60;         // frames (two seconds)

std::atomic<int> gDuck(256);
int gRoom = -1;
bool gHostile;
bool gBattle;
int gNearFrames, gCalmFrames;
int gTrack = -1;
int gBlocked;

bool threat(const cEm* m)
{
    const int id = m->id;
    return id >= 0x10 && id < 0x40 && id != 0x18 && id != 0x28 && id != 0x3B && m->hp > 0;
}

const char* zone(int room)
{
    switch (room >> 8) {
    case 1:
        return "village";
    case 2:
        return "castle";
    case 3:
        return "island";
    default:
        return "other";
    }
}

// <zone>_<what>, else <what>; -1 none
int track(const char* what)
{
    char name[64];
    snprintf(name, sizeof(name), "%s_%s", zone(pG->room_id), what);
    const int t = re4_mod_music_find(name);
    return t >= 0 ? t : re4_mod_music_find(what);
}

// the game plays room music: a BGM slot running at an audible volume
bool gameMusic()
{
    if (!pSnd) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        const BGM_STAT& w = pSnd->bgm_state[i];
        if (w.busy == 1 && w.stop_req == 0 && w.now_play_vol > 4) {
            return true;
        }
    }
    return false;
}

void frame(unsigned)
{
    if (re4_mod_music_count() == 0) {
        return;
    }
    int want = -1;
    const bool hard = !pG || !pPL || pG->pl_type != 0 || StaFlagChk(pG, STA_MOVIE_ON) || StaFlagChk(pG, STA_DIEDEMO);
    const bool soft = !hard && (StaFlagChk(pG, STA_EVENT) || StaFlagChk(pG, STA_SUSPEND));
    gBlocked = hard || soft ? gBlocked + 1 : 0;
    if (soft && gTrack >= 0 && gBlocked < kHold) {
        want = gTrack;  // a moment's break: the track plays on
    } else if (!hard && !soft) {
        if (pG->room_id != gRoom) {
            gRoom = pG->room_id;
            gHostile = false;
            gBattle = false;
            gNearFrames = gCalmFrames = 0;
        }
        bool near = false, far = false;
        int nearId = 0;
        for (cEm* m = EmMgr.getActiveWork(); m; m = EmMgr.getNext(m)) {
            if (m == (cEm*) pPL || !threat(m)) {
                continue;
            }
            gHostile = true;
            const f32 dx = m->pos.x - pPL->pos.x, dy = m->pos.y - pPL->pos.y, dz = m->pos.z - pPL->pos.z;
            const f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < kNear && !near) {
                near = true;
                nearId = m->id;
            }
            far = far || d < kFar;
        }
        gNearFrames = near ? gNearFrames + 1 : 0;
        if (!gBattle && gNearFrames >= kBattleIn) {
            gBattle = true;
            gCalmFrames = 0;
            re4_log("music: battle (enemy id 0x%x within %.0f m, room %03x)", nearId, (double) (kNear / 1000.0f),
                    (unsigned) pG->room_id);
        }
        if (gBattle) {
            gCalmFrames = far ? 0 : gCalmFrames + 1;
            if (gCalmFrames >= kBattleOut) {
                gBattle = false;
                re4_log("music: calm again (room %03x)", (unsigned) pG->room_id);
            }
        }
        if (gBattle) {
            want = track("battle");
        }
        if (want < 0 && gHostile && gameMusic()) {
            want = track("explore");
        }
    }
    if (want != gTrack) {
        gTrack = want;
        re4_log("music: track %d (room %03x)", want, pG ? (unsigned) pG->room_id : 0u);
    }
    re4_mod_music_want(want, (int) (256.0f * kGain * (f32) re4_sound_volume(1) / 100.0f));
    // the game's music gives way as much as the mod's sounds (both fade over the mixer's crossfade)
    const int duck = 256 - re4_mod_music_level();
    const int clamped = duck < 0 ? 0 : duck > 256 ? 256 : duck;
    if (abs(clamped - gDuck.load()) >= 8 || (clamped != gDuck.load() && (clamped == 0 || clamped == 256))) {
        gDuck.store(clamped);
        Snd_reset_vol_all();
    }
}

}  // namespace

extern "C" {

// Patch 0929 (snd_seq3.cpp Snd_seq_work_calc_ax_vol): the game's music sequences' share, 0..256. Its first call (the
// first music the game starts) hooks the choice into the frames.
int re4_music_duck(void)
{
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        re4_add_frame_hook(frame);
    }
    return gDuck.load();
}

}  // extern "C"
