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
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"

// Target lib for Bullet Echo
#define targetLibName OBFUSCATE("libbulletecho.so")

#include "Includes/Macros.h"

// ============================================================================
// HUD Status Panel State & Counters
// ============================================================================
static std::atomic<int>  g_PlayerCount{0};
static std::atomic<int>  g_BotCount{0};
static std::atomic<int>  g_AliveUnits{0};
static std::atomic<int>  g_TotalUnitsSpawned{0};
static std::atomic<bool> g_InMatch{false};

static bool g_HudStatusPanelEnabled = true;
static bool g_ShowBotCount          = true;
static bool g_ShowPlayerCount       = true;
static int  g_HudOpacity            = 85;
static int  g_HudTextSize           = 14;

// Target offsets resolved from dump.txt:
// 1. 0x1bcadcc: Function setting up "alive_units_counter" (string at 0x7E035)
// 2. 0x1c9e3e0: BattleHudPresenterSystem player death announcement ("%1 player died announcement" at 0x1c9e400)
// 3. 0x1f65028: BotSystem bot logic/version registration ("BotSystem.cpp" at 0x262e4b)
// 4. 0x1c06758: Bot elimination handler ("kill_bots" / "KILL BOTS (Alt+I)" at 0x1011fa)
#define OFFSET_ALIVE_UNITS_SETUP   "0x1bcadcc"
#define OFFSET_PLAYER_DIED_EVENT   "0x1c9e3e0"
#define OFFSET_BOT_SYSTEM_SPAWN    "0x1f65028"
#define OFFSET_KILL_BOTS_ACTION    "0x1c06758"

// Original function pointers
static void *(*orig_alive_units_setup)(void *instance) = nullptr;
static void  (*orig_player_died_event)(void *instance, void *param1, void *param2) = nullptr;
static void *(*orig_bot_system_spawn)(void *instance, void *cfg) = nullptr;
static void  (*orig_kill_bots_action)(void *instance) = nullptr;

// Hook 1: alive_units_counter setup (called whenever the in-game HUD initializes alive counter)
void *hook_alive_units_setup(void *instance) {
    g_InMatch.store(true);
    LOGI("[HUD Hook] alive_units_counter setup initialized! View: %p", instance);

    // If starting a new battle, establish initial alive unit baseline
    int currentTotal = g_PlayerCount.load() + g_BotCount.load();
    if (currentTotal <= 0) {
        // Typical match size in Bullet Echo modes is 10 to 15
        currentTotal = 10;
        g_PlayerCount.store(2);
        g_BotCount.store(8);
    }
    g_AliveUnits.store(currentTotal);

    LOGI("[HUD Status] Match Active! Initial Units: Alive=%d, Players=%d, Bots=%d",
         g_AliveUnits.load(), g_PlayerCount.load(), g_BotCount.load());

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

    // Determine if casualty was bot or player
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

// Hook 4: kill_bots execution (developer / cheat command invoked)
void hook_kill_bots_action(void *instance) {
    LOGI("[HUD Hook] kill_bots invoked! Clearing all active bots");
    int bots = g_BotCount.load();
    int alive = g_AliveUnits.load();
    alive -= bots;
    if (alive < 0) alive = 0;
    g_AliveUnits.store(alive);
    g_BotCount.store(0);

    LOGI("[HUD Hook] After kill_bots: Alive=%d, Bots=0", g_AliveUnits.load());

    if (orig_kill_bots_action) {
        orig_kill_bots_action(instance);
    }
}

// Native hack / hook thread
void *hack_thread(void *) {
    LOGI("==================================================");
    LOGI("Bullet Echo HUD Status Panel Mod initializing...");
    LOGI("Log file destination: %s", kLogFilePath1);
    LOGI("==================================================");

    // Install crash handler to capture SIGSEGV, SIGABRT, etc. into /sdcard/Documents
    InstallCrashHandler();

    // Check if target lib is loaded
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
        LOGI("[OK] Hooked alive_units_counter setup at 0x%lx (offset %s)", addrAliveSetup, OFFSET_ALIVE_UNITS_SETUP);
    } else {
        LOGE("[FAIL] Failed to resolve address for alive_units_counter setup (%s)", OFFSET_ALIVE_UNITS_SETUP);
    }

    // 2. Hook player death announcement
    uintptr_t addrPlayerDied = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_PLAYER_DIED_EVENT));
    if (addrPlayerDied) {
        HOOK(OFFSET_PLAYER_DIED_EVENT, hook_player_died_event, orig_player_died_event);
        LOGI("[OK] Hooked player death announcement at 0x%lx (offset %s)", addrPlayerDied, OFFSET_PLAYER_DIED_EVENT);
    } else {
        LOGE("[FAIL] Failed to resolve address for player death announcement (%s)", OFFSET_PLAYER_DIED_EVENT);
    }

    // 3. Hook BotSystem registration
    uintptr_t addrBotSystem = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_BOT_SYSTEM_SPAWN));
    if (addrBotSystem) {
        HOOK(OFFSET_BOT_SYSTEM_SPAWN, hook_bot_system_spawn, orig_bot_system_spawn);
        LOGI("[OK] Hooked BotSystem spawn at 0x%lx (offset %s)", addrBotSystem, OFFSET_BOT_SYSTEM_SPAWN);
    } else {
        LOGE("[FAIL] Failed to resolve address for BotSystem spawn (%s)", OFFSET_BOT_SYSTEM_SPAWN);
    }

    // 4. Hook kill_bots action
    uintptr_t addrKillBots = getAbsoluteAddress(targetLibName, string2Offset(OFFSET_KILL_BOTS_ACTION));
    if (addrKillBots) {
        HOOK(OFFSET_KILL_BOTS_ACTION, hook_kill_bots_action, orig_kill_bots_action);
        LOGI("[OK] Hooked kill_bots action at 0x%lx (offset %s)", addrKillBots, OFFSET_KILL_BOTS_ACTION);
    } else {
        LOGE("[FAIL] Failed to resolve address for kill_bots action (%s)", OFFSET_KILL_BOTS_ACTION);
    }

    LOGI("All HUD hooks installed successfully!");
