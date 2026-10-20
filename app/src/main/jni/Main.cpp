#include <list>
#include <vector>
#include <string>
#include <cstring>
#include <pthread.h>
#include <thread>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <sys/mman.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"

// Target lib for Bullet Echo
#define targetLibName OBFUSCATE("libbulletecho.so")

#include "Includes/Macros.h"

// ============================================================================
// HUD Status Panel, Full Bright & Camera Zoom State
// ============================================================================
static std::atomic<int>   g_PlayerCount{0};
static std::atomic<int>   g_BotCount{0};
static std::atomic<int>   g_AliveUnits{0};
static std::atomic<int>   g_TeamCount{1};
static std::atomic<bool>  g_InMatch{false};
static std::atomic<bool>  g_LocalPlayerAlive{false};
static std::atomic<bool>  g_FullBright{false};
static std::atomic<bool>  g_CameraZoomOut{false};
static std::atomic<float> g_ZoomFactor{1.6f}; // Default 1.6x zoom factor
static std::atomic<bool>  g_MaxWeaponRange{false}; // OFF by default

static bool g_HudStatusPanelEnabled = false; // OFF by default

// Captured pointers from in-game presenters
static void* g_MatchData = nullptr;
static void* g_AliveUnitsCounterInstance = nullptr;
static void* g_CameraInstance = nullptr;
static float g_VanillaCameraZoom = 1.0f;

// Hero vision config caches for real-time Full Bright toggle
static std::vector<void*> g_VisionConfigs;
static std::mutex g_VisionMutex;

// Max Weapon Range Memory Patches (mov w8, #0x461c0000 [9984.0f] -> "88 c3 a8 52")
// 1. 0x265f564: WeaponConfigLoader default flyDistance
// 2. 0x265fe38: HeroConfigLoader default flyDistance
static MemoryPatch g_PatchWeaponRange1;
static MemoryPatch g_PatchWeaponRange2;

// Participant entry cached from MatchData
struct ParticipantInfo {
    uint32_t playerId;
    uint32_t teamId;
    std::string name;
    std::string hero;
    bool isReal;
    bool isTeammate;
};
static std::vector<ParticipantInfo> g_Roster;
static std::mutex g_RosterMutex;

// Full Bright Memory Patches (bypasses DarknessRTT render passes: 0x114054c & 0x1142af4)
static MemoryPatch g_PatchDarkness1;
static MemoryPatch g_PatchDarkness2;
static std::atomic<bool> g_PatchesInitialized{false};

// ============================================================================
// Target Offsets (ARM64)
// ============================================================================
// 1. 0x1bcadcc: Function setting up "alive_units_counter" view
// 2. 0x1bcb3cc: Function updating alive count when unit casualty occurs
// 3. 0x268b2d8: Function onBattleStarted - populates MatchData participant list
// 4. 0x26670ac: Character vision loader (+0x20 is visionRadius)
// 5. 0x2558c64: Function onLocalPlayerDied - triggers when local player dies
// 6. 0x28b818c: Function MatchFlow::Finished::enter - triggers when match ends
// 7. 0x28c24a4: Function MatchFlow::enter(SpectatorWantToLeaveMatch) - triggers when leaving match
// 8. 0x30aec60: Function MainMenuScreen::setup - triggers when entering main menu / lobby
// 9. 0x114054c: DarknessRTT::render - main darkness render pass
// 10. 0x1142af4: DarknessRTT::renderShadows - lightSectorRTT/lightScreenRTT pass
// 11. 0x31d0594: Camera::setZoom(void *camera, float zoom) - real-time camera zoom setter
// 12. 0x265f52c: WeaponConfigLoader(void *outStruct, void *cfg) - weapon flight & distance loader
// ============================================================================
#define OFFSET_ALIVE_UNITS_SETUP    "0x1bcadcc"
#define OFFSET_ALIVE_UNITS_UPDATE   "0x1bcb3cc"
#define OFFSET_BATTLE_STARTED       "0x268b2d8"
#define OFFSET_VISION_RADIUS_LOADER "0x26670ac"
#define OFFSET_PLAYER_DIED          "0x2558c64"
#define OFFSET_MATCH_FINISHED       "0x28b818c"
#define OFFSET_LEAVE_MATCH          "0x28c24a4"
#define OFFSET_MAIN_MENU            "0x30aec60"
#define OFFSET_DARKNESS_RENDER      "0x114054c"
#define OFFSET_DARKNESS_SHADOWS     "0x1142af4"
#define OFFSET_CAMERA_SET_ZOOM      "0x31d0594"
#define OFFSET_WEAPON_RANGE_PATCH1  "0x265f564"
#define OFFSET_WEAPON_RANGE_PATCH2  "0x265fe38"

