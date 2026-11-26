// Native Android touch context (touch_context.cpp): packs per-frame player, weapon,
// action prompt, QTE, Ashley, and scope state into an atomic uint64_t polled by the
// Android shell (NativeTouchOverlay) via re4_core_touch_context().
//
// Compiled with game flags into libre4_main (re4_gamehooks_obj). cActionButton::move()
// (patch 0954 in src/game/act_btn.cpp) notifies us each frame via re4_touch_act_clear()
// and re4_touch_act_set(kind, btn, flags) so we get both ACTION_TYPE (Kick, Suplex,
// Open, Climb, Pickup, Examine, Save) and DISP_FLAG (A, A_RAPID, L+R, A+B, etc.).
#include "types.h"
#include "global.h"
#include "main.h"
#include "player.h"
#include "pl_sub.h"
#include "pl_npc.h"
#include "item.h"
#include "act_btn.h"
#include "cockpit.h"
#include "re4_mem1_map.h"

#include <atomic>
#include <stdint.h>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;

extern "C" void re4_add_frame_hook(void (*hook)(unsigned));
extern "C" int re4_valid_port_ptr(const void* p);
extern "C" unsigned short re4_qte_buttons(int disp);
extern "C" int re4_quick_slots_on(void);
extern "C" int re4_movie_playing(void);

