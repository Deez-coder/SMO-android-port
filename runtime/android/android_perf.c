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
#include <dirent.h>
#include <sys/types.h>
#include <android/thermal.h>
#include "gfx_options.h"
#include "plat.h"

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
/* 0.3.6: the frame's GPU time to the hint session (Android 15+: reportActualWorkDuration2 with an AWorkDuration). The idea is from
 * nx2android (perf_android.c, MIT): a GPU whose governor sees slack lowers its clock until the work fills the frame, so the
 * GPU time measured says little; a system that takes the GPU time can raise the GPU's clock with the CPUs'. Whether a
 * phone's power HAL uses it is up to the phone. SMO_PERF_GPU=0: the total only, as before. */
typedef struct AWorkDuration AWorkDuration;
static int (*p_report2)(APerformanceHintSession *, AWorkDuration *);
static AWorkDuration *(*p_wd_create)(void);
static void (*p_wd_start)(AWorkDuration *, int64_t), (*p_wd_total)(AWorkDuration *, int64_t), (*p_wd_cpu)(AWorkDuration *, int64_t), (*p_wd_gpu)(AWorkDuration *, int64_t);
static int (*p_update_target)(APerformanceHintSession *, int64_t);                       /* Android 13+ */
static AWorkDuration *g_wd;
int64_t nvn_vk_gpu_busy_ns(void);                  /* nvn_vk_draw.c */
void android_window_rate(float fps);                /* android_main.c */

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
        p_update_target = (int (*)(APerformanceHintSession *, int64_t))dlsym(lib, "APerformanceHint_updateTargetWorkDuration");
        {   const char *g = getenv("SMO_PERF_GPU");
            if (!(g && !atoi(g))) {
                p_report2 = (int (*)(APerformanceHintSession *, AWorkDuration *))dlsym(lib, "APerformanceHint_reportActualWorkDuration2");
                p_wd_create = (AWorkDuration *(*)(void))dlsym(lib, "AWorkDuration_create");
                p_wd_start = (void (*)(AWorkDuration *, int64_t))dlsym(lib, "AWorkDuration_setWorkPeriodStartTimestampNanos");
                p_wd_total = (void (*)(AWorkDuration *, int64_t))dlsym(lib, "AWorkDuration_setActualTotalDurationNanos");
                p_wd_cpu = (void (*)(AWorkDuration *, int64_t))dlsym(lib, "AWorkDuration_setActualCpuDurationNanos");
                p_wd_gpu = (void (*)(AWorkDuration *, int64_t))dlsym(lib, "AWorkDuration_setActualGpuDurationNanos");
                if (p_report2 && p_wd_create && p_wd_start && p_wd_total && p_wd_cpu && p_wd_gpu) g_wd = p_wd_create();
            }
        }
    }
    if ((e = getenv("SMO_PERF_TARGET_US")) && atoll(e) > 0) g_target_ns = atoll(e) * 1000;
    g_boost = (e = getenv("SMO_PERF_BOOST")) && atoi(e);
    g_state = p_get_manager && p_create && p_report && p_close;
    printf("android_perf: performance hints %s\n", g_state ? "available (APerformanceHint): the game's threads get the clocks a 60 fps frame needs" : "not on this system (Android 13+ has them)");
    if (g_state) printf("android_perf: target %.2f ms, SMO_PERF_BOOST %s (power-efficiency call %s, workload-rise call %s, GPU time reports %s)\n", g_target_ns / 1e6,
                        g_boost ? "on" : "off", p_prefer_efficiency ? "there" : "missing", p_workload_increase ? "there" : "missing",
                        g_wd ? "there" : (getenv("SMO_PERF_GPU") && !atoi(getenv("SMO_PERF_GPU")) ? "off (SMO_PERF_GPU=0)" : "missing (Android 15+)"));
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

