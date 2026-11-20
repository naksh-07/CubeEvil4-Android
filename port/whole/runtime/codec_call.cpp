// The radio calls of the 2023 remake (wish of DevJunior, 19.11.2026: "the call system of the remake, a small box at
// the side while you move, talking with Hunnigan, with the hand-at-the-ear animation"; his choices: option A, the
// voice and the subtitles in a corner without pausing, and every call, the story's too, "but Leon only walks, with
// his hand at his ear, as in the remake"). Compiled with the game's flags into libre4_main (re4_gamehooks_obj); on
// with the app's "Remake movement" (re4_move_on, runtime/locomotion.cpp), like the quick pick-up.
//
//  - Patch 0939 (sscrn.cpp OpeSetOpenTerm, the scenario's call: once the player is free): re4_codec_call plays the
//    call itself in the scenario's task, the game going on. The game's own steps where they matter: the call marked
//    heard (OpeMdtSetInit: pG->ope_mdt_bits, ope_mdt_no), the radio's tone (SndStrPlayBlock, strTbl), then the
//    call's voice stream (SndStrReq 0x8C + call, as SsTermMain::OpeMesMove) and its subtitles on their frames
//    (the call's OpeMesSeq table and messages from op/op<stage>.das, the entries OpeMesTblInit takes, MesData
//    type 2 meanwhile; shown where the events show theirs). No sub screen, no event, Leon not moved (the calls
//    that place him somewhere first leave him where he is). When the scenario's task goes on, the call is over,
//    as after the codec screen. Not handled (the game's codec screen as before): another character than Leon,
//    a call number out of the table, the data not readable.
//  - Meanwhile Leon walks only: re4_run_held (aim_options.cpp, the run checks of patch 0156 and free_move) says
//    "not held"; the frame hook takes A, B, X, Y, Z, L, R, START and the D-pad from the pad (re4_pad_block) while he
//    is free in the field (not caught, no event: quick-time events get their keys). The stick and the C stick stay.
//  - His left hand at his ear (patch 0940, player.cpp cPlayer::move after moveMatCalcBefore, before the parts'
//    world matrices): the upper arm and the forearm (parts 13, 14; the hand 16) reach with two bones to a point
//    beside and under his left ear (the head part 4), the elbow out to the side, blended in over kArmIn frames and
//    out at the end; only while he is free in the field.
//  - START skips the call (the frame hook sees its press in the pad's raw buttons: re4_pad_raw; the game does not get
//    it meanwhile): the voice stops, the subtitles go, the scenario goes on as after the call.
//  - Nothing else starts meanwhile (DevJunior, 19.11.2026: "control how far the player gets ... and that in a room no
//    enemy appears until it is over"): patch 0941 holds the areas that start something (re4_codec_holds_area: they
//    fire after the call if Leon still stands in them), patch 0942 lets a room script's enemy wait for the call's end
//    (re4_codec_spawn_wait). Doors need A, which is taken anyway.
//  - The app's box (core_bridge.cpp: Re4CoreSnapshot::codec = the call + 1; core_mode.cpp draws it at the top left).
//  - The scenario's task ended by the game (room change, game over): the frame hook ends the call (stream, message,
//    MesData, the data freed if the room is the same: the room heap 4 it came from is replaced otherwise).
//  - Each step is logged ("codec: ...").
#include "types.h"
#include "global.h"
#include "main.h"
#include "player.h"
#include "model.h"
#include "sscrn.h"
#include "mes.h"
#include "snd.h"
#include "event.h"
#include "dvd.h"
#include "main_mem.h"
#include "sce.h"
#include "sce_sys.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_pad_block(u16 buttons);                  // runtime/pad.cpp
extern "C" u16 re4_pad_raw(void);                             // runtime/pad.cpp
extern "C" int re4_move_on(void);                            // runtime/locomotion.cpp
extern cPlayer* pPL;

