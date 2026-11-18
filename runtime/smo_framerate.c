/* smo_framerate.c - how many logic steps the game runs per frame drawn.
 *
 * SMO advances its world one step per frame (sead::GameFrameworkNx::procCalc_ [main 0x731fdc] runs every task's calc
 * once), so below 60 frames a second it plays slowly. The calc is reached through the framework's vtable (call_indirect,
 * the function table) and libsmo.so is linked with -Wl,--wrap=f_4000731fdc (tools/android/build_runtime.py), so every
 * call comes here. procCalc_ takes only `this` (x0): a repeated call gets x0 and the return address back; the registers a
 * callee must keep are kept by the call itself. procDraw_ still runs once a frame.
 *
 * frame_rate = 30 (graphics.ini): the presents come every second refresh (display.c) and every procCalc_ runs the calc
 * twice: 60 steps a second, as on the console, with half the drawing. Android port (2026-11-02).
 *
 * steady_speed = on (0.2.6): the calc runs as many 1/60 s steps as the time since the last frame holds, so the game keeps
 * its speed at any frame rate down to 20 fps. A step count kept in `g_acc` (in steps): each frame adds the frame's time,
 * runs floor(acc + 0.15) steps (at least 1, at most STEADY_MAX) and takes them off. The 0.15 keeps a frame rate that
 * wobbles around 60 at one step a frame; at 40 fps it gives 1, 2, 1, 2 ...; at 30 fps 2 every frame. What is left after the
 * most steps is dropped (no catching up into a spiral: each step costs the CPU too), and so is a frame longer than 0.25 s
 * (loading, a hitch). Above 60 fps it is one step a frame, as before. Read every frame: the in-game menu changes it.
 * SMO_STEADY = 0/1 overrides graphics.ini. */
#include <stdio.h>
#include <stdlib.h>
#include "plat.h"
#include "runtime.h"
#include "gfx_options.h"

#define STEADY_MAX 3

void __real_f_4000731fdc(Ctx *c);
static int g_twice = -1, g_steady_env = -2;
static int64_t g_calcs, g_frames, g_steps, g_multi[STEADY_MAX + 1], g_calc_ticks;
static int64_t g_last;
static int64_t g_calc_total;                     /* 0.3: every logic step's time since the start (smo_framerate_totals) */
/* 0.3.2: the part of the logic steps' time the calc thread spent blocked (a guest event, condition variable, semaphore, message
 * queue or mutex in os_threads.c; a wait for the render thread in nvn_gl.c). The tester's log (Mali-G615) had "game logic" at
 * 21-25 ms a frame and the wait for the frame before at 13-17: the step waits for the drawing, so its own work was far less. */
_Thread_local int smo_t_in_calc;
static volatile int64_t g_calc_wait_ns;
void smo_calc_wait_add(int64_t ns) { if (ns > 0) __atomic_fetch_add(&g_calc_wait_ns, ns, __ATOMIC_RELAXED); }
/* 0.3.13: the same waits by kind, printed with the 1800-frame line: 0 nn::os objects (events, condition variables, semaphores,
 * message queues, mutexes: os_threads.c; nn_waitmap lines name them), 1 a synchronous job of the render thread (nvn_gl.c gl_run),
 * 2 the present (nvn_gl.c, a free window texture), 3 spinning before blocking (os_threads.c). The tester's 0.3.8 log: the logic
 * waited ~25 ms of a ~36 ms frame, and nothing said on what. */
static volatile int64_t g_calc_wait_k[4];
void smo_calc_wait_add_k(int kind, int64_t ns) { if (ns > 0) { __atomic_fetch_add(&g_calc_wait_ns, ns, __ATOMIC_RELAXED); __atomic_fetch_add(&g_calc_wait_k[kind & 3], ns, __ATOMIC_RELAXED); } }
double smo_framerate_wait_ms(void) { return (double)__atomic_load_n(&g_calc_wait_ns, __ATOMIC_RELAXED) / 1e6; }
static double g_acc;
/* 0.3.11: the logic steps of the frame being drawn (set before its calcs; procDraw_, and so StageScene::drawMain, runs after them on
 * this thread). smo_camera.c keeps it with the frame's camera: frame generation extrapolates one step, the camera motion over
 * this frame divided by its steps (nvn_vk_motion.c). */
volatile int smo_frame_steps = 1;

static int steady_on(void) {
    if (g_steady_env == -2) { const char *e = getenv("SMO_STEADY"); g_steady_env = e ? atoi(e) != 0 : -1; }
    if (g_steady_env >= 0) return g_steady_env;
    if (gfx_framegen_on()) return 1;               /* 0.3.12: frame generation runs the game at 30 (display.c): it keeps its speed */
    return gfx_options()->steady;
}

static void calc_once(Ctx *c, uint64_t self, uint64_t ret, int first) {
    if (!first) { c->x[0] = self; c->x[30] = ret; }
    plat_li t0, t1;
    t0.QuadPart = plat_qpc();
    smo_t_in_calc = 1;
    __real_f_4000731fdc(c);
    smo_t_in_calc = 0;
    t1.QuadPart = plat_qpc();
    g_calc_ticks += t1.QuadPart - t0.QuadPart;
    g_calc_total += t1.QuadPart - t0.QuadPart;
}

