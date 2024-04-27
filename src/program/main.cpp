#include "ExceptionHandler.h"
#include "al/Library/LiveActor/ActorPoseKeeper.h"
#include "al/Library/Math/MathRandomUtil.h"
#include "fs.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "helpers/InputHelper.h"
#include "helpers/PlayerHelper.h"
#include "imgui.h"
#include "imgui_backend/imgui_impl_nvn.hpp"
#include "imgui_internal.h"
#include "imgui_nvn.h"
#include "lib.hpp"
#include "logger/Logger.hpp"
#include "patches.hpp"

#include <basis/seadRawPrint.h>
#include <devenv/seadDebugFontMgrNvn.h>
#include <filedevice/nin/seadNinSDFileDeviceNin.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <gfx/seadTextWriter.h>
#include <gfx/seadViewport.h>
#include <heap/seadHeapMgr.h>
#include <prim/seadSafeString.h>
#include <resource/seadArchiveRes.h>
#include <resource/seadResourceMgr.h>

#include <al/Library/File/FileLoader.h>
#include <al/Library/File/FileUtil.h>

#include <game/GameData/GameDataFunction.h>
#include <game/HakoniwaSequence/HakoniwaSequence.h>
#include <game/StageScene/StageScene.h>
#include <game/System/Application.h>
#include <game/System/GameSystem.h>

#include "al/Library/Controller/JoyPadUtil.h"
#include "rs/util.hpp"
#include "sead/random/seadGlobalRandom.h"

namespace al {
    void setVelocityZero(al::LiveActor*);
}

#define IMGUI_ENABLED true
#define TOTAL_RAMDOM
#define PROGGRESS_BAR_MAX_HEIGHT 750
#define PROGRESS_COLOR IM_COL32(12, 60, 138, 255)
#define PROGGRESS_BAR_WIDTH 70
#define TIMER_WAIT_FRAMES 1800 // 30 secconds

// Copied from lunakit:
// https://github.com/Amethyst-szs/smo-lunakit/blob/256c08c2dc731863d9c026cda94e306951956585/src/helpers/PlayerHelper.cpp
void killPlayer(PlayerActorHakoniwa* mainPlayer) {
    GameDataFunction::killPlayer(GameDataHolderAccessor(mainPlayer));
    mainPlayer->startDemoPuppetable();
    al::setVelocityZero(mainPlayer);
    mainPlayer->mPlayerAnimator->endSubAnim();
    mainPlayer->mPlayerAnimator->startAnimDead();
}

bool progressMod = false;
int gravityDir = 0;
int nextGravityDir = -1;

using sead::Vector3f;

const Vector3f directions[] = {{0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};

int frameCounter = 0;

const char* getGravityDirName(int dir) {
    switch (dir) {
    case 0:
        return "Down";
    case 1:
        return "Up";
    case 2:
        return "North";
    case 3:
        return "South";
    case 4:
        return "East";
    case 5:
        return "West";
    default:
        return "Unknown";
    }
}

HOOK_DEFINE_TRAMPOLINE(ControlHook){
    static void Callback(StageScene * scene){progressMod = scene->mIsAlive && !scene->isPause();
if (nextGravityDir == -1) {
    nextGravityDir = al::getRandom(0, 6); // This doesn't include 6
}
if (progressMod) {
    auto player = (PlayerActorHakoniwa*)rs::getPlayerActor(scene);
    if (al::isPadHoldL(-1) && al::isPadTriggerPressLeftStick(-1)) {
        killPlayer(player);
    }
    if (frameCounter % TIMER_WAIT_FRAMES == 0) {
        gravityDir = nextGravityDir;
#ifndef TOTAL_RAMDOM
        do {
            nextGravityDir = al::getRandom(0, 6);
        } while (nextGravityDir == gravityDir);

#else
        nextGravityDir = al::getRandom(0, 6);
#endif // !TOTAL_RAMDOM
    }
    if (player) {
        al::setGravity(player, directions[gravityDir]);
    }

    frameCounter++;
}
Orig(scene);
}
}
;

HOOK_DEFINE_TRAMPOLINE(InitRandomHook){static void Callback(void* thisPtr){

    Orig(thisPtr);
sead::GlobalRandom::instance()->init();
}
}
;

void drawDebugWindow() {
    if (!progressMod)
        return;
    ImGuiWindowFlags windowFlags = 0;
    windowFlags |= ImGuiWindowFlags_NoBackground;
    windowFlags |= ImGuiWindowFlags_NoTitleBar;
    ImGui::Begin("Game Debug Window", nullptr, windowFlags);
    ImGui::SetWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_FirstUseEver);
    ImGui::SetWindowPos(ImVec2(0, 0));
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 windowPos = ImGui::GetWindowPos();
    auto drawList = ImGui::GetWindowDrawList();
    float rectLength =
        ((float)PROGGRESS_BAR_MAX_HEIGHT / (float)TIMER_WAIT_FRAMES) * (frameCounter % TIMER_WAIT_FRAMES);

    drawList->AddRectFilled(
        ImVec2(windowPos.x + windowSize.x - 20, windowPos.y + windowSize.y - rectLength - 50),
        ImVec2(windowPos.x + windowSize.x - PROGGRESS_BAR_WIDTH - 20, windowPos.y + windowSize.y - 50), PROGRESS_COLOR);

    drawList->AddRect(ImVec2(windowPos.x + windowSize.x - 20, windowSize.y - PROGGRESS_BAR_MAX_HEIGHT - 50),
                      ImVec2(windowPos.x + windowSize.x - PROGGRESS_BAR_WIDTH - 20, windowPos.y + windowSize.y - 50),
                      IM_COL32_BLACK, 0.0f, 0, 5.0f);
    ImGui::SetCursorPos(ImVec2(windowPos.x + windowSize.x - 100, windowPos.y + windowSize.y - 42));
    ImGui::SetWindowFontScale(0.65f);
    ImGui::Text("Next: %s", getGravityDirName(nextGravityDir));

    ImGui::End();
}

extern "C" void exl_main(void* x0, void* x1) {
    /* Setup hooking enviroment. */
    exl::hook::Initialize();

    nn::os::SetUserExceptionHandler(exception_handler, nullptr, 0, nullptr);
    installExceptionStub();

    runCodePatches();

    // General Hooks

    ControlHook::InstallAtSymbol("_ZN10StageScene7controlEv");
    InitRandomHook::InstallAtOffset(0x535850);

    // ImGui Hooks
#if IMGUI_ENABLED
    nvnImGui::InstallHooks();

    nvnImGui::addDrawFunc(drawDebugWindow);
#endif
}

extern "C" NORETURN void exl_exception_entry() {
    /* TODO: exception handling */
    EXL_ABORT(0x420);
}
