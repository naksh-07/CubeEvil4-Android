#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void re4_weapon_sfx_set_dir(const char* dir);
void re4_weapon_sfx_fire(int weaponNo, int effectsVolumePercent);
int re4_weapon_sfx_suppress(int blk2No);
void re4_weapon_sfx_mix(int16_t* lr, unsigned frames);

#ifdef __cplusplus
}
#endif
