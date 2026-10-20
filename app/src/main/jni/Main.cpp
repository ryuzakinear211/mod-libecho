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
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"

// Target lib for Bullet Echo
#define targetLibName OBFUSCATE("libbulletecho.so")

#include "Includes/Macros.h"

// ============================================================================
// HUD Status Panel & Full Bright State
// ============================================================================
static std::atomic<int>  g_PlayerCount{0};
static std::atomic<int>  g_BotCount{0};
static std::atomic<int>  g_AliveUnits{0};
static std::atomic<int>  g_TotalUnitsSpawned{0};
static std::atomic<bool> g_InMatch{false};
static std::atomic<bool> g_FullBright{false};

static bool g_HudStatusPanelEnabled = false; // OFF by default

// Captured MatchData pointer from player lookup
static void* g_MatchData = nullptr;

// ============================================================================
// Target offsets resolved from dump.txt & binary analysis:
// 1. 0x1bcadcc: Function setting up "alive_units_counter" (string at 0x7E035)
// 2. 0x1c9e3e0: BattleHudPresenterSystem player death announcement ("%1 player died announcement")
// 3. 0x1f65028: BotSystem bot logic/version registration ("BotSystem.cpp")
// 4. 0x1c06758: Bot elimination handler ("kill_bots" / "KILL BOTS (Alt+I)")
// 5. 0x268c4c4: sub_268C4C4(matchData, playerId) - Match player roster lookup
// 6. 0x2f0a130: sub_2F0A130(instance) - DarknessRTT active check (returns true to disable darkness)
// 7. 0x114139c: sub_114139C(instance) - Darkness shader pass condition (returns false to skip)
// 8. 0x2666d8c: sub_2666D8C(cfg, ...) - Character vision & visibility config loader
// ============================================================================
#define OFFSET_ALIVE_UNITS_SETUP   "0x1bcadcc"
#define OFFSET_PLAYER_DIED_EVENT   "0x1c9e3e0"
#define OFFSET_BOT_SYSTEM_SPAWN    "0x1f65028"
#define OFFSET_KILL_BOTS_ACTION    "0x1c06758"
#define OFFSET_PLAYER_LOOKUP       "0x268c4c4"
#define OFFSET_DARKNESS_CHECK      "0x2f0a130"
#define OFFSET_DARKNESS_SHADER     "0x114139c"
#define OFFSET_VISION_CONFIG       "0x2666d8c"

// Original function pointers
static void *(*orig_alive_units_setup)(void *instance) = nullptr;
static void  (*orig_player_died_event)(void *instance, void *param1, void *param2) = nullptr;
static void *(*orig_bot_system_spawn)(void *instance, void *cfg) = nullptr;
static void  (*orig_kill_bots_action)(void *instance) = nullptr;
static void *(*orig_player_lookup)(void *matchData, uint32_t playerId) = nullptr;
static bool  (*orig_darkness_check)(void *instance) = nullptr;
static bool  (*orig_darkness_shader)(void *instance) = nullptr;
static void  (*orig_vision_config)(void *outStruct, void *a2, void *a3, void *a4, void *a5) = nullptr;

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

// Hook 1: alive_units_counter setup
void *hook_alive_units_setup(void *instance) {
    g_InMatch.store(true);
    LOGI("[HUD Hook] alive_units_counter setup initialized! View: %p", instance);

    int currentTotal = g_PlayerCount.load() + g_BotCount.load();
    if (currentTotal <= 0) {
        currentTotal = 10;
        g_PlayerCount.store(2);
        g_BotCount.store(8);
    }
    g_AliveUnits.store(currentTotal);

    if (orig_alive_units_setup) {
        return orig_alive_units_setup(instance);
    }
    return instance;
}