void __wrap_f_4000731fdc(Ctx *c) {
    if (g_twice < 0) {
        const char *e = getenv("SMO_CALC_TWICE");                    /* test switch; graphics.ini frame_rate = 30 is the setting */
        g_twice = e ? atoi(e) != 0 : gfx_options()->fps30;
        printf("smo_framerate: %s\n", g_twice ? "frame_rate = 30: the game's logic runs twice a frame (full speed at 30 frames a second)"
                                              : "frame_rate = 60: the game's logic once a frame (steady_speed: as many steps as the frame's time, read every frame)");
    }
    uint64_t self = c->x[0], ret = c->x[30];
    plat_li now, fq;
    now.QuadPart = plat_qpc(); fq.QuadPart = plat_qpf();
    int n = 1;
    if (g_twice) n = 2;
    else if (steady_on()) {
        double dt = g_last ? (double)(now.QuadPart - g_last) / (double)fq.QuadPart : 0.0;
        if (!g_last || dt > 0.25 || dt <= 0.0) g_acc = 1.0;      /* the first frame, loading, a hitch: one step, nothing owed */
        else g_acc += dt * 60.0;
        n = (int)(g_acc + 0.15);
        if (n < 1) n = 1;
        if (n > STEADY_MAX) n = STEADY_MAX;
        g_acc -= n;
        if (g_acc > 1.0) g_acc = 1.0;                           /* more than the most steps: dropped */
        if (g_acc < -0.5) g_acc = -0.5;                         /* above 60 fps: one step a frame, not owed later */
    } else g_acc = 0.0;
    g_last = now.QuadPart;
    smo_frame_steps = n;
    int64_t c0 = g_calc_ticks;
    for (int i = 0; i < n; i++) calc_once(c, self, ret, i == 0);
    int64_t cd = g_calc_ticks - c0;
    g_frames++; g_steps += n; g_multi[n]++; g_calcs += n;
    {   /* 0.2.15, SMO_SPIN_AB=<frames> (test only): the spin's yield (plat.h plat_spin_yield_on) off and on by turns every that many
         * frames; frames and logic steps of each state are timed apart and printed with the 1800-frame line ("spin A/B"). */
        static int ab = -2; static int64_t abf[2], abft[2], abs_[2], abst[2], abt0;
        if (ab == -2) { const char *e = getenv("SMO_SPIN_AB"); ab = e ? atoi(e) : 0; }
        if (ab > 0) {
            int st = plat_spin_yield_on() ? 1 : 0;
            if (abt0) { abf[st]++; abft[st] += now.QuadPart - abt0; abs_[st] += n; abst[st] += cd; }
            abt0 = now.QuadPart;
            if (g_frames % ab == 0) __atomic_store_n(&plat_spin_yield_, !st, __ATOMIC_RELAXED);
            if (g_frames % 1800 == 0) {
                double q = (double)fq.QuadPart / 1000.0;
                printf("smo_framerate: spin A/B: yield off: %lld frames, %.2f ms a frame, %.3f steps a frame, a step %.2f ms | yield on: %lld frames, %.2f ms a frame, %.3f steps a frame, a step %.2f ms\n",
                       (long long)abf[0], abf[0] ? (double)abft[0] / q / (double)abf[0] : 0.0, abf[0] ? (double)abs_[0] / (double)abf[0] : 0.0, abs_[0] ? (double)abst[0] / q / (double)abs_[0] : 0.0,
                       (long long)abf[1], abf[1] ? (double)abft[1] / q / (double)abf[1] : 0.0, abf[1] ? (double)abs_[1] / (double)abf[1] : 0.0, abs_[1] ? (double)abst[1] / q / (double)abs_[1] : 0.0);
            }
        }
    }
    if (g_frames % 1800 == 0) {
        printf("smo_framerate: frames %lld..%lld: %.3f logic steps a frame (1: %lld, 2: %lld, 3: %lld), a step %.2f ms%s\n",
               (long long)(g_frames - 1800), (long long)g_frames, (double)g_steps / 1800.0, (long long)g_multi[1], (long long)g_multi[2], (long long)g_multi[3],
               g_steps ? (double)g_calc_ticks * 1000.0 / (double)fq.QuadPart / (double)g_steps : 0.0, g_twice ? " (frame_rate = 30)" : steady_on() ? " (steady_speed on)" : "");
        fflush(stdout);
        {   int64_t k[4]; for (int i = 0; i < 4; i++) k[i] = __atomic_exchange_n(&g_calc_wait_k[i], 0, __ATOMIC_RELAXED);
            printf("smo_framerate: the logic's waiting in those frames, per frame: nn::os objects %.2f ms, render thread jobs %.2f ms, the present %.2f ms, spinning %.2f ms\n",
                   (double)k[0] / 1e6 / 1800.0, (double)k[1] / 1e6 / 1800.0, (double)k[2] / 1e6 / 1800.0, (double)k[3] / 1e6 / 1800.0); }
        g_steps = 0; g_calc_ticks = 0; g_multi[1] = g_multi[2] = g_multi[3] = 0;
    }
}

/* 0.3: running totals for android_perf.c's perf and hitch lines (it takes differences): ms spent in the game's logic steps, and the
 * number of steps. Read from the render thread without a lock: a figure for a log line, a step late at worst. */
void smo_framerate_totals(double *logic_ms, int64_t *steps) {
    static int64_t fq;
    if (!fq) fq = plat_qpf();
    *logic_ms = (double)g_calc_total * 1000.0 / (double)fq;
    *steps = g_calcs;
}
