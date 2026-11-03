/* android_perf.c - Android's performance hints (ADPF, APerformanceHint, Android 13+) for the game's threads.
 *
 * Measured on a Galaxy S24 Ultra: no thread of the game is busy all the time (render 55%, the game's main thread 45%,
 * its helpers 7-21%: they wait for one another), so the CPU governor kept their clocks low and every step of a frame ran
 * slowly; the phone's own "performance" game mode made a clear difference in play. A hint session asks for the clocks
 * a frame needs instead, on any phone: the render thread and the game's threads are its threads, the target is one
 * frame at 60 fps, and after each present the time since the present before is reported. Frames that take longer make
 * the system raise those threads' clocks; frames on time let it lower them again.
 *
 * The functions are looked up at run time (libandroid.so of Android 13+; this library is built for Android 11), so an
 * older system simply has no session. SMO_PERF_HINT=0: none. Android port (2026-10-29). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sched.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <android/thermal.h>
#include "gfx_options.h"

const char *plat_data_root(void);                    /* plat.h */

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
static APerformanceHintManager *(*p_get_manager)(void);
static APerformanceHintSession *(*p_create)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
static int (*p_report)(APerformanceHintSession *, int64_t);
static int (*p_set_threads)(APerformanceHintSession *, const pid_t *, size_t);        /* Android 14+ */
static void (*p_close)(APerformanceHintSession *);
static int (*p_prefer_efficiency)(APerformanceHintSession *, int);                       /* Android 15+ */
static int (*p_workload_increase)(APerformanceHintSession *, int, int, const char *);   /* Android 16+ */

#define PERF_MAX_TIDS 64
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_tids[PERF_MAX_TIDS];
static int g_ntids, g_dirty, g_state = -1;         /* g_state: -1 not looked up, 0 off, 1 on */
static APerformanceHintSession *g_session;
static int64_t g_target_ns = 16666667;           /* SMO_PERF_TARGET_US */
static int g_boost;                               /* SMO_PERF_BOOST=1: no power-efficiency preference, a workload rise each frame */

static int perf_on(void) {
    if (g_state >= 0) return g_state;
    const char *e = getenv("SMO_PERF_HINT");
    g_state = 0;
    if (e && !atoi(e)) { printf("android_perf: performance hints off (SMO_PERF_HINT=0)\n"); return 0; }
    void *lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib) {
        p_get_manager = (APerformanceHintManager *(*)(void))dlsym(lib, "APerformanceHint_getManager");
        p_create = (APerformanceHintSession *(*)(APerformanceHintManager *, const int32_t *, size_t, int64_t))dlsym(lib, "APerformanceHint_createSession");
        p_report = (int (*)(APerformanceHintSession *, int64_t))dlsym(lib, "APerformanceHint_reportActualWorkDuration");
        p_set_threads = (int (*)(APerformanceHintSession *, const pid_t *, size_t))dlsym(lib, "APerformanceHint_setThreads");
        p_close = (void (*)(APerformanceHintSession *))dlsym(lib, "APerformanceHint_closeSession");
        p_prefer_efficiency = (int (*)(APerformanceHintSession *, int))dlsym(lib, "APerformanceHint_setPreferPowerEfficiency");
        p_workload_increase = (int (*)(APerformanceHintSession *, int, int, const char *))dlsym(lib, "APerformanceHint_notifyWorkloadIncrease");
    }
    if ((e = getenv("SMO_PERF_TARGET_US")) && atoll(e) > 0) g_target_ns = atoll(e) * 1000;
    g_boost = (e = getenv("SMO_PERF_BOOST")) && atoi(e);
    g_state = p_get_manager && p_create && p_report && p_close;
    printf("android_perf: performance hints %s\n", g_state ? "available (APerformanceHint): the game's threads get the clocks a 60 fps frame needs" : "not on this system (Android 13+ has them)");
    if (g_state) printf("android_perf: target %.2f ms, boost %s (power-efficiency call %s, workload-rise call %s)\n", g_target_ns / 1e6,
                        g_boost ? "on" : "off", p_prefer_efficiency ? "there" : "missing", p_workload_increase ? "there" : "missing");
    return g_state;
}

/* A thread of the game (render thread, guest threads) joins the session; from its own thread. */
void android_perf_add_thread(void) {
    int32_t tid = (int32_t)gettid();
    pthread_mutex_lock(&g_lock);
    int have = 0;
    for (int i = 0; i < g_ntids; i++) have |= g_tids[i] == tid;
    if (!have && g_ntids < PERF_MAX_TIDS) { g_tids[g_ntids++] = tid; g_dirty = 1; }
    pthread_mutex_unlock(&g_lock);
}

/* SMO_SPIN=<cpu>,<cpu>...: a measuring aid, not for play. One thread per CPU named, held on it and kept busy, so the
 * governor runs that CPU's cluster at its highest clock: the frame rate with full clocks, against the frame rate without.
 * They start when <files>/config/spin_now appears (a test script makes it once in gameplay), so the menus before do
 * not heat the phone and the comparison is at the same temperature. */