namespace {

// Packed layout (64 bits):
//   bit 0:      inGame        (pG && pG->Rno0 == 3)
//   bit 1:      inSubscreen   (!inGame || STA_SUB_SCRN || SYS_TYPEWRITER || STA_INTO_SHOP)
//   bit 2:      inCutscene    (STA_EVENT || STA_DIEDEMO)
//   bit 3:      isAimingGun   (pPL && pPL->isKamae())
//   bit 4:      isAimingKnife (pPL knife ready/slash routine)
//   bit 5:      inScopeOrBino (STA_SCOPE_CAMERA || F_BINOCULAR)
//   bit 6:      canReload     (ItemMgr.reloadable() != 0)
//   bit 7:      ashleyPresent (SubCharCheckCtrl() != 0)
//   bit 8:      actBtnActive  (visible action/QTE prompt this frame)
//   bit 9:      isQte         (re4_qte_buttons(actDisp) != 0)
//   bit 10:     hasWeapon     (pG->weapon_no != 0 && pG->weapon_no != 0x10)
//   bits 11..15: actDisp      (5 bits: DISP_FLAG 0..16)
//   bits 16..23: actKind      (8 bits: ACTION_TYPE 0..65)
//   bits 24..31: weaponNo     (8 bits: pG->weapon_no)
//   bits 32..39: ammoCount    (8 bits: min(255, ItemMgr.bulletNum()))
//   bit 40:     hasQuickSlots (re4_quick_slots_on() != 0)
//   bit 41:     isMerchant    (STA_INTO_SHOP)
//   bit 42:     inTypewriter  (SYS_TYPEWRITER)
//   bit 43:     inDiedemo     (STA_DIEDEMO)

std::atomic<uint64_t> gTouchContext{0};
std::atomic<int> gActActive{0};
std::atomic<int> gActKind{0};
std::atomic<int> gActBtn{0};
std::atomic<int> gActFlags{0};

bool alignedFor(const void* p, size_t align)
{
    return p != 0 && (((unsigned long) p) & (align - 1UL)) == 0;
}

bool liveByte(const void* p)
{
    const unsigned long a = (unsigned long) p;
    return a >= 0x80000000ul && (a < RE4_MEM1_END || re4_valid_port_ptr(p) != 0);
}

template <class T>
bool liveStruct(const T* p)
{
    if (!alignedFor(p, alignof(T))) {
        return false;
    }
    const char* c = (const char*) p;
    return liveByte(c) && liveByte(c + sizeof(T) - 1);
}

void touchContextHook(unsigned)
{
    if (!liveStruct(pG)) {
        gTouchContext.store(0, std::memory_order_relaxed);
        return;
    }

    const bool inGame = (pG->Rno0 == 3);
    if (!inGame) {
        gTouchContext.store(0, std::memory_order_relaxed);
        return;
    }

    const bool inSub = StaFlagChk(pG, STA_SUB_SCRN) || SysFlagChk(pG, SYS_TYPEWRITER)
        || StaFlagChk(pG, STA_INTO_SHOP);
    const bool inTypewriter = SysFlagChk(pG, SYS_TYPEWRITER) != 0;
    const bool inDiedemo = StaFlagChk(pG, STA_DIEDEMO) != 0;
    const bool inMovie = (re4_movie_playing() != 0);
    const bool inCutscene = StaFlagChk(pG, STA_EVENT) || inDiedemo || inMovie;
    const bool isMerchant = StaFlagChk(pG, STA_INTO_SHOP) != 0;

    bool isAimGun = false;
    bool isAimKnife = false;
    bool inScope = StaFlagChk(pG, STA_SCOPE_CAMERA) != 0;
    const int wepNo = (int) pG->weapon_no & 0xFF;

    if (liveStruct(pPL)) {
        isAimKnife = pPL->r_no_0 == 0
            && (pPL->r_no_1 == 0xB || (pPL->r_no_1 == 6 && wepNo == 0x10));
        isAimGun = !isAimKnife && (pPL->isKamae() != 0);
        if (pPL->stat.check(cPlayer::F_BINOCULAR)) {
            inScope = true;
        }
    }

    cItem* wep = ItemMgr.weapon();
    const bool validWep = liveStruct(wep);
    const bool hasWeapon = (wepNo != 0 && wepNo != 0x10 && validWep);
    const bool canReload = !inSub && validWep && (ItemMgr.reloadable() != 0);
    cSubChar* sub = SUB_CHAR();
    const bool ashleyPresent = !inSub && liveStruct(sub) && sub->id == 3
        && (SubCharCheckCtrl() != 0);
    const u16 rawAmmo = validWep ? ItemMgr.bulletNum() : 0;
    const uint64_t ammoClamped = rawAmmo > 255 ? 255ULL : (uint64_t) rawAmmo;

    const u8 dispShown = *reinterpret_cast<const u8*>(&Cckpt.m_ActBttn);
    const bool actActive = !inSub && !inMovie && (dispShown != DISP_OFF)
        && (gActActive.load(std::memory_order_relaxed) != 0);
    const uint64_t actDisp = actActive ? ((uint64_t) dispShown & 0x1FULL) : 0ULL;
    const uint64_t actKind = actActive ? ((uint64_t) gActKind.load(std::memory_order_relaxed) & 0xFFULL) : 0ULL;
    const bool isQte = actActive && (re4_qte_buttons((int) actDisp) != 0);
    const bool hasQuickSlots = re4_quick_slots_on() != 0;

    uint64_t bits = 0;
    bits |= 1ULL << 0;
    if (inSub) bits |= 1ULL << 1;
    if (inCutscene) bits |= 1ULL << 2;
    if (isAimGun) bits |= 1ULL << 3;
    if (isAimKnife) bits |= 1ULL << 4;
    if (inScope) bits |= 1ULL << 5;
    if (canReload) bits |= 1ULL << 6;
    if (ashleyPresent) bits |= 1ULL << 7;
    if (actActive) bits |= 1ULL << 8;
    if (isQte) bits |= 1ULL << 9;
    if (hasWeapon) bits |= 1ULL << 10;
    bits |= actDisp << 11;
    bits |= actKind << 16;
    bits |= ((uint64_t) wepNo) << 24;
    bits |= ammoClamped << 32;
    if (hasQuickSlots) bits |= 1ULL << 40;
    if (isMerchant) bits |= 1ULL << 41;
    if (inTypewriter) bits |= 1ULL << 42;
    if (inDiedemo) bits |= 1ULL << 43;

    gTouchContext.store(bits, std::memory_order_relaxed);
}

struct TouchContextInstall {
    TouchContextInstall() { re4_add_frame_hook(touchContextHook); }
} gTouchContextInstall;

}  // namespace

extern "C" void re4_touch_act_clear(void)
{
    gActActive.store(0, std::memory_order_relaxed);
}

extern "C" void re4_touch_act_set(int kind, int btn, int flags)
{
    gActKind.store(kind, std::memory_order_relaxed);
    gActBtn.store(btn, std::memory_order_relaxed);
    gActFlags.store(flags, std::memory_order_relaxed);
    gActActive.store(1, std::memory_order_relaxed);
}

extern "C" uint64_t re4_core_touch_context(void)
{
    return gTouchContext.load(std::memory_order_relaxed);
}