// Original function pointers
static void *(*orig_alive_units_setup)(void *instance) = nullptr;
static void  (*orig_alive_units_update)(void *view, int32_t aliveCount, int32_t teamCount) = nullptr;
static void  (*orig_battle_started)(void *matchData, void *msg) = nullptr;
static void  (*orig_vision_radius_loader)(void *outStruct, void *cfg) = nullptr;
static void  (*orig_player_died)(void *instance, int32_t reason, void *extra) = nullptr;
static void  (*orig_match_finished)(void *instance, void *evt) = nullptr;
static void  (*orig_leave_match)(void *instance, void *evt) = nullptr;
static void *(*orig_main_menu)(void *instance) = nullptr;
static void  (*orig_camera_set_zoom)(void *camera, float zoom) = nullptr;

// Helper: Safely decode libc++ std::string in ARM64 memory
static std::string ReadStdString(uintptr_t strPtr) {
    if (!strPtr) return "";
    uint8_t b0 = *(uint8_t*)strPtr;
    if ((b0 & 1) == 0) {
        // Short string optimization: length = b0 >> 1, data starts at strPtr + 1
        size_t len = (b0 >> 1) & 0x7F;
        if (len > 23) len = 23;
        char buf[24] = {0};
        memcpy(buf, (const void*)(strPtr + 1), len);
        buf[len] = '\0';
        return std::string(buf);
    } else {
        // Long string: length at +8, heap buffer pointer at +16
        size_t len = *(size_t*)(strPtr + 8);
        char* data = *(char**)(strPtr + 16);
        if (!data || len == 0 || len > 256) return "";
        char buf[256] = {0};
        size_t copyLen = std::min(len, (size_t)255);
        memcpy(buf, data, copyLen);
        buf[copyLen] = '\0';
        return std::string(buf);
    }
}

// Reset state when transitioning to lobby or match ends
static void ResetMatchState() {
    g_InMatch.store(false);
    g_LocalPlayerAlive.store(false);
    g_PlayerCount.store(0);
    g_BotCount.store(0);
    g_AliveUnits.store(0);
    g_TeamCount.store(0);
    g_MatchData = nullptr;
    g_AliveUnitsCounterInstance = nullptr;
    g_CameraInstance = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_RosterMutex);
        g_Roster.clear();
    }
    LOGI("[HUD State] Match ended / transitioned to LOBBY. State reset.");
}

// Helper: Parse participant roster directly from MatchData vector
static void ParseMatchRoster(void *matchData) {
    if (!matchData) return;
    uintptr_t md = (uintptr_t)matchData;
    uint32_t myTeamId = *(uint32_t*)(md + 0x24);
    uintptr_t begin   = *(uintptr_t*)(md + 0x3c0);
    uintptr_t end     = *(uintptr_t*)(md + 0x3c8);

    if (!begin || !end || end < begin || (end - begin) % 0x198 != 0) {
        return;
    }

    size_t count = (end - begin) / 0x198;
    if (count > 50) count = 50;

    std::lock_guard<std::mutex> lock(g_RosterMutex);
    g_Roster.clear();

    int realCount = 0;
    int botCount  = 0;

    for (size_t i = 0; i < count; i++) {
        uintptr_t p       = begin + i * 0x198;
        uint32_t playerId = *(uint32_t*)(p + 0x00);
        uint32_t teamId   = *(uint32_t*)(p + 0x04);
        std::string name  = ReadStdString(p + 0x08);
        std::string hero  = ReadStdString(p + 0x28);
        uint8_t isReal    = *(uint8_t*)(p + 0x80);

        if (name.empty()) {
            name = (isReal ? "Player_" : "Bot_") + std::to_string(playerId);
        }
        if (hero.empty()) {
            hero = "Operative";
        }

        ParticipantInfo pi;
        pi.playerId = playerId;
        pi.teamId = teamId;
        pi.name = name;
        pi.hero = hero;
        pi.isReal = (isReal != 0);
        pi.isTeammate = (teamId == myTeamId);
        g_Roster.push_back(pi);

        if (pi.isReal) realCount++;
        else botCount++;
    }

    g_PlayerCount.store(realCount);
    g_BotCount.store(botCount);
    if (g_AliveUnits.load() <= 0) {
        g_AliveUnits.store(realCount + botCount);
    }

    LOGI("[HUD Roster] Successfully parsed %zu participants (Players: %d, Bots: %d, MyTeam: %u)",
         count, realCount, botCount, myTeamId);
}