static void *spin_main(void *arg) {
    int cpu = (int)(intptr_t)arg;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof set, &set);
    for (volatile uint64_t n = 0;; n++) { }
    return NULL;
}
static void spin_start(void) {
    const char *e = getenv("SMO_SPIN");
    for (const char *p = e; p && *p; ) {
        int cpu = atoi(p);
        pthread_t t;
        if (pthread_create(&t, NULL, spin_main, (void *)(intptr_t)cpu) == 0) { pthread_detach(t); printf("android_perf: SMO_SPIN: CPU %d kept busy (a measuring aid)%c", cpu, 10); }
        p = strchr(p, ',');
        if (p) p++;
    }
}

/* Every 10 s of play a "perf:" line in log.txt, for reports from other phones (Export logs in the launcher): the frame
 * rate, the 1% low, the worst frame, the frame mode and scale, each CPU cluster's clock and Android's thermal state. */
static int cmp_float(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }
static void perf_line(int64_t dt) {
    enum { MAXF = 4096 };
    static float ms[MAXF];
    static int nf;
    static double total;
    static AThermalManager *thermal;
    static int thermal_tried;
    if (dt <= 0 || dt >= 1000000000) return;           /* a second or more: a pause (the app in the background), not a frame */
    if (nf < MAXF) ms[nf++] = (float)(dt / 1e6);
    total += dt / 1e6;
    if (total < 10000.0 || nf < 2) return;
    double sum = 0;
    for (int i = 0; i < nf; i++) sum += ms[i];
    qsort(ms, (size_t)nf, sizeof ms[0], cmp_float);
    int slow = nf / 100 ? nf / 100 : 1;                /* the 1% low: the average of the slowest 1% of the frames */
    double slow_sum = 0;
    for (int i = nf - slow; i < nf; i++) slow_sum += ms[i];
    float worst = ms[nf - 1];
    char cpu[256] = "";
    size_t at = 0;
    for (int i = 0; i < 16 && at < sizeof cpu - 32; i++) {      /* one cpufreq policy per cluster */
        char path[96];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq", i);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        long khz = 0;
        if (fscanf(f, "%ld", &khz) == 1) at += (size_t)snprintf(cpu + at, sizeof cpu - at, "%scpu%d %ld", at ? ", " : "", i, khz / 1000);
        fclose(f);
    }
    if (!thermal_tried) { thermal_tried = 1; thermal = AThermal_acquireManager(); }
    static const char *k_thermal[] = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
    int ts = thermal ? (int)AThermal_getCurrentThermalStatus(thermal) : -1;
    const GfxOptions *o = gfx_options();
    char mode[32];
    if (o->fps30) snprintf(mode, sizeof mode, "30 fps mode");
    else if (o->fps_cap) snprintf(mode, sizeof mode, "cap %d", o->fps_cap);
    else snprintf(mode, sizeof mode, "60 fps mode");
    char scale[16];                                    /* resolution_scale = auto reads as 0 */
    if (o->scale > 0) snprintf(scale, sizeof scale, "%.2f", o->scale); else snprintf(scale, sizeof scale, "auto");
    printf("perf: %.1f fps, 1%% low %.1f, worst frame %.1f ms over %d frames (%s, scale %s); CPU MHz %s; thermal %s%c",
           nf * 1000.0 / sum, 1000.0 * slow / slow_sum, worst, nf, mode, scale, at ? cpu : "unknown",
           ts >= 0 && ts <= 6 ? k_thermal[ts] : "unknown", 10);
    nf = 0;
    total = 0;
}

/* After a present, on the render thread: the time since the present before. */
void android_perf_frame(void) {
    static int64_t last_ns;
    static int spun, frames;
    if (!spun && getenv("SMO_SPIN") && ++frames % 30 == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/config/spin_now", plat_data_root());
        if (access(path, F_OK) == 0) { spun = 1; spin_start(); }
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec, dt = last_ns ? now - last_ns : 0;
    last_ns = now;
    perf_line(dt);
    if (!perf_on()) return;
    pthread_mutex_lock(&g_lock);
    if (g_dirty && g_ntids) {                      /* threads came (or the first frame): the session's thread list */
        g_dirty = 0;
        int n = 0;                                 /* only threads that still run: the system refuses a list with a thread that has ended */
        for (int i = 0; i < g_ntids; i++) {
            char path[64];
            snprintf(path, sizeof path, "/proc/self/task/%d", (int)g_tids[i]);
            if (access(path, F_OK) == 0) g_tids[n++] = g_tids[i];
        }
        g_ntids = n;
        if (g_session && p_set_threads && p_set_threads(g_session, (const pid_t *)g_tids, (size_t)g_ntids) == 0) {}
        else if (g_ntids) {
            APerformanceHintManager *m = p_get_manager();
            APerformanceHintSession *ns = m ? p_create(m, g_tids, (size_t)g_ntids, g_target_ns) : NULL;
            if (ns) { if (g_session) p_close(g_session); g_session = ns; if (g_boost && p_prefer_efficiency) p_prefer_efficiency(ns, 0); }      /* refused: the session before stays */
            static int said;
            if (!said++ || !ns) printf("android_perf: hint session for %d threads %s\n", g_ntids, ns ? "made" : "REFUSED (the one before stays)");
        }
    }
    APerformanceHintSession *s = g_session;
    pthread_mutex_unlock(&g_lock);
    if (s && dt > 0) p_report(s, dt > 100000000 ? 100000000 : dt);      /* at most 100 ms: a loading pause is no frame */
    if (s && g_boost && p_workload_increase) p_workload_increase(s, 1, 0, "frame");
}