/* 0.3: what a frame's time went to, from the running totals of the renderer (nvn_vk_draw.c nvn_vk_totals) and of the game's
 * logic (smo_framerate.c); the perf and hitch lines take differences of two snapshots. */
void nvn_vk_totals(double t[10]);
void smo_framerate_totals(double *logic_ms, int64_t *steps);
void smo_render_size(unsigned *w, unsigned *h, unsigned *asked_w);
typedef struct { double t[10], logic, wait; int64_t steps; } Totals;
double smo_framerate_wait_ms(void);
static void totals_now(Totals *x) { nvn_vk_totals(x->t); smo_framerate_totals(&x->logic, &x->steps); x->wait = smo_framerate_wait_ms(); }

/* The GPU's clock, where the system lets an app read it: Adreno (kgsl), MediaTek's GED (Mali on Dimensity), a devfreq node
 * of the GPU (Exynos, Tensor, others). Many phones refuse all of them (SELinux); the first perf line says which one works,
 * or that none does. Values over 10^7 are Hz, over 10^4 kHz, else MHz. */
static const char *k_gpu_clock[] = {"/sys/class/kgsl/kgsl-3d0/clock_mhz", "/sys/kernel/ged/hal/current_freqency", "/sys/kernel/gpu/gpu_clock",
                                    "/sys/class/misc/mali0/device/cur_freq", NULL};
static const char *k_gpu_busy[] = {"/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage", "/sys/kernel/ged/hal/gpu_utilization", "/sys/kernel/gpu/gpu_busy", NULL};
static long read_last_number(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[256];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    long v = -1;                                       /* GED's file is "<index> <kHz>": the last number on the first line */
    for (char *p = buf; *p && *p != '\n'; ) {
        if (*p >= '0' && *p <= '9') { v = strtol(p, &p, 10); continue; }
        p++;
    }
    return v;
}
static int gpu_clock_mhz(int *busy) {
    static int clk = -2, bz = -2;                      /* -2 not looked for yet, -1 none readable, 99 devfreq, else the index */
    static char devfreq[192];
    if (clk == -2) {
        clk = -1;
        for (int i = 0; k_gpu_clock[i]; i++) if (read_last_number(k_gpu_clock[i]) > 0) { clk = i; break; }
        if (clk < 0) {                                 /* a devfreq device with "gpu", "mali" or "kgsl" in its name */
            DIR *d = opendir("/sys/class/devfreq");
            struct dirent *e;
            while (d && (e = readdir(d))) {
                if (!strstr(e->d_name, "gpu") && !strstr(e->d_name, "mali") && !strstr(e->d_name, "kgsl")) continue;
                snprintf(devfreq, sizeof devfreq, "/sys/class/devfreq/%s/cur_freq", e->d_name);
                if (read_last_number(devfreq) > 0) { clk = 99; break; }
                devfreq[0] = 0;
            }
            if (d) closedir(d);
        }
        bz = -1;
        for (int i = 0; k_gpu_busy[i]; i++) if (read_last_number(k_gpu_busy[i]) >= 0) { bz = i; break; }
        printf("perf: GPU clock %s%s, GPU load %s%c", clk < 0 ? "not readable on this phone" : "from ", clk < 0 ? "" : clk == 99 ? devfreq : k_gpu_clock[clk],
               bz < 0 ? "not readable" : k_gpu_busy[bz], 10);
    }
    *busy = -1;
    if (bz >= 0) {                                     /* GED: "<loading> <idle> <block>", the first number */
        FILE *f = fopen(k_gpu_busy[bz], "r");
        if (f) { if (fscanf(f, "%d", busy) != 1) *busy = -1; fclose(f); }
    }
    if (clk < 0) return 0;
    long v = read_last_number(clk == 99 ? devfreq : k_gpu_clock[clk]);
    if (v <= 0) return 0;
    return (int)(v > 10000000 ? v / 1000000 : v > 10000 ? v / 1000 : v);
}
static int battery_tenths(void) {                      /* tenths of a degree C; -1000: not readable */
    long v = read_last_number("/sys/class/power_supply/battery/temp");
    return v < 0 ? -1000 : (int)v;
}