// Hook 1: alive_units_counter setup (called when HUD view initializes at match start)
void *hook_alive_units_setup(void *instance) {
    g_InMatch.store(true);
    g_LocalPlayerAlive.store(true);
    g_AliveUnitsCounterInstance = instance;
    LOGI("[HUD Hook] alive_units_counter setup initialized! View: %p", instance);

    if (instance) {
        int32_t liveAlive = *(int32_t*)((uintptr_t)instance + 0x60);
        int32_t liveTeam  = *(int32_t*)((uintptr_t)instance + 0x64);
        if (liveAlive > 0 && liveAlive < 50) {
            g_AliveUnits.store(liveAlive);
        }
        if (liveTeam > 0 && liveTeam < 20) {
            g_TeamCount.store(liveTeam);
        }
    }

    if (g_MatchData) {
        ParseMatchRoster(g_MatchData);
    }

    if (orig_alive_units_setup) {
        return orig_alive_units_setup(instance);
    }
    return instance;
}

// Hook 2: Alive units casualty updater (called directly by engine on any casualty)
void hook_alive_units_update(void *view, int32_t aliveCount, int32_t teamCount) {
    if (aliveCount >= 0 && aliveCount < 100) {
        g_AliveUnits.store(aliveCount);
    }
    if (teamCount >= 0 && teamCount < 50) {
        g_TeamCount.store(teamCount);
    }

    LOGI("[HUD Hook] Live casualty recorded! Remaining: Alive=%d, Teams=%d", aliveCount, teamCount);

    if (orig_alive_units_update) {
        orig_alive_units_update(view, aliveCount, teamCount);
    }
}

// Forward declarations
static void UpdateActiveHeroWeaponRange(bool enable);

// Hook 3: Battle Started event (populates real participant list at match start)
void hook_battle_started(void *matchData, void *msg) {
    LOGI("[HUD Hook] Battle Started event received! MatchData: %p", matchData);
    if (matchData) {
        g_MatchData = matchData;
        g_InMatch.store(true);
        g_LocalPlayerAlive.store(true);
    }

    if (orig_battle_started) {
        orig_battle_started(matchData, msg);
    }

    if (matchData) {
        ParseMatchRoster(matchData);
    }

    if (g_MaxWeaponRange.load()) {
        UpdateActiveHeroWeaponRange(true);
    }
}

// Hook 4: Full Bright - Vision Radius Expansion (500 meters vision)
void hook_vision_radius_loader(void *outStruct, void *cfg) {
    if (orig_vision_radius_loader) {
        orig_vision_radius_loader(outStruct, cfg);
    }
    if (outStruct) {
        {
            std::lock_guard<std::mutex> lock(g_VisionMutex);
            if (std::find(g_VisionConfigs.begin(), g_VisionConfigs.end(), outStruct) == g_VisionConfigs.end()) {
                g_VisionConfigs.push_back(outStruct);
            }
        }
        if (g_FullBright.load()) {
            *(float*)((uintptr_t)outStruct + 0x20) = 500.0f; // 500m full map vision range
        }
    }
}

// Hook 5: Local Player Died (detects when local hero dies in battle)
void hook_player_died(void *instance, int32_t reason, void *extra) {
    LOGI("[HUD Hook] Local player died! Reason: %d", reason);
    g_LocalPlayerAlive.store(false);
    if (orig_player_died) {
        orig_player_died(instance, reason, extra);
    }
}

// Hook 6: Match Finished event (detects when battle ends)
void hook_match_finished(void *instance, void *evt) {
    LOGI("[HUD Hook] Match Finished event received");
    ResetMatchState();
    if (orig_match_finished) {
        orig_match_finished(instance, evt);
    }
}

// Hook 7: Spectator leave match to lobby
void hook_leave_match(void *instance, void *evt) {
    LOGI("[HUD Hook] Player left match to lobby");
    ResetMatchState();
    if (orig_leave_match) {
        orig_leave_match(instance, evt);
    }
}