namespace {

// SsTermMain::OpeMesSeq (ss_main.h), converted by formats_arc.cpp convertOpeMesSeq
struct Seq {
    u16 be_flag;
    s16 no;
    int frame;  // the frame the entry fires at
    int mes;    // message (-1: wait for the current one)
    int timer;  // frames it stays; -1 ends the call
};

const int kArmIn = 10;           // frames the hand takes to the ear and back
const int kRingFrames = 20;      // the radio's tone before the voice (OpeSetOpenTerm: 21 frames to the radio)
const int kStreamWait = 240;     // frames the voice may take to be ready
const int kMaxFrames = 60 * 60 * 6;  // a call's longest (6 minutes)
const u16 kBlocked = 0x1F7F;     // START, Y, X, B, A, L, R, Z, the D-pad

std::atomic<int> gCall{-1};  // the call playing, -1 none
std::atomic<bool> gHooked{false};
u32 gTick;        // pG->Frame_cnt at the call's last step
u16 gRoom;
void* gOp;        // op/op<stage>.das (room heap)
u8* gMesBak;      // MesData type 2 before the call
u32 gStr;         // the voice stream (SndStrReq)
int gRing = -1;   // the radio's tone (SndStrPlayBlock)
bool gArmWanted;  // the hand to the ear
f32 gArmW;        // its blend
std::atomic<bool> gSkip{false};  // START during the call (the frame hook)
u16 gRawPrev;     // the pad's raw buttons at the last frame (START's press)

bool inField(cPlayer* pl)
{
    return pl && pl == pPL && pl->r_no_0 == 0 && (pl->r_no_1 <= 4 || pl->r_no_1 == 6 || pl->r_no_1 == 0xB) &&
           !StaFlagChk(pG, STA_SUB_SCRN) && !StaFlagChk(pG, STA_EVENT);
}

void finish(bool sameRoom, const char* why)
{
    if (gStr) {
        SndStrReq(gStr, 8, 0, 0);
        gStr = 0;
    }
    if (gRing >= 0) {
        SndStrStopBlock(gRing);
        gRing = -1;
    }
    cMes.Delete(0);
    if (gMesBak) {
        MesData.registData(2, gMesBak);
        gMesBak = 0;
    }
    if (gOp && sameRoom) {
        Mem_free(gOp);
    }
    gOp = 0;
    gArmWanted = false;
    re4_log("codec: call %d %s", gCall.load(), why);
    gCall = -1;
}

void codecFrame(unsigned)
{
    if (gCall.load() < 0 || !pG) {
        return;
    }
    // the scenario's task gone (room change, game over, continue)
    if (G_ROOM_ID != gRoom || pG->Rno0 != 3) {
        finish(false, "ended by the game (room change or game over)");
        return;
    }
    if (!StaFlagChk(pG, STA_SUB_SCRN) && pG->Frame_cnt - gTick > 600) {
        finish(true, "ended: its task stopped");
        return;
    }
    // START skips the call (DevJunior, 19.11.2026: "the ability to skip that talk with a button"): its press while the
    // game does not get it
    const u16 raw = re4_pad_raw();
    const bool startPressed = (raw & 0x1000) && !(gRawPrev & 0x1000);
    gRawPrev = raw;
    if (inField(pPL)) {
        if (startPressed && !gSkip.exchange(true)) {
            re4_log("codec: call %d skipped (START)", gCall.load());
        }
        re4_pad_block(kBlocked);
    }
}

// a frame of the call: the task sleeps, the frame counted
void step()
{
    gTick = pG->Frame_cnt;
    SceSleep(1);
    gTick = pG->Frame_cnt;
}

void show(int mes)
{
    if (StaFlagChk(pG, STA_EVENT) || StaFlagChk(pG, STA_SUB_SCRN)) {
        return;  // an event's or a screen's own messages
    }
    cMes.Delete(0);
    // where the events show their subtitles (event.cpp: x 0x64, over line 0x150)
    const int y = 0x150 - cMes.getLineGap(0) - cMes.getFontHeight(0) - 1;
    cMes.MesSet(mes, 0x64, y, 0x03000054, 0, 0, 4);  // SsTermMain::OpeMesSet's attributes: no stop, kept shown
}

// ---- the hand at the ear: a two-bone reach (as runtime/ashley_gun.cpp's, for Leon's left arm) ----

Vec v3(f32 x, f32 y, f32 z)
{
    Vec v = {x, y, z};
    return v;
}
Vec sub3(const Vec& a, const Vec& b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
Vec add3(const Vec& a, const Vec& b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
Vec mul3(const Vec& a, f32 k) { return v3(a.x * k, a.y * k, a.z * k); }
f32 dot3(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
f32 len3(const Vec& a) { return sqrtf(dot3(a, a)); }
Vec unit3(const Vec& a)
{
    const f32 l = len3(a);
    return l > 1e-6f ? mul3(a, 1.0f / l) : v3(0.0f, 0.0f, 1.0f);
}
struct M3 {
    f32 m[3][3];
};
M3 worldRot(const cCoord* c)
{
    M3 r;
    for (int k = 0; k < 3; k++) {
        const Vec col = unit3(v3(c->mat[0][k], c->mat[1][k], c->mat[2][k]));
        r.m[0][k] = col.x;
        r.m[1][k] = col.y;
        r.m[2][k] = col.z;
    }
    return r;
}
Vec applyT(const M3& r, const Vec& v)
{
    return v3(r.m[0][0] * v.x + r.m[1][0] * v.y + r.m[2][0] * v.z, r.m[0][1] * v.x + r.m[1][1] * v.y + r.m[2][1] * v.z,
              r.m[0][2] * v.x + r.m[1][2] * v.y + r.m[2][2] * v.z);
}
M3 mulM(const M3& a, const M3& b)
{
    M3 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}
M3 ofQuat(const Quaternion& q)
{
    Mtx t;
    PSMTXQuat(t, &q);
    M3 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = t[i][j];
        }
    }
    return r;
}
void quatFromTo(const Vec& a, const Vec& b, Quaternion* q)
{
    const f32 d = dot3(a, b);
    const Vec c = v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    const f32 s = sqrtf((1.0f + (d > -0.9999f ? d : -0.9999f)) * 2.0f);
    q->x = c.x / s;
    q->y = c.y / s;
    q->z = c.z / s;
    q->w = s * 0.5f;
}
Vec bindPos(const cParts* p)
{
    return v3(-p->lt_inv_mat[0][3], -p->lt_inv_mat[1][3], -p->lt_inv_mat[2][3]);
}
void turnLocal(cParts* p, const Quaternion& q, f32 w)
{
    Quaternion qa, r;
    C_QUATMtx(&qa, p->l_mat);
    C_QUATSlerp(&qa, (Quaternion*) &q, &r, w);
    Vec t = {p->l_mat[0][3], p->l_mat[1][3], p->l_mat[2][3]};
    PSMTXQuat(p->l_mat, &r);
    TransMatrix(p->l_mat, &t);
    ScaleMatrix(p->l_mat, &p->scale);
}

// the left arm (13 upper arm, 14 forearm, 16 hand) onto the point beside his left ear (4 the head)
void armToEar(cPlayer* pl, f32 w)
{
    cParts* u = pl->getPartsPtr(13);
    cParts* f = pl->getPartsPtr(14);
    cParts* h = pl->getPartsPtr(16);
    cParts* head = pl->getPartsPtr(4);
    cParts* ru = pl->getPartsPtr(7);
    if (!u || !f || !h || !head || !ru || !u->pParent) {
        return;
    }
    const Vec bu = bindPos(u), bf = bindPos(f), bh = bindPos(h);
    const Vec restUF = sub3(bf, bu), restFH = sub3(bh, bf);
    const f32 l1 = len3(restUF), l2 = len3(restFH);
    if (l1 < 1.0f || l2 < 1.0f) {
        return;
    }
    pl->partsWorldCalc();  // this frame's pose (the game computes it again after us)
    const Vec s = u->world;
    Vec left = sub3(s, ru->world);  // from the right shoulder to the left one
    left.y = 0.0f;
    left = unit3(left);
    const Vec fwd = v3(sinf(pl->ang.y), 0.0f, cosf(pl->ang.y));
    const Vec up = v3(0.0f, 1.0f, 0.0f);
    // the wrist under and a little in front of the ear: the fingers reach it
    const Vec g = add3(head->world, add3(add3(mul3(left, 95.0f), mul3(fwd, 35.0f)), mul3(up, -10.0f)));
    // the elbow out to the side, down and a little forward
    const Vec pole = add3(add3(left, mul3(up, -0.6f)), mul3(fwd, 0.35f));
    const Vec dv = sub3(g, s);
    f32 dist = len3(dv);
    const Vec dir = unit3(dv);
    if (dist > l1 + l2 - 1.0f) {
        dist = l1 + l2 - 1.0f;
    }
    if (dist < 1.0f) {
        dist = 1.0f;
    }
    const f32 along = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    const f32 hh = l1 * l1 - along * along;
    const Vec p = unit3(sub3(pole, mul3(dir, dot3(pole, dir))));
    const Vec e = add3(add3(s, mul3(dir, along)), mul3(p, hh > 0.0f ? sqrtf(hh) : 0.0f));
    const M3 par = worldRot(u->pParent);
    Quaternion qu, qf;
    quatFromTo(unit3(restUF), unit3(applyT(par, sub3(e, s))), &qu);
    const M3 wu = mulM(par, ofQuat(qu));
    quatFromTo(unit3(restFH), unit3(applyT(wu, sub3(g, e))), &qf);
    turnLocal(u, qu, w);
    turnLocal(f, qf, w);
}

}  // namespace

extern "C" {

// Patch 0939 (sscrn.cpp OpeSetOpenTerm, the scenario's task, the player free): 1 = the call was played here
int re4_codec_call(int no)
{
    static const int kRingTbl[24] = {3, 3, 0x33, 3, 0x33, 3, 3, 0x33, 3, 0x33, 0x33, 3,
                                     3, 3, 0x33, 3, 3, 3, 3, 0x33, 3, 3, 3, 3};  // OpeSetOpenTerm's strTbl
    if (!re4_move_on() || !pG || !pPL || pG->pl_type != 0 || no < 0 || no > 23 || gCall.load() >= 0) {
        return 0;
    }
    if (!gHooked.exchange(true)) {
        re4_add_frame_hook(codecFrame);
    }
    char name[32];
    sprintf(name, "op/op%02d.das", pG->stage_no);
    const int req = DvdReadN(name, 0, 0, 0, 0, 4, __FILE__, __LINE__);  // async, on the current (room) heap
    DVD_READINFO info;
    int ret, t = 0;
    while ((ret = Dvd.ReadCheck(req, &info)) != 1) {
        if (ret < 0 || ++t > 600) {
            re4_log("codec: call %d: %s not readable: the game's codec screen", no, name);
            return 0;
        }
        SceSleep(1);
    }
    void* op = (void*) (unsigned long) info.start_addr[0][0];  // below 4 GB on 64 bit too (the game's heap)
    if (!op || info.read_size[0][0] < 0x420) {
        re4_log("codec: call %d: %s is empty: the game's codec screen", no, name);
        return 0;
    }
    u32* arc = (u32*) ((u8*) op + 0x400);
    const int n = no <= 12 ? no : no <= 18 ? no - 13 : no - 19;
    const int mesSub = 4 + n, seqSub = no <= 12 ? 17 + n : no <= 18 ? 10 + n : 9 + n;
    if (arc[0] < (u32) (seqSub - 3)) {
        re4_log("codec: call %d: %s has %u entries: the game's codec screen", no, name, (unsigned) arc[0]);
        Mem_free(op);
        return 0;
    }
    u8* mes = (u8*) arc + arc[mesSub];
    const Seq* seq = (const Seq*) ((u8*) arc + arc[seqSub]);
    gOp = op;
    gRoom = G_ROOM_ID;
    gTick = pG->Frame_cnt;
    gCall = no;
    gSkip = false;
    gRawPrev = re4_pad_raw();
    SubScreenWork* wk = &SubScreenWk;
    wk->cancel = 0;
    wk->opeMdtNo = no;
    OpeMdtSetInit();  // heard
    gMesBak = MesData.m_Data[2];
    MesData.registData(2, mes);
    gArmWanted = true;
    re4_log("codec: call %d (stage %d, voice 0x%x) while Leon walks", no, (int) pG->stage_no, 0x8C + no);
    gRing = SndStrPlayBlock(1, kRingTbl[no], 0.0f);
    for (int i = 0; i < kRingFrames && gCall.load() == no && !gSkip.load(); i++) {
        step();
    }
    if (gCall.load() != no) {
        return 1;  // ended by the game meanwhile
    }
    if (gSkip.load()) {
        finish(true, "skipped before the voice");
        return 1;
    }
    SndStrStopBlock(gRing);
    gRing = -1;
    gStr = SndStrReq(1, 0x8C + no, 1, 0, 0, 0.0f);
    int wait = 0;
    while (gStr && SndStrStatusCk(gStr, 2) == 0 && wait < kStreamWait && gCall.load() == no && !gSkip.load()) {
        step();
        wait++;
    }
    if (gCall.load() != no) {
        return 1;
    }
    if (gSkip.load()) {
        finish(true, "skipped before the voice");
        return 1;
    }
    if (gStr && wait < kStreamWait) {
        SndStrReq(gStr, 2, 0, 0);
    } else {
        re4_log("codec: call %d: the voice was not ready after %d frames: subtitles only", no, wait);
    }
    int idx = 0, count = 0, left = 0, shown = 0;
    for (int frames = 0; frames < kMaxFrames && gCall.load() == no && !gSkip.load(); frames++) {
        if (left > 0 && --left == 0) {
            cMes.Delete(0);
        }
        bool end = false;
        while (idx < 512 && seq[idx].frame <= count) {
            if (seq[idx].timer == -1) {
                end = true;
                break;
            }
            if (seq[idx].mes >= 0) {
                show(seq[idx].mes);
                shown++;
            }
            left = seq[idx].timer;
            idx++;
        }
        if (end || idx >= 512) {
            break;
        }
        count++;
        step();
    }
    if (gCall.load() == no) {
        re4_log("codec: call %d: %d subtitles in %d frames", no, shown, count);
        finish(true, gSkip.load() ? "skipped" : "ended");
    }
    return 1;
}

// Patch 0941 (sce_at.cpp, the area check): the areas that start something wait while a call plays (DevJunior,
// 19.11.2026: "control how far the player gets, so that it does not clash with certain scenes"): doors (1), the
// scenario's functions (2), flags (4), messages (5), the plain ones (0, 6, 7, 0x14), use (0x11), position jumps
// (0x13). Items, saving, camera, damage, collision, field info, stooping, ladders and hiding go on.
int re4_codec_holds_area(u32 kind)
{
    if (gCall.load() < 0) {
        return 0;
    }
    switch (kind) {
    case 0:
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
    case 7:
    case 0x11:
    case 0x13:
    case 0x14:
        return 1;
    }
    return 0;
}

// Patch 0942 (em_set.cpp EmSetFromList2, a room script's enemy): no enemy appears while a call plays (DevJunior,
// 19.11.2026: "that in a room no enemy appears until it is over"); the scenario task that asks sleeps meanwhile
void re4_codec_spawn_wait(void)
{
    if (gCall.load() < 0 || !SceSys.checkCTaskRange()) {
        return;
    }
    re4_log("codec: an enemy of the room's script waits for the call's end");
    while (gCall.load() >= 0) {
        SceSleep(1);
    }
}

int re4_codec_active(void)  // core_bridge.cpp: Re4CoreSnapshot::codec
{
    return gCall.load() + 1;
}

int re4_codec_walk_only(void)  // aim_options.cpp re4_run_held
{
    return gCall.load() >= 0 && inField(pPL);
}

// Patch 0940 (player.cpp cPlayer::move, after moveMatCalcBefore, before partsWorldCalc; the game thread)
void re4_codec_arm(cPlayer* pl)
{
    if (!pl || pl != pPL || !pG || pG->pl_type != 0) {
        return;
    }
    const bool on = gArmWanted && gCall.load() >= 0 && inField(pl);
    gArmW += on ? 1.0f / kArmIn : -1.0f / kArmIn;
    gArmW = gArmW < 0.0f ? 0.0f : gArmW > 1.0f ? 1.0f : gArmW;
    if (gArmW > 0.0f) {
        const f32 w = gArmW * gArmW * (3.0f - 2.0f * gArmW);
        armToEar(pl, w);
    }
}

}  // extern "C"