/* Every 10 s of play a "perf:" line in log.txt, for reports from other phones (Export logs in the launcher): the frame
 * rate, the 1% low, the worst frame, the frame mode and scale, each CPU cluster's clock and Android's thermal state. */
static int g_slow_frames;                          /* frames over 50 ms since the last perf line (hitch_check) */
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
    int busy, gpu_mhz = gpu_clock_mhz(&busy);         /* on a Galaxy S24 Ultra the clock decides the frame rate (nvn_vk_draw.c, submit_chunk) */
    char gpu[64] = "";
    if (gpu_mhz > 0 && busy >= 0) snprintf(gpu, sizeof gpu, "; GPU %d MHz, %d%% busy", gpu_mhz, busy);
    else if (gpu_mhz > 0) snprintf(gpu, sizeof gpu, "; GPU %d MHz", gpu_mhz);
    else if (busy >= 0) snprintf(gpu, sizeof gpu, "; GPU %d%% busy", busy);
    char heat[64] = "";
    {   static float (*p_headroom)(AThermalManager *, int);      /* Android 12+: 1.0 = the point where the system slows the phone */
        static int looked;
        if (!looked) { looked = 1; void *lib = dlopen("libandroid.so", RTLD_NOW); if (lib) p_headroom = (float (*)(AThermalManager *, int))dlsym(lib, "AThermal_getThermalHeadroom"); }
        float hr = thermal && p_headroom ? p_headroom(thermal, 0) : -1.0f;
        int bt = battery_tenths();
        size_t h = 0;
        if (hr >= 0.0f && hr == hr) h += (size_t)snprintf(heat + h, sizeof heat - h, ", headroom %.2f", hr);
        if (bt > -1000) snprintf(heat + h, sizeof heat - h, ", battery %.1f C", bt / 10.0);
    }
    char fgs[112];                                     /* 0.4: frame generation (mode, Auto's decision, quality, objects drawn again) */
    {   int fm = o->framegen, fon = gfx_framegen_on();
        int nvn_vk_fg_objects(int *cap);
        int cap = 0, taken = nvn_vk_fg_objects(&cap);
        if (!fon) snprintf(fgs, sizeof fgs, "; frame generation %s", fm == 2 ? "auto (off now)" : "off");
        else if (gfx_framegen_fast()) snprintf(fgs, sizeof fgs, "; frame generation %s, quality fast", fm == 2 ? "auto (on now)" : "on");
        else snprintf(fgs, sizeof fgs, "; frame generation %s, quality high, %d objects drawn again (at most %d)", fm == 2 ? "auto (on now)" : "on", taken, cap > 64 ? 64 : cap);
    }
    printf("perf: %.1f fps, 1%% low %.1f, worst frame %.1f ms over %d frames (%s, scale %s); CPU MHz %s; thermal %s%s%s%s%c",
           nf * 1000.0 / sum, 1000.0 * slow / slow_sum, worst, nf, mode, scale, at ? cpu : "unknown",
           ts >= 0 && ts <= 6 ? k_thermal[ts] : "unknown", heat, gpu, fgs, 10);
    /* 0.3: where the frames' time went in these 10 s, per frame. The game's logic and the render thread run side by side, and the
     * GPU after them, so a frame takes about the longest of the three; the one close to the frame's time is the limit. */
    {   static Totals prev;
        static int have_prev;
        Totals cur;
        totals_now(&cur);
        if (have_prev) {
            double n = nf, frame = sum / n;
            double logic = (cur.logic - prev.logic) / n, steps = (double)(cur.steps - prev.steps) / n;
            double render = (cur.t[0] - prev.t[0] + cur.t[1] - prev.t[1]) / n, pipes = (cur.t[2] - prev.t[2] + cur.t[3] - prev.t[3]) / n;
            double holds = (cur.t[7] - prev.t[7]) / n, gpu_n = cur.t[6] - prev.t[6], gpu_ms = gpu_n > 0 ? (cur.t[5] - prev.t[5]) / n : -1.0;
            long long nholds = (long long)(cur.t[8] - prev.t[8]);
            /* 0.3.2: each part's own work, without its waits: the logic step without its blocking waits (smo_framerate.c), the render
             * thread without its waits for the GPU, and the GPU's busy time (the union of its command buffers' spans; their sum
             * can count time twice on a tiled GPU). The tester's 0.3.1 log said "the game's logic" where the step was waiting for
             * the drawing. */
            double lwait = (cur.wait - prev.wait) / n, logic_work = logic - lwait;
            double rwait = (cur.t[4] - prev.t[4]) / n, render_work = render - rwait;
            double busy = gpu_n > 0 ? (cur.t[9] - prev.t[9]) / n : -1.0, gpu_work = busy >= 0 ? busy : gpu_ms;
            if (logic_work < 0) logic_work = 0;
            if (render_work < 0) render_work = 0;
            const char *limit = "mixed (no one part fills the frame)";
            double cap_ms = o->fps30 ? 33.333 : o->fps_cap ? 1000.0 / o->fps_cap : 16.667;
            char mixed[96];
            if (frame <= cap_ms * 1.05) limit = o->fps30 || o->fps_cap ? "the fps cap (frames on time)" : "60 fps (frames on time)";
            else if (gpu_work >= 0 && gpu_work >= frame * 0.8) limit = "the GPU";
            else if (holds >= frame * 0.3) limit = "shaders being prepared (holds)";
            else if (logic_work >= frame * 0.8) limit = "the game's logic (CPU)";
            else if (render_work >= frame * 0.8) limit = "the render thread (CPU)";
            else {                                     /* no part fills the frame: they wait for each other; the largest is named */
                const char *big = "the GPU"; double bv = gpu_work;
                if (render_work > bv) { big = "the render thread"; bv = render_work; }
                if (logic_work > bv) { big = "the game's logic"; bv = logic_work; }
                snprintf(mixed, sizeof mixed, "mixed: the parts wait for each other (largest: %s, %.0f%% of the frame)", big, frame > 0 ? 100.0 * bv / frame : 0.0);
                limit = mixed;
            }
            unsigned rw, rh, aw;
            smo_render_size(&rw, &rh, &aw);
            char size[64];                             /* no 3D view yet: the menus before the game */
            if (rw) snprintf(size, sizeof size, "3D %ux%u%s", rw, rh, aw && rw < aw ? " (the game lowered it)" : "");
            else snprintf(size, sizeof size, "no 3D view yet");
            char gpus[64];
            if (gpu_ms >= 0 && busy >= 0) snprintf(gpus, sizeof gpus, "busy %.1f (command buffers %.1f)", busy, gpu_ms);
            else if (gpu_ms >= 0) snprintf(gpus, sizeof gpus, "%.1f", gpu_ms); else snprintf(gpus, sizeof gpus, "unknown");
            printf("perf: per frame %.1f ms: GPU %s, render thread %.1f (waiting for the GPU %.1f; making pipelines and shader stages %.1f; %lld holds, %.0f ms in all), game logic %.1f (waiting %.1f; %.2f steps of %.1f working) | likely limit: %s | %s, %s, steady speed %s, %d frames over 50 ms%c",
                   frame, gpus, render, rwait, pipes, nholds, holds * n, logic, lwait, steps, steps > 0 ? logic_work / steps : 0.0, limit,
                   o->handheld ? "handheld" : "docked", size, o->steady ? "on" : "off", g_slow_frames, 10);
        }
        prev = cur; have_prev = 1;
        g_slow_frames = 0;
    }
    nf = 0;
    total = 0;
}