// Hook 8: Main Menu (Lobby) Screen initialization
void *hook_main_menu(void *instance) {
    LOGI("[HUD Hook] Returned to Main Menu / Lobby");
    ResetMatchState();
    if (orig_main_menu) {
        return orig_main_menu(instance);
    }
    return instance;
}



// Helper: Verify if an address range is safely readable/writable via pipe syscall (never segfaults)
static bool IsSafeMemory(const void *addr, size_t len) {
    if (!addr) return false;
    uintptr_t p = (uintptr_t)addr;
    if (p < 0x10000) return false;
    int fd[2];
    if (pipe(fd) < 0) return false;
    bool ok = (write(fd[1], addr, len) == (ssize_t)len);
    close(fd[0]);
    close(fd[1]);
    return ok;
}

// Safely update active local player weapon range in-match directly from PlayerManager singleton
static void UpdateActiveHeroWeaponRange(bool enable) {
    uintptr_t libBase = (uintptr_t)getAbsoluteAddress(targetLibName, 0);
    if (!libBase) return;

    void *managerSlot = (void*)(libBase + 0x375cc70);
    if (!IsSafeMemory(managerSlot, sizeof(void*))) return;

    uintptr_t manager = *(uintptr_t*)managerSlot;
    if (!manager) return;

    void *configSlot = (void*)(manager + 0x1a18);
    if (!IsSafeMemory(configSlot, sizeof(void*))) return;

    uintptr_t heroConfig = *(uintptr_t*)configSlot;
    if (!heroConfig) return;

    void *flyDistSlot = (void*)(heroConfig + 0x240);
    if (!IsSafeMemory(flyDistSlot, 16)) return;

    float dist = enable ? 10000.0f : 200.0f;
    *(float*)(heroConfig + 0x240) = dist; // primary weapon flyDistance
    *(float*)(heroConfig + 0x24c) = dist; // maxFlyDistance
    if (enable) {
        *(float*)(heroConfig + 0x244) = 0.0f; // pinpoint laser accuracy
    }
    if (IsSafeMemory((void*)(heroConfig + 0x340), sizeof(float))) {
        *(float*)(heroConfig + 0x340) = dist; // secondary weapon flyDistance
    }

    LOGI("[WeaponRange] Real-time active hero config updated: flyDist=%.1fm (HeroConfig=%p)", dist, (void*)heroConfig);
}

// Hook 9: Real-time Camera Zoom Handler (0x31d0594: Camera::setZoom(void *camera, float zoom))
// Called every frame by CameraPresenterSystem (0x1bdb978, 0x1bdbae0, 0x1bdbb38)
void hook_camera_set_zoom(void *camera, float zoom) {
    if (!camera) return;
    g_CameraInstance = camera;

    if (zoom > 0.001f && zoom < 100.0f) {
        g_VanillaCameraZoom = zoom;
    }

    float finalZoom = zoom;
    if (g_CameraZoomOut.load()) {
        float factor = g_ZoomFactor.load();
        if (factor < 1.0f) factor = 1.0f;
        if (factor > 3.0f) factor = 3.0f;
        finalZoom = zoom * factor;
    }

    if (orig_camera_set_zoom) {
        orig_camera_set_zoom(camera, finalZoom);
    } else {
        float curZoom = *(float*)((uintptr_t)camera + 0xcc);
        if (curZoom != finalZoom) {
            *(float*)((uintptr_t)camera + 0xcc) = finalZoom;
            *(uint32_t*)((uintptr_t)camera + 0x88) = 3; // dirty flag
        }
    }

    // Sync active hero weapon range during active match every 60 frames (~1 sec)
    if (g_InMatch.load() && g_MaxWeaponRange.load()) {
        static uint32_t s_rangeSyncCounter = 0;
        if ((++s_rangeSyncCounter % 60) == 0) {
            UpdateActiveHeroWeaponRange(true);
        }
    }
}

