#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Configure bio4bgm.xwb path and enable/disable Remake soundtrack mode.
void re4_remake_audio_configure(int enabled, const char* xwbPath);

// Called from game frame hook (sound_volume.cpp) to report active BGM stream (-1 if none).
void re4_remake_audio_set_active(int trackIndex, int streamVol, int musicPercent, int paused);

// Returns 1 if a Remake XWB track is currently loaded and playing (so GameCube BGM should be muted).
int re4_remake_audio_is_playing(void);

// Mixes active Remake XWB track (resampled 48kHz -> 32kHz) into CoreAudio's output buffer.
void re4_remake_audio_mix(int16_t* out, int frames, int channels);

#ifdef __cplusplus
}
#endif