/* 0.3: a frame of 100 ms or more (up to 10 s: longer is the app in the background) gets a "hitch:" line, at most one a second, with what
 * its time went to; frames over 50 ms are counted for the perf line. The render thread's and the logic's figures are this frame's;
 * the GPU's is the work that finished during it. */
static void hitch_check(int64_t dt) {
    static Totals prev;
    static int have;
    static long long frame_no, hidden;
    static int64_t last_print;
    Totals cur;
    totals_now(&cur);
    frame_no++;
    double ms = dt / 1e6;
    if (ms > 50.0 && ms < 1000.0) g_slow_frames++;
    if (have && ms >= 100.0 && ms < 10000.0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int64_t now = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
        if (now - last_print >= 1000000000) {
            double render = cur.t[0] - prev.t[0] + cur.t[1] - prev.t[1], pipes = cur.t[2] - prev.t[2] + cur.t[3] - prev.t[3];
            double holds = cur.t[7] - prev.t[7], wait = cur.t[4] - prev.t[4], logic = cur.logic - prev.logic, gpu = cur.t[5] - prev.t[5];
            double most = render > logic ? render : logic;
            if (gpu > most) most = gpu;
            char more[48] = "";
            if (hidden) snprintf(more, sizeof more, " (%lld more since the last line)", hidden);
            printf("hitch: frame %lld took %.0f ms%s: render thread %.0f (making pipelines and shader stages %.0f; %lld holds, %.0f ms; waits for the GPU %.0f), game logic %.0f (%lld steps), GPU work finished %.0f; not in these %.0f (loading, the game's other threads, the system)%c",
                   frame_no, ms, more, render, pipes, (long long)(cur.t[8] - prev.t[8]), holds, wait, logic, (long long)(cur.steps - prev.steps), gpu, ms > most ? ms - most : 0.0, 10);
            last_print = now;
            hidden = 0;
        } else hidden++;
    }
    prev = cur; have = 1;
}