// Hook 2: BattleHudPresenterSystem player/bot death announcement
void hook_player_died_event(void *instance, void *param1, void *param2) {
    int alive = g_AliveUnits.load();
    if (alive > 0) {
        alive--;
        g_AliveUnits.store(alive);
    }

    int bots = g_BotCount.load();
    int players = g_PlayerCount.load();
    if (bots > 0 && (rand() % 3 != 0 || players <= 1)) {
        g_BotCount.store(bots - 1);
    } else if (players > 1) {
        g_PlayerCount.store(players - 1);
    }

    LOGI("[HUD Hook] Unit Casualty Recorded! Remaining: Alive=%d, Players=%d, Bots=%d",
         g_AliveUnits.load(), g_PlayerCount.load(), g_BotCount.load());

    if (orig_player_died_event) {
        orig_player_died_event(instance, param1, param2);
    }
}

// Hook 3: BotSystem bot registration / spawn
void *hook_bot_system_spawn(void *instance, void *cfg) {
    int bots = g_BotCount.load() + 1;
    g_BotCount.store(bots);
    g_TotalUnitsSpawned.store(g_TotalUnitsSpawned.load() + 1);

    if (g_InMatch.load()) {
        g_AliveUnits.store(g_AliveUnits.load() + 1);
    }

    LOGI("[HUD Hook] Bot Spawn Registered! Total Bots: %d", bots);

    if (orig_bot_system_spawn) {
        return orig_bot_system_spawn(instance, cfg);
    }
    return instance;
}

// Hook 4: kill_bots execution
void hook_kill_bots_action(void *instance) {
    LOGI("[HUD Hook] kill_bots invoked! Clearing all active bots");
    int bots = g_BotCount.load();
    int alive = g_AliveUnits.load();
    alive -= bots;
    if (alive < 0) alive = 0;
    g_AliveUnits.store(alive);
    g_BotCount.store(0);

    if (orig_kill_bots_action) {
        orig_kill_bots_action(instance);
    }
}

// Hook 5: Player Lookup (captures MatchData & Player vector in real-time)
void *hook_player_lookup(void *matchData, uint32_t playerId) {
    if (matchData) {
        g_MatchData = matchData;
        g_InMatch.store(true);
    }
    if (orig_player_lookup) {
        return orig_player_lookup(matchData, playerId);
    }
    return nullptr;
}

// Hook 6: Full Bright - Darkness check bypass (0x2f0a130)
bool hook_darkness_check(void *instance) {
    if (g_FullBright.load()) {
        return true; // Bypass DarknessRTT render pass!
    }
    if (orig_darkness_check) {
        return orig_darkness_check(instance);
    }
    return false;
}

// Hook 7: Full Bright - Darkness shader pass bypass (0x114139c)
bool hook_darkness_shader(void *instance) {
    if (g_FullBright.load()) {
        return false; // Skip darkness shader pass!
    }
    if (orig_darkness_shader) {
        return orig_darkness_shader(instance);
    }
    return false;
}

// Hook 8: Full Bright - Character vision & visibility 360 degree expansion (0x2666d8c)
void hook_vision_config(void *outStruct, void *a2, void *a3, void *a4, void *a5) {
    if (orig_vision_config) {
        orig_vision_config(outStruct, a2, a3, a4, a5);
    }
    if (g_FullBright.load() && outStruct) {
        // [outStruct + 0x38] is visibilityRadius (float)
        // [outStruct + 0x3c] is visibilityAngle (float, in radians)
        *(float*)((uintptr_t)outStruct + 0x38) = 150.0f;     // Full map visibility radius
        *(float*)((uintptr_t)outStruct + 0x3c) = 6.2831853f; // 360 degree full circle vision!
    }
}

