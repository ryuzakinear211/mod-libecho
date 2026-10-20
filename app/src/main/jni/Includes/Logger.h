//
//  Logger.h
//
//  Created by MJ (Ruit) on 1/1/19.
//  Enhanced with /sdcard/Documents file logging and native crash handling
//

#ifndef Logger_h
#define Logger_h

#include <jni.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>

enum LogType {
    oDEBUG = 3,
    oERROR = 6,
    oINFO  = 4,
    oWARN  = 5
};

#define TAG "BulletEcho_ModMenu"

// File logging paths
static const char *kLogFilePath1 = "/sdcard/Documents/BulletEcho_HUD.log";
static const char *kLogFilePath2 = "/storage/emulated/0/Documents/BulletEcho_HUD.log";
static const char *kLogFilePath3 = "/sdcard/Download/BulletEcho_HUD.log";

static pthread_mutex_t g_LogMutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_LogInitialized = false;

inline void EnsureLogDirExists(const char *filePath) {
    char dir[512];
    strncpy(dir, filePath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0777);
    }
}

inline void WriteLogToFile(int level, const char *tag, const char *msg) {
    pthread_mutex_lock(&g_LogMutex);

    FILE *fp = fopen(kLogFilePath1, "a");
    if (!fp) {
        EnsureLogDirExists(kLogFilePath1);
        fp = fopen(kLogFilePath1, "a");
    }
    if (!fp) {
        fp = fopen(kLogFilePath2, "a");
    }
    if (!fp) {
        EnsureLogDirExists(kLogFilePath2);
        fp = fopen(kLogFilePath2, "a");
    }
    if (!fp) {
        fp = fopen(kLogFilePath3, "a");
    }

    if (fp) {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_info);

        const char *levelStr = "INFO";
        switch (level) {
            case oDEBUG: levelStr = "DEBUG"; break;
            case oWARN:  levelStr = "WARN";  break;
            case oERROR: levelStr = "ERROR"; break;
            case oINFO:  levelStr = "INFO";  break;
            default:     levelStr = "LOG";   break;
        }

        fprintf(fp, "[%s] [%s] [%s] %s\n", timeBuf, levelStr, tag, msg);
        fflush(fp);
        fclose(fp);
    }

    pthread_mutex_unlock(&g_LogMutex);
}

inline void AppLog(int level, const char *tag, const char *fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // 1. Log to Android logcat
    __android_log_print(level, tag, "%s", buffer);

    // 2. Log to /sdcard/Documents/
    WriteLogToFile(level, tag, buffer);
}

#define LOGD(...) AppLog(oDEBUG, TAG, __VA_ARGS__)
#define LOGE(...) AppLog(oERROR, TAG, __VA_ARGS__)
#define LOGI(...) AppLog(oINFO,  TAG, __VA_ARGS__)
#define LOGW(...) AppLog(oWARN,  TAG, __VA_ARGS__)

// Native crash signal handler
inline void NativeCrashHandler(int sig, siginfo_t *info, void *context) {
    const char *sigName = "UNKNOWN";
    switch (sig) {
        case SIGSEGV: sigName = "SIGSEGV (Segmentation Fault)"; break;
        case SIGBUS:  sigName = "SIGBUS (Bus Error)"; break;
        case SIGABRT: sigName = "SIGABRT (Abort)"; break;
        case SIGFPE:  sigName = "SIGFPE (Floating Point Exception)"; break;
        case SIGILL:  sigName = "SIGILL (Illegal Instruction)"; break;
        default:      break;
    }

    char crashMsg[1024];
    snprintf(crashMsg, sizeof(crashMsg),
             "CRASH DETECTED! Signal: %d (%s) at address %p",
             sig, sigName, info ? info->si_addr : nullptr);

    LOGE("==================================================");
    LOGE("!!! FATAL NATIVE CRASH IN BULLET ECHO MOD MENU !!!");
    LOGE("%s", crashMsg);

#if defined(__aarch64__)
    if (context) {
        ucontext_t *uc = (ucontext_t *)context;
        LOGE("Fault PC: %p, LR: %p, SP: %p",
             (void *)uc->uc_mcontext.pc,
             (void *)uc->uc_mcontext.regs[30],
             (void *)uc->uc_mcontext.sp);
    }
#endif
    LOGE("==================================================");

    // Re-raise default signal handler to terminate cleanly
    signal(sig, SIG_DFL);
    raise(sig);
}

inline void InstallCrashHandler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = NativeCrashHandler;
    sa.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);

    LOGI("Native crash handler installed successfully. Logs pointing to %s", kLogFilePath1);
}

#endif /* Logger_h */