/* SMO_SPIN_TEST=1, a measuring aid: <files>/config/spinhot holds the hot threads' spin in microseconds (plat.h
 * plat_spinhot_ns), read every 60 frames; no file: 0. (A utilization floor through sched_setattr UTIL_CLAMP_MIN was
 * tried first: Android refuses it to apps, EPERM for every thread, 2026-11-03.) */
static void spin_test_poll(void) {
    static int on = -1, frames;
    static long cur = -1;
    if (on < 0) on = getenv("SMO_SPIN_TEST") && atoi(getenv("SMO_SPIN_TEST"));
    if (!on || ++frames % 60) return;
    char path[512];
    snprintf(path, sizeof path, "%s/config/spinhot", plat_data_root());
    long us = 0;
    FILE *f = fopen(path, "r");
    if (f) { if (fscanf(f, "%ld", &us) != 1) us = 0; fclose(f); }
    if (us < 0) us = 0;
    if (us == cur) return;
    plat_spinhot_set_ns((int64_t)us * 1000);
    printf("android_perf: hot threads spin %ld us before they block (SMO_SPIN_TEST)%c", us, 10);
    cur = us;
}

/* graphics.ini boost = auto (the default): the hot threads (plat_spin_mark_hot: render thread, the game thread that
 * presents) spin before they block while the frames are late, and do not when they are on time. Measured in Cascade's
 * waterfall view (1,300 draws, 30 fps mode, S24 Ultra): the governor kept the mid cores at 0.5-1.4 GHz and the frame at
 * 25.5 fps; a 16 ms spin raised them to 1.8-2.0 GHz and the frame to 29.3-29.8 fps, but the chip warmed ~2 C a minute
 * where it cooled ~2 C without, so it runs only where it is needed (2026-11-03).
 * One-second windows of frames (a frame over 100 ms, a loading pause, is left out): two windows in a row whose average
 * frame is 5% over the target (frame_rate 30: 33.3 ms, fps_cap: its frame, else 16.7 ms) turn it on, for at least 10 s;
 * then it goes off to see whether the frames keep up without it. A view that is still heavy turns it back on within
 * seconds, and then it stays on twice as long (at most 2 minutes) before the next look. boost = on: always on.
 * SMO_SPINHOT_US or SMO_SPIN_TEST set: the spin they give, nothing automatic. */
