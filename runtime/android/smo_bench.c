/* smo_bench.c - the launcher's benchmark (0.5.3, Advanced > Run benchmark).
 *
 * Testers' logs could not be compared: each was another place, other settings, a session of its own. The launcher starts the
 * game with SMO_BENCH=1, SMO_START_STAGE=WaterfallWorldHomeStage (Cascade Kingdom), the scripted forward view (SMO_SEQ) and a
 * settings file of its own (SMO_GRAPHICS_CONFIG: 100% scale, frame generation off, 60 fps cap); the player's save and
 * graphics.ini are copied away before and put back after (LauncherActivity). Here, after every present (android_perf_frame):
 *   1. the view: the frame's draw count in 1000..1400 for 3 s (the scripted view, about 1,210 draws);
 *   2. warm-up: until no shader or pipeline hold for 10 s (at least 10 s, at most 150 s), so a phone whose cache lacks the
 *      view's pipelines builds them first (into its normal cache) instead of measuring them;
 *   3. 60 s measured: fps, 1% low, worst frame, and per frame the GPU's busy time, the render thread's and the game logic's
 *      own work (as the perf line computes them), holds during the measurement (0 = a clean run), battery temperature;
 *   4. files/bench_result.txt and the same lines in the log, then GameActivity.benchDone (the normal way out: saves finished,
 *      the pipeline cache written). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plat.h"
#include "gfx_options.h"

void nvn_vk_totals(double t[10]);
void smo_framerate_totals(double *logic_ms, int64_t *steps);
double smo_framerate_wait_ms(void);
int nvn_vk_last_frame_draws(void);
void smo_render_size(unsigned *w, unsigned *h, unsigned *asked_w);
void android_bench_done(void);                       /* android_main.c: GameActivity.benchDone */

typedef struct { double t[10], logic, wait; int64_t steps; } BTot;
static void btot(BTot *x) { nvn_vk_totals(x->t); smo_framerate_totals(&x->logic, &x->steps); x->wait = smo_framerate_wait_ms(); }

enum { B_OFF, B_VIEW, B_WARM, B_MEASURE, B_DONE };
#define B_MAXF 9000
static int g_b = -1;
static int64_t g_b_t0, g_b_phase_t0, g_b_view_since, g_b_last_hold_t;
static double g_b_holds;
static float g_b_ft[B_MAXF];
static int g_b_nf, g_b_warm_settled = 1;
static BTot g_b_start;
static int g_b_temp0;

static int64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec; }
static int battery_tenths_b(void) {
    FILE *f = fopen("/sys/class/power_supply/battery/temp", "r");
    long v = -1000;
    if (f) { if (fscanf(f, "%ld", &v) != 1) v = -1000; fclose(f); }
    return (int)v;
}
static int cmpf(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }

static void bench_report(int64_t t) {
    BTot e;
    btot(&e);
    int n = g_b_nf;
    double sum = 0, worst = 0;
    for (int i = 0; i < n; i++) { sum += g_b_ft[i]; if (g_b_ft[i] > worst) worst = g_b_ft[i]; }
    static float s[B_MAXF];
    memcpy(s, g_b_ft, sizeof(float) * (size_t)n);
    qsort(s, (size_t)n, sizeof(float), cmpf);
    int k = n / 100 > 0 ? n / 100 : 1;               /* the slowest 1% of frames */
    double slow = 0;
    for (int i = n - k; i < n; i++) slow += s[i];
    double fps = n > 0 && sum > 0 ? n * 1000.0 / sum : 0, low = slow > 0 ? k * 1000.0 / slow : 0;
    double logic = (e.logic - g_b_start.logic) / n, lwait = (e.wait - g_b_start.wait) / n;
    double render = (e.t[0] - g_b_start.t[0] + e.t[1] - g_b_start.t[1]) / n, rwait = (e.t[4] - g_b_start.t[4]) / n;
    double gpu_n = e.t[6] - g_b_start.t[6], busy = gpu_n > 0 ? (e.t[9] - g_b_start.t[9]) / n : -1, gpu_ms = gpu_n > 0 ? (e.t[5] - g_b_start.t[5]) / n : -1;
    double gpu = busy > 0 ? busy : gpu_ms;
    long long holds = (long long)(e.t[8] - g_b_start.t[8]);
    double hold_ms = e.t[7] - g_b_start.t[7];
    unsigned rw = 0, rh = 0, aw = 0;
    smo_render_size(&rw, &rh, &aw);
    int temp1 = battery_tenths_b();
    const GfxOptions *o = gfx_options();
    char clean[160];
    if (holds == 0 && g_b_warm_settled) snprintf(clean, sizeof clean, "yes (no shaders were built during the measurement)");
    else snprintf(clean, sizeof clean, "NO: %lld shader holds (%.0f ms) during the measurement%s; run it again", holds, hold_ms, g_b_warm_settled ? "" : ", the warm-up did not settle");
    char temps[96];                                  /* the battery's file is unreadable to apps on some phones (S24: SELinux) */
    if (g_b_temp0 > -1000 && temp1 > -1000) snprintf(temps, sizeof temps, "%.1f C at the start, %.1f C at the end", g_b_temp0 / 10.0, temp1 / 10.0);
    else snprintf(temps, sizeof temps, "temperature not readable on this phone");
    char buf[2048];
    int m = snprintf(buf, sizeof buf,
        "Odyssey benchmark (Cascade Kingdom, scripted forward view, 60 s)\n"
        "fps %.1f, 1%% low %.1f, worst frame %.1f ms, %d frames\n"
        "per frame: GPU %.1f ms, render thread %.1f ms (its own work %.1f), game logic %.1f ms (its own work %.1f)\n"
        "3D size %ux%u%s, scale %.2f, %s, CPU boost %s, frame generation off\n"
        "battery %s; %.0f s from the stage to the measurement\n"
        "clean run: %s\n",
        fps, low, worst, n, gpu, render, render - rwait, logic, logic - lwait,
        rw, rh, aw && rw < aw ? " (the game lowered it)" : "", o->scale, o->handheld ? "handheld" : "docked", o->boost == 2 ? "on" : o->boost ? "auto" : "off",
        temps, (double)(g_b_phase_t0 - g_b_t0) / 1e9, clean);
    (void)m;
    printf("smo_bench: result\n%s", buf);
    fflush(stdout);
    char path[600];
    snprintf(path, sizeof path, "%s/bench_result.txt", plat_data_root());
    FILE *f = fopen(path, "w");
    if (f) { fputs(buf, f); fclose(f); }
    (void)t;
}