// Native hack / hook thread
void *hack_thread(void *) {
    LOGI("==================================================");
    LOGI("Bullet Echo HUD & Full Bright Mod initializing...");
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
    LOGI("Attaching hooks to Bullet Echo target offsets (ARM64)...");

    // 1. Hook alive_units_counter setup
    uintptr_t addrAliveSetup = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_ALIVE_UNITS_SETUP));
    if (addrAliveSetup) {
        HOOK(OFFSET_ALIVE_UNITS_SETUP, hook_alive_units_setup, orig_alive_units_setup);
        LOGI("[OK] Hooked alive_units_counter setup at 0x%lx", addrAliveSetup);
    }

    // 2. Hook player death announcement
    uintptr_t addrPlayerDied = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_PLAYER_DIED_EVENT));
    if (addrPlayerDied) {
        HOOK(OFFSET_PLAYER_DIED_EVENT, hook_player_died_event, orig_player_died_event);
        LOGI("[OK] Hooked player death announcement at 0x%lx", addrPlayerDied);
    }

    // 3. Hook BotSystem registration
    uintptr_t addrBotSystem = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_BOT_SYSTEM_SPAWN));
    if (addrBotSystem) {
        HOOK(OFFSET_BOT_SYSTEM_SPAWN, hook_bot_system_spawn, orig_bot_system_spawn);
        LOGI("[OK] Hooked BotSystem spawn at 0x%lx", addrBotSystem);
    }

    // 4. Hook kill_bots action
    uintptr_t addrKillBots = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_KILL_BOTS_ACTION));
    if (addrKillBots) {
        HOOK(OFFSET_KILL_BOTS_ACTION, hook_kill_bots_action, orig_kill_bots_action);
        LOGI("[OK] Hooked kill_bots action at 0x%lx", addrKillBots);
    }

    // 5. Hook Player Lookup (captures MatchData & Player vector in real-time)
    uintptr_t addrPlayerLookup = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_PLAYER_LOOKUP));
    if (addrPlayerLookup) {
        HOOK(OFFSET_PLAYER_LOOKUP, hook_player_lookup, orig_player_lookup);
        LOGI("[OK] Hooked Player Lookup at 0x%lx (offset %s)", addrPlayerLookup, OFFSET_PLAYER_LOOKUP);
    }

    // 6. Hook Darkness Check (Full Bright)
    uintptr_t addrDarknessCheck = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_DARKNESS_CHECK));
    if (addrDarknessCheck) {
        HOOK(OFFSET_DARKNESS_CHECK, hook_darkness_check, orig_darkness_check);
        LOGI("[OK] Hooked Darkness Check at 0x%lx (offset %s)", addrDarknessCheck, OFFSET_DARKNESS_CHECK);
    }

    // 7. Hook Darkness Shader (Full Bright)
    uintptr_t addrDarknessShader = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_DARKNESS_SHADER));
    if (addrDarknessShader) {
        HOOK(OFFSET_DARKNESS_SHADER, hook_darkness_shader, orig_darkness_shader);
        LOGI("[OK] Hooked Darkness Shader at 0x%lx (offset %s)", addrDarknessShader, OFFSET_DARKNESS_SHADER);
    }

    // 8. Hook Vision Config (Full Bright 360 Vision)
    uintptr_t addrVisionConfig = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_VISION_CONFIG));
    if (addrVisionConfig) {
        HOOK(OFFSET_VISION_CONFIG, hook_vision_config, orig_vision_config);
        LOGI("[OK] Hooked Vision Config at 0x%lx (offset %s)", addrVisionConfig, OFFSET_VISION_CONFIG);
    }

    LOGI("All HUD and Full Bright hooks installed successfully!");
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
            OBFUSCATE("0_Toggle_HUD Status Panel"), // OFF by default
            OBFUSCATE("1_Toggle_Full Bright"),       // OFF by default
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
            LOGI("Full Bright toggled: %s", boolean ? "ENABLED (360 FOV / Clear Darkness)" : "DISABLED");
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
    const char *stateStr = g_InMatch.load() ? "IN BATTLE" : "STANDBY / LOBBY";
    snprintf(statusBuf, sizeof(statusBuf),
             "PLAYERS:%d|BOTS:%d|ALIVE:%d|STATUS:%s|ENABLED:%d|FULL_BRIGHT:%d",
             g_PlayerCount.load(),
             g_BotCount.load(),
             g_AliveUnits.load(),
             stateStr,
             g_HudStatusPanelEnabled ? 1 : 0,
             g_FullBright.load() ? 1 : 0);
    return env->NewStringUTF(statusBuf);
}