#define BOOST_SPIN_NS   16000000
#define BOOST_HOLD_NS   10000000000LL
#define BOOST_HOLD_MAX  120000000000LL
/* 0.3.1 (issue 23, heat on Mali): spinning only helps when the CPU side is what the frame waits for. When the GPU is the
 * limit (its own time per frame, from the GPU timestamps, at least 85% of the frame), the spinning threads wait for the GPU
 * at full clock: heat and battery for nothing, and on a phone the CPU's heat lowers the GPU's clock too. So in a one-second
 * window where the GPU is the limit, the spin stops (boost = auto and boost = on alike) and starts again once it is not.
 * Without GPU times (SMO_VK_GPUTIME=0, or a GPU without timestamps) nothing changes.
 * 0.4: OFF by default (the user: boost does what the player set, no pause of its own; the launcher's note under CPU boost says
 * it may not be worth it where it gains little). SMO_BOOST_GPU_CHECK=1 turns the pause on again. */
static int boost_gpu_bound(int64_t win_ns, int win_n) {
    static int check = -1;
    static double gpu0, n0;
    if (check < 0) { const char *e = getenv("SMO_BOOST_GPU_CHECK"); check = e && atoi(e) != 0; }
    double t[10];
    nvn_vk_totals(t);
    double gpu = t[5] - gpu0, n = t[6] - n0;
    gpu0 = t[5]; n0 = t[6];
    if (!check || n <= 0 || win_n <= 0) return 0;
    return gpu / win_n >= 0.85 * (win_ns / 1e6) / win_n;
}
static void boost_frame(int64_t now, int64_t dt) {
    static int mode = -1, on, slow_run, gpu_run, gpu_off;
    static int64_t target, win_sum, win_start, hold = BOOST_HOLD_NS, until, off_at;
    static int win_n;
    if (mode < 0) {
        const GfxOptions *o = gfx_options();
        mode = o->boost && !getenv("SMO_SPINHOT_US") && !(getenv("SMO_SPIN_TEST") && atoi(getenv("SMO_SPIN_TEST")));
        target = o->fps30 ? 33333333 : o->fps_cap ? 1000000000LL / o->fps_cap : 16666667;
        if (mode && o->boost == 2) mode = 2;
        printf("android_perf: CPU boost %s%c", mode == 2 ? "on (graphics.ini boost = on: the frame's threads always spin)" : mode ? "auto (the frame's threads spin while the frames are late)" : "off", 10);
        if (mode) plat_spinhot_set_ns(mode == 2 ? BOOST_SPIN_NS : 0);
    }
    if (!mode || dt <= 0 || dt > 100000000) return;
    if (!win_n) win_start = now;
    win_sum += dt;
    win_n++;
    if (now - win_start < 1000000000) return;
    int slow = win_sum > (int64_t)win_n * target / 100 * 105;
    double fps = win_n * 1e9 / (double)win_sum;
    int gpu_bound = boost_gpu_bound(win_sum, win_n);
    win_sum = 0;
    win_n = 0;
    gpu_run = gpu_bound ? gpu_run + 1 : 0;
    if (mode == 2) {                                   /* always on, but not while the GPU is the limit */
        if (!gpu_off && gpu_run >= 2) { gpu_off = 1; plat_spinhot_set_ns(0); printf("android_perf: CPU boost paused, the GPU is the limit (%.1f fps)%c", fps, 10); }
        else if (gpu_off && !gpu_bound) { gpu_off = 0; plat_spinhot_set_ns(BOOST_SPIN_NS); printf("android_perf: CPU boost on again, the GPU is not the limit (%.1f fps)%c", fps, 10); }
        return;
    }
    if (on) {
        if (now < until && gpu_run < 2) return;
        on = 0;
        slow_run = 0;
        off_at = now;
        plat_spinhot_set_ns(0);
        printf("android_perf: CPU boost off, %.1f fps with it (%s)%c", fps, gpu_run >= 2 ? "the GPU is the limit" : "a look whether the frames keep up without", 10);
        return;
    }
    slow_run = slow && !gpu_bound ? slow_run + 1 : 0;
    if (slow_run < 2) return;
    hold = off_at && now - off_at < 8000000000LL ? (hold * 2 > BOOST_HOLD_MAX ? BOOST_HOLD_MAX : hold * 2) : BOOST_HOLD_NS;
    on = 1;
    until = now + hold;
    plat_spinhot_set_ns(BOOST_SPIN_NS);
    printf("android_perf: CPU boost on for %.0f s, %.1f fps without it%c", hold / 1e9, fps, 10);
}

