// A whole-body skin's own hands (patch 0937, pl_leon.cpp cPlLeon::setRightHand; wish of DevJunior, 19.11.2026: the
// GTA skin over Leon showed Leon's hand over its glove when he held a weapon). Compiled with the game's flags into
// libre4_main.
//
// The game draws the weapon's grip hand (type 1: Body->pWepHand, from the weapon's own file) in place of Leon's right
// hand. A skin made by CubeEvil4 Studio from another game (cpp/studio_dff.inc) has its hands in its body and folds
// Leon's hand models into one point, marking them with 0xF0 in byte 0x29 (cModelData::pad_29, 0 on every disc model;
// the head's blink factor of runtime/blink.cpp, 5..40, lies elsewhere). With the player's file a mod's, Leon the
// player and his own right hand (archive 0x12) so marked, the grip hand is that folded one: nothing drawn.
#include "types.h"
#include "global.h"

extern "C" int re4_player_modded(void);  // runtime/dvd.cpp

extern "C" void* re4_skin_hand(void* wep)
{
    if (!pG || pG->pl_type != 0 || !re4_player_modded() || !pG->pPlayer) {
        return wep;
    }
    void* own = PL_ARC_PTR(pG->pPlayer, 0x12);
    return own && ((const u8*) own)[0x29] == 0xF0 ? own : wep;
}
