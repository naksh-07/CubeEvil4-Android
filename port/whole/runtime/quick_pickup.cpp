// The remake's quick pick-up (patch 0938, game/sce_at.cpp sceAtFunc_item; wish of DevJunior, 19.11.2026: "the
// pick-ups, so you do not stop so much on your way"). Compiled with the game's flags into libre4_main. On with the
// app's "Remake movement" (runtime/locomotion.cpp): money, treasures and the ammunition and healing items the case takes by
// itself are taken without the zoom and the question, the game goes on. Each one is logged ("pick-up: ...").
#include "types.h"

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_move_on(void);  // runtime/locomotion.cpp

extern "C" int re4_quick_pickup_on(void)
{
    return re4_move_on();
}

extern "C" void re4_quick_pickup_log(int id, int num, int kind)
{
    re4_log("pick-up: item %#x x%d (%s) taken on the move", id, num,
            kind == 8 ? "money" : kind == 2 ? "ammunition" : kind == 5 || kind == 0xC ? "treasure" : kind == 3 ? "grenade"
            : kind == 6 ? "healing" : kind == 9 ? "weapon part" : "item");
}

// patch 0943 (game/cockpit.cpp BulletInfo::move): the reserve rounds beside the loaded ones in the HUD, as the remake
extern "C" int re4_ammo_reserve_on(void)
{
    return re4_move_on();
}