/* After a present, on the render thread: the time since the present before. */
void android_perf_frame(void) {
    { void smo_bench_frame(void); smo_bench_frame(); }      /* 0.5.3: the launcher's benchmark (smo_bench.c) */
    spin_test_poll();
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
    {   /* 0.3.6: the window's rate and the hints' target follow fps_cap (android_main.c android_window_rate). 0.3.12: with frame
         * generation on, 60 Hz and the real frames' 30 (a real and a made frame by turns, FIFO), whatever the FPS limit says */
        static int last_cap = -1;
        int fg = gfx_framegen_on();
        int cap = fg ? 30 : gfx_options()->fps30 ? 30 : gfx_options()->fps_cap;
        if (cap + (fg ? 1000 : 0) != last_cap) {
            last_cap = cap + (fg ? 1000 : 0);
            android_window_rate(fg ? 60.0f : cap > 0 && 60 % cap ? (float)cap : 60.0f);
            int64_t t = cap > 0 ? 1000000000LL / cap : 16666667;
            const char *e = getenv("SMO_PERF_TARGET_US");
            if (!(e && atoll(e) > 0) && t != g_target_ns) {
                g_target_ns = t;
                pthread_mutex_lock(&g_lock);
                if (g_session && p_update_target) p_update_target(g_session, t);
                pthread_mutex_unlock(&g_lock);
                printf("android_perf: hint target %.2f ms (fps_cap %d)%c", t / 1e6, cap, 10);
            }
        }
    }
    hitch_check(dt);
    perf_line(dt);
    boost_frame(now, dt);
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
    static int64_t gpu_last;                       /* 0.3.6: the GPU's busy time since the last report */
    int64_t gpu_now = nvn_vk_gpu_busy_ns(), gpu = gpu_last && gpu_now > gpu_last ? gpu_now - gpu_last : 0;
    gpu_last = gpu_now;
    if (s && dt > 0) {
        int64_t w = dt > 100000000 ? 100000000 : dt;      /* at most 100 ms: a loading pause is no frame */
        if (gpu > w) gpu = w;
        int r = -1;
        if (g_wd && gpu > 0) { p_wd_start(g_wd, now - w); p_wd_total(g_wd, w); p_wd_cpu(g_wd, w); p_wd_gpu(g_wd, gpu); r = p_report2(s, g_wd); }
        if (r != 0) p_report(s, w);
        static int said;
        if (g_wd && gpu > 0 && !said++) printf("android_perf: the frames' GPU time goes to the hint session too (%s)%c", r ? "REFUSED: the total only" : "taken", 10);
    }
    if (s && g_boost && p_workload_increase) p_workload_increase(s, 1, 0, "frame");
}