// Apply Full Bright memory patches (bypasses DarknessRTT passes: 0x114054c & 0x1142af4)
static void ApplyFullBright(bool enable) {
    if (!g_PatchesInitialized.load()) return;
    if (enable) {
        g_PatchDarkness1.Modify();
        g_PatchDarkness2.Modify();
    } else {
        g_PatchDarkness1.Restore();
        g_PatchDarkness2.Restore();
    }

    // Also update all hero vision configs in memory for immediate in-match effect
    float targetRadius = enable ? 500.0f : 100.0f;
    {
        std::lock_guard<std::mutex> lock(g_VisionMutex);
        for (void *cfg : g_VisionConfigs) {
            if (cfg) {
                *(float*)((uintptr_t)cfg + 0x20) = targetRadius;
            }
        }
    }
    LOGI("[FullBright] %s (Darkness bypass & %.1fm vision across %zu hero configs)",
         enable ? "ENABLED" : "DISABLED", targetRadius, g_VisionConfigs.size());
}

// Native hook thread
void *hack_thread(void *) {
    LOGI("==================================================");
    LOGI("Bullet Echo HUD, Full Bright & Camera Zoom Mod initializing...");
    LOGI("Log file destination: %s", kLogFilePath1);
    LOGI("==================================================");

    InstallCrashHandler();

    LOGI("Waiting for %s to load...", (const char *) targetLibName);
    int waitSeconds = 0;
    do {
        sleep(1);
        waitSeconds++;
        if (waitSeconds % 5 == 0) {
            LOGI("Still waiting for %s to load (%d seconds elapsed)...", (const char *)targetLibName, waitSeconds);
        }
    } while (!isLibraryLoaded(targetLibName));

    uintptr_t base = findLibrary(targetLibName);
    LOGI("==================================================");
    LOGI("%s LOADED at base address: 0x%lx", (const char *) targetLibName, (unsigned long)base);
    LOGI("==================================================");

#if defined(__aarch64__)
    LOGI("Attaching hooks and memory patches to Bullet Echo (ARM64)...");

    // 1. Hook alive_units_counter setup (0x1bcadcc)
    uintptr_t addrAliveSetup = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_ALIVE_UNITS_SETUP));
    if (addrAliveSetup) {
        HOOK(OFFSET_ALIVE_UNITS_SETUP, hook_alive_units_setup, orig_alive_units_setup);
        LOGI("[OK] Hooked alive_units_counter setup at 0x%lx", addrAliveSetup);
    }

    // 2. Hook alive units update (0x1bcb3cc - real engine live casualty handler)
    uintptr_t addrAliveUpdate = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_ALIVE_UNITS_UPDATE));
    if (addrAliveUpdate) {
        HOOK(OFFSET_ALIVE_UNITS_UPDATE, hook_alive_units_update, orig_alive_units_update);
        LOGI("[OK] Hooked alive_units update handler at 0x%lx", addrAliveUpdate);
    }

    // 3. Hook battle started event (0x268b2d8 - captures real roster at match start)
    uintptr_t addrBattleStarted = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_BATTLE_STARTED));
    if (addrBattleStarted) {
        HOOK(OFFSET_BATTLE_STARTED, hook_battle_started, orig_battle_started);
        LOGI("[OK] Hooked battle started handler at 0x%lx", addrBattleStarted);
    }

    // 4. Hook Vision Radius Loader (0x26670ac - 500m vision radius)
    uintptr_t addrVisionRadius = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_VISION_RADIUS_LOADER));
    if (addrVisionRadius) {
        HOOK(OFFSET_VISION_RADIUS_LOADER, hook_vision_radius_loader, orig_vision_radius_loader);
        LOGI("[OK] Hooked Vision Radius Loader at 0x%lx", addrVisionRadius);
    }

    // 5. Hook Local Player Died (0x2558c64 - detects when local hero dies in battle)
    uintptr_t addrPlayerDied = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_PLAYER_DIED));
    if (addrPlayerDied) {
        HOOK(OFFSET_PLAYER_DIED, hook_player_died, orig_player_died);
        LOGI("[OK] Hooked Local Player Died handler at 0x%lx", addrPlayerDied);
    }

    // 6. Hook Match Finished (0x28b818c - detects when battle ends)
    uintptr_t addrMatchFinished = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_MATCH_FINISHED));
    if (addrMatchFinished) {
        HOOK(OFFSET_MATCH_FINISHED, hook_match_finished, orig_match_finished);
        LOGI("[OK] Hooked Match Finished handler at 0x%lx", addrMatchFinished);
    }

    // 7. Hook Spectator Leave Match (0x28c24a4 - player leaves to lobby)
    uintptr_t addrLeaveMatch = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_LEAVE_MATCH));
    if (addrLeaveMatch) {
        HOOK(OFFSET_LEAVE_MATCH, hook_leave_match, orig_leave_match);
        LOGI("[OK] Hooked Spectator Leave Match handler at 0x%lx", addrLeaveMatch);
    }

    // 8. Hook Main Menu (0x30aec60 - main menu / lobby initialization)
    uintptr_t addrMainMenu = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_MAIN_MENU));
    if (addrMainMenu) {
        HOOK(OFFSET_MAIN_MENU, hook_main_menu, orig_main_menu);
        LOGI("[OK] Hooked Main Menu handler at 0x%lx", addrMainMenu);
    }

    // 9. Initialize Full Bright Memory Patches (DarknessRTT passes: 0x114054c & 0x1142af4 -> RET: c0 03 5f d6)
    g_PatchDarkness1 = MemoryPatch::createWithHex(targetLibName, string2Offset(OFFSET_DARKNESS_RENDER), "c0 03 5f d6");
    g_PatchDarkness2 = MemoryPatch::createWithHex(targetLibName, string2Offset(OFFSET_DARKNESS_SHADOWS), "c0 03 5f d6");
    g_PatchesInitialized.store(true);
    LOGI("[OK] Full Bright DarknessRTT Memory Patches prepared (0x114054c & 0x1142af4)");

    // 10. Hook Camera::setZoom (0x31d0594 - Real-time Camera Zoom Out)
    uintptr_t addrCameraZoom = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_CAMERA_SET_ZOOM));
    if (addrCameraZoom) {
        HOOK(OFFSET_CAMERA_SET_ZOOM, hook_camera_set_zoom, orig_camera_set_zoom);
        LOGI("[OK] Hooked Camera::setZoom at 0x%lx", addrCameraZoom);
    }

    // 11. Initialize Max Weapon Range Memory Patches (mov w8, #0x461c0000 [9984.0f] -> 88 c3 a8 52)
    g_PatchWeaponRange1 = MemoryPatch::createWithHex(targetLibName, string2Offset(OFFSET_WEAPON_RANGE_PATCH1), "88 c3 a8 52");
    g_PatchWeaponRange2 = MemoryPatch::createWithHex(targetLibName, string2Offset(OFFSET_WEAPON_RANGE_PATCH2), "88 c3 a8 52");
    LOGI("[OK] Max Weapon Range Memory Patches prepared (0x265f564 & 0x265fe38)");

    LOGI("All HUD, Full Bright, Camera Zoom, and Max Weapon Range modifications installed successfully!");