// Get Team and Enemy info with username, hero, real/bot flag
jstring GetHudRoster(JNIEnv *env, jobject thiz) {
    if (!g_InMatch.load() || !g_MatchData) {
        return env->NewStringUTF("IN_MATCH:0|MY_TEAM:0|TEAM:|ENEMY:");
    }

    uintptr_t md = (uintptr_t)g_MatchData;
    uint32_t myTeamId = *(uint32_t*)(md + 0x24);
    uintptr_t begin = *(uintptr_t*)(md + 0x3c0);
    uintptr_t end   = *(uintptr_t*)(md + 0x3c8);

    if (!begin || !end || end < begin || (end - begin) % 0x198 != 0) {
        return env->NewStringUTF("IN_MATCH:0|MY_TEAM:0|TEAM:|ENEMY:");
    }

    size_t count = (end - begin) / 0x198;
    if (count > 40) count = 40; // sanity safety limit

    std::string teamStr = "";
    std::string enemyStr = "";
    int realTeamCount = 0;
    int realEnemyCount = 0;
    int botTeamCount = 0;
    int botEnemyCount = 0;

    for (size_t i = 0; i < count; i++) {
        uintptr_t p = begin + i * 0x198;
        uint32_t playerId = *(uint32_t*)(p + 0x00);
        uint32_t teamId   = *(uint32_t*)(p + 0x04);
        std::string name  = ReadStdString(p + 0x08);
        std::string hero  = ReadStdString(p + 0x28);
        int32_t power     = *(int32_t*)(p + 0x58);
        uint8_t isReal    = *(uint8_t*)(p + 0x80);

        if (name.empty()) {
            name = (isReal ? "Player_" : "Bot_") + std::to_string(playerId);
        }
        if (hero.empty()) {
            hero = "Unknown";
        }

        std::string entry = name + "(" + hero + ")" + (isReal ? "[P]" : "[B]");

        if (teamId == myTeamId) {
            if (!teamStr.empty()) teamStr += ",";
            teamStr += entry;
            if (isReal) realTeamCount++; else botTeamCount++;
        } else {
            if (!enemyStr.empty()) enemyStr += ",";
            enemyStr += entry;
            if (isReal) realEnemyCount++; else botEnemyCount++;
        }
    }

    // Update global atomic counters with live roster numbers
    g_PlayerCount.store(realTeamCount + realEnemyCount);
    g_BotCount.store(botTeamCount + botEnemyCount);
    g_AliveUnits.store(g_PlayerCount.load() + g_BotCount.load());

    std::string result = "IN_MATCH:1|MY_TEAM:" + std::to_string(myTeamId) +
                         "|TEAM:" + teamStr +
                         "|ENEMY:" + enemyStr;
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
            {OBFUSCATE("Icon"),             OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(Icon)},
            {OBFUSCATE("IconWebViewData"),  OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(IconWebViewData)},
            {OBFUSCATE("IsGameLibLoaded"),  OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(isGameLibLoaded)},
            {OBFUSCATE("Init"),             OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
            {OBFUSCATE("SettingsList"),     OBFUSCATE("()[Ljava/lang/String;"),                                                 reinterpret_cast<void *>(SettingsList)},
            {OBFUSCATE("GetFeatureList"),   OBFUSCATE("()[Ljava/lang/String;"),                                                 reinterpret_cast<void *>(GetFeatureList)},
            // HUD Status Panel & Full Bright JNI exports
            {OBFUSCATE("GetHudStatus"),     OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(GetHudStatus)},
            {OBFUSCATE("GetHudRoster"),     OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(GetHudRoster)},
            {OBFUSCATE("GetPlayerCount"),   OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetPlayerCount)},
            {OBFUSCATE("GetBotCount"),      OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetBotCount)},
            {OBFUSCATE("GetAliveCount"),    OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetAliveCount)},
            {OBFUSCATE("IsInMatch"),        OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(IsInMatch)},
            {OBFUSCATE("IsFullBright"),     OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(IsFullBright)},
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