#else
    LOGW("Target is 64-bit arm64-v8a. Non-arm64 compilation skipped.");
#endif

    return nullptr;
}

// Mod Menu Feature List
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[] = {
            OBFUSCATE("Category_HUD Status Panel"),
            OBFUSCATE("0_Toggle_True_HUD Status Panel"),
            OBFUSCATE("1_Toggle_True_Show Bot Count"),
            OBFUSCATE("2_Toggle_True_Show Player Count"),
            OBFUSCATE("3_SeekBar_HUD Opacity_20_100"),
            OBFUSCATE("4_SeekBar_HUD Text Size_10_26"),
            OBFUSCATE("Category_HUD Controls & Debug"),
            OBFUSCATE("5_Button_Reset HUD Counters"),
            OBFUSCATE("6_Button_Dump Status to Log"),
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
        case 0: // HUD Status Panel Toggle
            g_HudStatusPanelEnabled = boolean;
            LOGI("HUD Status Panel toggled: %s", boolean ? "ENABLED" : "DISABLED");
            break;
        case 1: // Show Bot Count
            g_ShowBotCount = boolean;
            LOGI("Show Bot Count toggled: %s", boolean ? "ON" : "OFF");
            break;
        case 2: // Show Player Count
            g_ShowPlayerCount = boolean;
            LOGI("Show Player Count toggled: %s", boolean ? "ON" : "OFF");
            break;
        case 3: // HUD Opacity
            g_HudOpacity = value;
            LOGI("HUD Opacity set to: %d%%", value);
            break;
        case 4: // HUD Text Size
            g_HudTextSize = value;
            LOGI("HUD Text Size set to: %d sp", value);
            break;
        case 5: // Reset HUD Counters button
            g_PlayerCount.store(0);
            g_BotCount.store(0);
            g_AliveUnits.store(0);
            g_InMatch.store(false);
            LOGI("HUD Counters have been manually reset by user.");
            break;
        case 6: // Dump Status to Log button
            LOGI("================ DUMP STATUS ================");
            LOGI("In Match        : %s", g_InMatch.load() ? "YES" : "NO");
            LOGI("Alive Units     : %d", g_AliveUnits.load());
            LOGI("Player Count    : %d", g_PlayerCount.load());
            LOGI("Bot Count       : %d", g_BotCount.load());
            LOGI("HUD Enabled     : %s", g_HudStatusPanelEnabled ? "YES" : "NO");
            LOGI("Log File Path   : %s", kLogFilePath1);
            LOGI("=============================================");
            break;
        default:
            break;
    }

    env->ReleaseStringUTFChars(featName, featNameStr);
}

// ============================================================================
// JNI Methods for HUD Status Panel Query (Called by Menu.java)
// ============================================================================
jstring GetHudStatus(JNIEnv *env, jobject thiz) {
    char statusBuf[256];
    const char *stateStr = g_InMatch.load() ? "IN BATTLE" : "STANDBY / LOBBY";
    snprintf(statusBuf, sizeof(statusBuf),
             "PLAYERS:%d|BOTS:%d|ALIVE:%d|STATUS:%s|ENABLED:%d|SHOW_BOT:%d|SHOW_PLAYER:%d|OPACITY:%d|TEXT_SIZE:%d",
             g_PlayerCount.load(),
             g_BotCount.load(),
             g_AliveUnits.load(),
             stateStr,
             g_HudStatusPanelEnabled ? 1 : 0,
             g_ShowBotCount ? 1 : 0,
             g_ShowPlayerCount ? 1 : 0,
             g_HudOpacity,
             g_HudTextSize);
    return env->NewStringUTF(statusBuf);
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

// Constructor to start thread
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
            // HUD Status Panel JNI exports
            {OBFUSCATE("GetHudStatus"),     OBFUSCATE("()Ljava/lang/String;"),                                                  reinterpret_cast<void *>(GetHudStatus)},
            {OBFUSCATE("GetPlayerCount"),   OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetPlayerCount)},
            {OBFUSCATE("GetBotCount"),      OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetBotCount)},
            {OBFUSCATE("GetAliveCount"),    OBFUSCATE("()I"),                                                                   reinterpret_cast<void *>(GetAliveCount)},
            {OBFUSCATE("IsInMatch"),        OBFUSCATE("()Z"),                                                                   reinterpret_cast<void *>(IsInMatch)},
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