void smo_bench_frame(void) {                         /* render thread, after each present */
    if (g_b == B_OFF || g_b == B_DONE) return;
    int64_t t = now_ns();
    if (g_b < 0) {
        const char *e = getenv("SMO_BENCH");
        g_b = e && atoi(e) ? B_VIEW : B_OFF;
        if (g_b == B_OFF) return;
        g_b_t0 = g_b_phase_t0 = t;
        printf("smo_bench: the benchmark waits for its view (1000 to 1400 draws a frame)\n");
        fflush(stdout);
        return;
    }
    if (g_b == B_VIEW) {
        int d = nvn_vk_last_frame_draws();
        if (d >= 1000 && d <= 1400) { if (!g_b_view_since) g_b_view_since = t; }
        else g_b_view_since = 0;
        if (g_b_view_since && t - g_b_view_since >= 3000000000LL) {
            BTot x; btot(&x);
            g_b = B_WARM; g_b_phase_t0 = t; g_b_last_hold_t = t; g_b_holds = x.t[8];
            printf("smo_bench: in the view after %.0f s (%d draws); warm-up until no shader is built for 10 s\n", (double)(t - g_b_t0) / 1e9, d);
            fflush(stdout);
        } else if (t - g_b_t0 > 240000000000LL) {
            printf("smo_bench: the view was not reached in 240 s; the benchmark ends without a result\n"); fflush(stdout);
            char path[600]; snprintf(path, sizeof path, "%s/bench_result.txt", plat_data_root());
            FILE *f = fopen(path, "w"); if (f) { fputs("Odyssey benchmark: the view was not reached in 240 s, no result.\n", f); fclose(f); }
            g_b = B_DONE; android_bench_done();
        }
        return;
    }
    if (g_b == B_WARM) {
        BTot x; btot(&x);
        if (x.t[8] > g_b_holds) { g_b_holds = x.t[8]; g_b_last_hold_t = t; }
        int settled = t - g_b_last_hold_t >= 10000000000LL && t - g_b_phase_t0 >= 10000000000LL;
        if (settled || t - g_b_phase_t0 >= 150000000000LL) {
            g_b_warm_settled = settled;
            printf("smo_bench: warm-up %s after %.0f s; measuring 60 s\n", settled ? "done" : "NOT settled (150 s)", (double)(t - g_b_phase_t0) / 1e9);
            g_b = B_MEASURE; g_b_phase_t0 = t; g_b_nf = 0;
            btot(&g_b_start);
            g_b_temp0 = battery_tenths_b();
            fflush(stdout);
        }
        return;
    }
    if (g_b == B_MEASURE) {
        static int64_t last;
        if (last && g_b_nf < B_MAXF) g_b_ft[g_b_nf++] = (float)((t - last) / 1e6);
        last = t;
        if (t - g_b_phase_t0 >= 60000000000LL && g_b_nf > 10) {
            g_b = B_DONE;
            bench_report(t);
            android_bench_done();
        }
    }
}