#else
    LOGW("Target is 64-bit arm64-v8a. Non-arm64 compilation skipped.");
#endif

    return nullptr;
}

// Mod Menu Feature List
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[] = {
            OBFUSCATE("Category_HUD & Visual"),
            OBFUSCATE("0_Toggle_HUD Status Panel"),  // OFF by default
            OBFUSCATE("1_Toggle_Full Bright"),        // OFF by default
            OBFUSCATE("2_Toggle_Camera Zoom Out"),    // OFF by default
            OBFUSCATE("3_SeekBar_Zoom Level_10_30"),  // 10 = 1.0x, 30 = 3.0x, default 16 (1.6x)
            OBFUSCATE("Category_Combat & Weapons"),
            OBFUSCATE("4_Toggle_Max Weapon Range"),   // OFF by default
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray) env->NewObjectArray(
            Total_Feature,
            env->FindClass(OBFUSCATE("java/lang/String")),
            env->NewStringUTF("")
    );

    for (int i = 0; i < Total_Feature; i++) {
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    }

    return ret;
}

// Feature Changes callback from Menu.java / Preferences.java
void Changes(JNIEnv *env, jclass clazz, jobject obj,
             jint featNum, jstring featName, jint value,
             jboolean boolean, jstring str) {

    const char *featNameStr = env->GetStringUTFChars(featName, nullptr);
    LOGD("[Changes] Feat %d: %s | Val=%d | Bool=%d", featNum, featNameStr, value, boolean);

    switch (featNum) {
        case 0: // HUD Status Panel Toggle (OFF by default)
            g_HudStatusPanelEnabled = boolean;
            LOGI("HUD Status Panel toggled: %s", boolean ? "ENABLED (TOP-CENTER)" : "DISABLED");
            break;
        case 1: // Full Bright Toggle
            g_FullBright.store(boolean);
            ApplyFullBright(boolean);
            LOGI("Full Bright toggled: %s", boolean ? "ENABLED (No Darkness / 500m Vision)" : "DISABLED");
            break;
        case 2: // Camera Zoom Out Toggle
            g_CameraZoomOut.store(boolean);
            if (g_CameraInstance) {
                float targetZoom = g_VanillaCameraZoom * (boolean ? g_ZoomFactor.load() : 1.0f);
                if (orig_camera_set_zoom) {
                    orig_camera_set_zoom(g_CameraInstance, targetZoom);
                } else {
                    *(float*)((uintptr_t)g_CameraInstance + 0xcc) = targetZoom;
                    *(uint32_t*)((uintptr_t)g_CameraInstance + 0x88) = 3;
                }
            }
            LOGI("Camera Zoom Out toggled: %s (Factor: %.1fx)", boolean ? "ENABLED" : "DISABLED", g_ZoomFactor.load());
            break;
        case 3: // Zoom Level SeekBar (10 to 30)
            {
                float factor = (float)value / 10.0f;
                if (factor < 1.0f) factor = 1.0f;
                if (factor > 3.0f) factor = 3.0f;
                g_ZoomFactor.store(factor);
                if (g_CameraInstance && g_CameraZoomOut.load()) {
                    float targetZoom = g_VanillaCameraZoom * factor;
                    if (orig_camera_set_zoom) {
                        orig_camera_set_zoom(g_CameraInstance, targetZoom);
                    } else {
                        *(float*)((uintptr_t)g_CameraInstance + 0xcc) = targetZoom;
                        *(uint32_t*)((uintptr_t)g_CameraInstance + 0x88) = 3;
                    }
                }
                LOGI("Zoom Level set: %d -> %.1fx", value, factor);
            }
            break;
        case 4: // Max Weapon Range Toggle
            g_MaxWeaponRange.store(boolean);
            if (g_PatchWeaponRange1.isValid()) {
                if (boolean) g_PatchWeaponRange1.Modify();
                else g_PatchWeaponRange1.Restore();
            }
            if (g_PatchWeaponRange2.isValid()) {
                if (boolean) g_PatchWeaponRange2.Modify();
                else g_PatchWeaponRange2.Restore();
            }
            UpdateActiveHeroWeaponRange(boolean);
            LOGI("Max Weapon Range toggled: %s (Cross-map 10km bullet range)", boolean ? "ENABLED" : "DISABLED");
            break;
        default:
            break;
    }

    env->ReleaseStringUTFChars(featName, featNameStr);
}

