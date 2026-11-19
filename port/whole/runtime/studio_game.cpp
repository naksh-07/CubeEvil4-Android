// CubeEvil4 Studio, step S3 (Mods, 19.11.2026; port/docs/studio-plan.md; DevJunior: "it should go in directly, only
// choose the character and start working"): the Studio's icon (MainActivity.studioLaunch) leaves the file
// <data>/studio/open-viewer and starts the game into a room (a new game in r100 by the test script's way); once
// Leon stands free in a room (no event, no sub screen for kFreeFrames) this opens the debug menu with the motion
// viewer queued (DbMenuSetExecTool: its init picks it, its move starts it at once; patch 0061 asks every game frame).
// The file goes when the viewer opens: one opening per press of the icon (a game already running takes it too).
// Compiled with the game's flags into libre4_main (re4_gamehooks_obj).
#include "types.h"
#include "global.h"
#include "main.h"
#include "re4_runtime.h"

#include <stdio.h>
#include <string>
#include <unistd.h>

void DbMenuSetExecTool(const char* name);  // game/db_menu.cpp

namespace {

const int kFreeFrames = 90;
const int kCheckEvery = 30;
int gFree;
int gTick;

}  // namespace

extern "C" {

// Patch 0061 (game.cpp gameDebug, every game frame): 1 opens the debug menu now (the motion viewer queued)
int re4_studio_frame(void)
{
    if (!pG) {
        return 0;
    }
    const bool free = pG->Rno0 == 3 && !StaFlagChk(pG, STA_EVENT) && !StaFlagChk(pG, STA_SUB_SCRN) &&
                      !DbgFlagChk(pG, DBG_TEST_MODE);
    gFree = free ? gFree + 1 : 0;
    if (gFree < kFreeFrames || ++gTick % kCheckEvery) {
        return 0;
    }
    const std::string marker = std::string(re4_data_dir()) + "/studio/open-viewer";
    if (access(marker.c_str(), F_OK) != 0) {
        return 0;
    }
    unlink(marker.c_str());
    if (re4_disc_retail()) {
        re4_log("studio: the motion viewer needs the debug disc (its tools); not opened");
        return 0;
    }
    DbMenuSetExecTool("MOT VIEWER");
    re4_log("studio: the motion viewer opens (room r%03x)", (unsigned) pG->room_id);
    gFree = 0;
    return 1;
}

}  // extern "C"
