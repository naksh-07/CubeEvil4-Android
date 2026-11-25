// The app menu's AUDIO page (wish of the project owner, 30.10.2026): the volume of the music and of the
// sound effects, 0 .. 100 % each. Compiled with the game's flags into libre4_main; the app sends both at
// its start and at every change (re4_core_set_sound_volume).
//
// The sound driver works out each sound's output volume from its system volumes (Snd_ctrl_work) and the
// sound's own volume: Snd_axv_work_calc_ax_vol (the sound effects of the I.S.S.), Snd_seq_work_calc_ax_vol
// (sequences: type 2 effects, else music) and Snd_str_work_calc_ax_vol (streams: type 2 effects, else
// music, the room BGM). Patch 0153 scales the result by re4_sound_volume(): the game's own volumes stay as
// they are (its mute and fades work as before). After a change the driver recomputes every playing sound at
// the next game frame (Snd_reset_vol_all, a frame hook on the game thread). The movies' ADX sound is not
// part of the driver and keeps its volume.
//
// For tests on the PC: RE4_SOUND_VOLUME=music,effects (percent) at the start.
#include "types.h"
#include "snd_drv.h"
#include "remake_audio.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp

namespace {

std::atomic<int> gMusic(100);       // percent
std::atomic<int> gEffects(100);     // percent
std::atomic<int> gModMusic(100);    // percent: the music mod's tracks (Mods, 19.11.2026; runtime/music.cpp)
std::atomic<bool> gPending(false);  // recompute the playing sounds at the next game frame
std::atomic<bool> gHooked(false);
std::atomic<bool> gMuted(false);  // photo mode (runtime/photo_mode.cpp, 1.1.0): everything silent while it is on
int gLastRemakePlaying = 0;

void hook(unsigned)
{
    int activeTrack = -1;
    int activeVol = 127;
    if (Snd_str_blk[0].rit_adrs != nullptr && Snd_str_blk[0].rit_num > 0) {
        const SND_RIT* base = Snd_str_blk[0].rit_adrs;
        const SND_RIT* end = base + Snd_str_blk[0].rit_num;
        for (int i = 0; i < SND_STR_MAX; ++i) {
            const SND_STR& s = Snd_str_work[i];
            if ((s.be_flag & 0x10) && !(s.be_flag & 0x4000) &&
                s.rit_adrs >= base && s.rit_adrs < end) {
                activeTrack = static_cast<int>(s.rit_adrs - base);
                activeVol = s.vol;
                break;
            }
        }
    }
    const int musicPct = gMuted.load(std::memory_order_relaxed) ? 0 : gMusic.load(std::memory_order_relaxed);
    re4_remake_audio_set_active(activeTrack, activeVol, musicPct, 0);
    const int remakePlaying = re4_remake_audio_is_playing();
    if (remakePlaying != gLastRemakePlaying) {
        gLastRemakePlaying = remakePlaying;
        gPending.store(true);
    }
    if (gPending.exchange(false)) {
        Snd_reset_vol_all();
    }
}

int clampPercent(int v)
{
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

// RE4_SOUND_VOLUME at the start (the frame hook is not needed: nothing plays yet)
struct StartSetting {
    StartSetting()
    {
        int music = 100, effects = 100;
        if (const char* v = getenv("RE4_SOUND_VOLUME")) {
            if (sscanf(v, "%d,%d", &music, &effects) == 2) {
                gMusic = clampPercent(music);
                gEffects = clampPercent(effects);
            }
        }
    }
} gStartSetting;

}  // namespace

extern "C" {

// The app (core_api.cpp re4_core_set_sound_volume). Any thread.
void re4_sound_volume_set(int music, int effects)
{
    music = clampPercent(music);
    effects = clampPercent(effects);
    const bool changed = gMusic.exchange(music) != music;
    const bool changed2 = gEffects.exchange(effects) != effects;
    if (changed || changed2) {
        re4_log("sound: music %d %%, sound effects %d %%", music, effects);
    }
    if (!gHooked.exchange(true)) {
        re4_add_frame_hook(hook);
    }
    gPending.store(true);
}

// The app's "Mod music" slider (Mods, 19.11.2026: DevJunior "that its volume can be controlled"). Any thread.
void re4_mod_music_volume_set(int percent)
{
    percent = clampPercent(percent);
    if (gModMusic.exchange(percent) != percent) {
        re4_log("sound: mod music %d %%", percent);
    }
}

// Patch 0153: the factor for a sound's output volume, music (1) or a sound effect (0), as percent 0..100;
// 2: the music mod's own volume (runtime/music.cpp)
int re4_sound_volume(int music)
{
    if (gMuted.load(std::memory_order_relaxed)) {
        return 0;
    }
    if (music == 2) {
        return gModMusic.load(std::memory_order_relaxed);
    }
    if (music == 1 && re4_remake_audio_is_playing()) {
        return 0;
    }
    return (music ? gMusic : gEffects).load(std::memory_order_relaxed);
}

// Photo mode (1.1.0): the music and the sounds that go on (the effects are paused, SndSePauseAll) silent while it is
// on; the playing sounds are recomputed at the next game frame. The game thread.
void re4_sound_volume_mute(int on)
{
    gMuted.store(on != 0);
    if (!gHooked.exchange(true)) {
        re4_add_frame_hook(hook);
    }
    gPending.store(true);
}

}  // extern "C"