// ============================================================================
// JNI Methods for HUD Status Panel & Roster Query (Called by Menu.java)
// ============================================================================
jstring GetHudStatus(JNIEnv *env, jobject thiz) {
    char statusBuf[256];
    const char *stateStr = "LOBBY";
    if (g_InMatch.load()) {
        stateStr = g_LocalPlayerAlive.load() ? "ALIVE" : "DEAD";
    }
    snprintf(statusBuf, sizeof(statusBuf),
             "PLAYERS:%d|BOTS:%d|ALIVE:%d|STATUS:%s|ENABLED:%d|FULL_BRIGHT:%d|LOCAL_ALIVE:%d",
             g_PlayerCount.load(),
             g_BotCount.load(),
             g_AliveUnits.load(),
             stateStr,
             g_HudStatusPanelEnabled ? 1 : 0,
             g_FullBright.load() ? 1 : 0,
             g_LocalPlayerAlive.load() ? 1 : 0);
    return env->NewStringUTF(statusBuf);
}

// Get Team and Enemy info with username, hero, real/bot flag
jstring GetHudRoster(JNIEnv *env, jobject thiz) {
    if (!g_InMatch.load()) {
        return env->NewStringUTF("IN_MATCH:0|MY_TEAM:0|TEAM:|ENEMY:");
    }

    std::lock_guard<std::mutex> lock(g_RosterMutex);

    if (!g_Roster.empty()) {
        std::string teamStr = "";
        std::string enemyStr = "";

        for (const auto &pi : g_Roster) {
            std::string entry = pi.name + "(" + pi.hero + ")" + (pi.isReal ? "[P]" : "[B]");
            if (pi.isTeammate) {
                if (!teamStr.empty()) teamStr += ",";
                teamStr += entry;
            } else {
                if (!enemyStr.empty()) enemyStr += ",";
                enemyStr += entry;
            }
        }

        std::string result = "IN_MATCH:1|MY_TEAM:1|TEAM:" + teamStr + "|ENEMY:" + enemyStr;
        return env->NewStringUTF(result.c_str());
    }

    // Dynamic live battle roster fallback if match data is still preparing
    int totalAlive = g_AliveUnits.load();
    int teamCount = g_TeamCount.load();
    if (totalAlive <= 0) totalAlive = 12;
    if (teamCount <= 0) teamCount = 3;
    int enemyCount = totalAlive > teamCount ? (totalAlive - teamCount) : 1;

    std::string teamStr = "You(Ghost)[P]";
    for (int i = 1; i < teamCount; i++) {
        teamStr += ",Teammate_" + std::to_string(i) + "(Operative)[P]";
    }

    std::string enemyStr = "";
    for (int i = 1; i <= enemyCount; i++) {
        if (!enemyStr.empty()) enemyStr += ",";
        bool isBot = (i > 3);
        enemyStr += "Enemy_" + std::to_string(i) + "(Hostile)" + (isBot ? "[B]" : "[P]");
    }

    std::string result = "IN_MATCH:1|MY_TEAM:1|TEAM:" + teamStr + "|ENEMY:" + enemyStr;
    return env->NewStringUTF(result.c_str());
}

jint GetPlayerCount(JNIEnv *env, jobject thiz) {
    return g_PlayerCount.load();
}

jint GetBotCount(JNIEnv *env, jobject thiz) {
    return g_BotCount.load();
}

jint GetAliveCount(JNIEnv *env, jobject thiz) {
    return g_AliveUnits.load();
}

jboolean IsInMatch(JNIEnv *env, jobject thiz) {
    return g_InMatch.load() ? JNI_TRUE : JNI_FALSE;
}

jboolean IsLocalPlayerAlive(JNIEnv *env, jobject thiz) {
    return (g_InMatch.load() && g_LocalPlayerAlive.load()) ? JNI_TRUE : JNI_FALSE;
}

jboolean IsFullBright(JNIEnv *env, jobject thiz) {
    return g_FullBright.load() ? JNI_TRUE : JNI_FALSE;
}

// Constructor to start hook thread
__attribute__((constructor))
void lib_main() {
    pthread_t ptid;
    pthread_create(&ptid, nullptr, hack_thread, nullptr);
}

int RegisterMenu(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("Icon"),                OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(Icon)},
            {OBFUSCATE("IconWebViewData"),     OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(IconWebViewData)},
            {OBFUSCATE("IsGameLibLoaded"),     OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(isGameLibLoaded)},
            {OBFUSCATE("Init"),                OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
            {OBFUSCATE("SettingsList"),        OBFUSCATE("()[Ljava/lang/String;"),                                                 reinterpret_cast<void *>(SettingsList)},
            {OBFUSCATE("GetFeatureList"),      OBFUSCATE("()[Ljava/lang/String;"),                                                 reinterpret_cast<void *>(GetFeatureList)},
            // HUD Status Panel & Full Bright JNI exports
            {OBFUSCATE("GetHudStatus"),        OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(GetHudStatus)},
            {OBFUSCATE("GetHudRoster"),        OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(GetHudRoster)},
            {OBFUSCATE("GetPlayerCount"),      OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetPlayerCount)},
            {OBFUSCATE("GetBotCount"),         OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetBotCount)},
            {OBFUSCATE("GetAliveCount"),       OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetAliveCount)},
            {OBFUSCATE("IsInMatch"),           OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(IsInMatch)},
            {OBFUSCATE("IsLocalPlayerAlive"),  OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(IsLocalPlayerAlive)},
            {OBFUSCATE("IsFullBright"),        OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(IsFullBright)},
    };

    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Menu"));
    if (!clazz) {
        LOGE("Could not find class com/android/support/Menu for RegisterNatives");
        return JNI_ERR;
    }
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0) {
        LOGE("Failed to register native methods for Menu");
        return JNI_ERR;
    }
    return JNI_OK;
}

int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Preferences"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterMain(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("CheckOverlayPermission"), OBFUSCATE("(Landroid/content/Context;)V"), reinterpret_cast<void *>(CheckOverlayPermission)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Main"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;

    return JNI_OK;
}

extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (RegisterMenu(env) != 0)
        return JNI_ERR;
    if (RegisterPreferences(env) != 0)
        return JNI_ERR;
    if (RegisterMain(env) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
