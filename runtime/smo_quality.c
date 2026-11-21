/* smo_quality.c - dynamic_resolution = off (graphics.ini): the game's 3D at the full size it asks for.
 *
 * al::GraphicsQualityInfo::calcRenderResolution [main 0xa58394] (out, requested size, view info) gives a view's render
 * size: a fixed 680 x 680 for one kind of view (view info +4 set), otherwise the requested size capped by the quality
 * info's own size, which al::GraphicsQualityController lowers when frames run long. Here frames run long because of the
 * CPU, so the cap only made the 3D coarse (800 x 450 measured on a Galaxy S24 Ultra) without helping the frame rate.
 * With dynamic_resolution = off the cap is not applied. libsmo.so is linked with -Wl,--wrap=f_4000a58394
 * (tools/android/build_runtime.py); the one caller is in another object. Android port (2026-11-02). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "runtime.h"
#include "gfx_options.h"

void __real_f_4000a58394(Ctx *c);
static int g_full = -1;
static volatile uint32_t g_rs_in_w, g_rs_w, g_rs_h;  /* 0.3: smo_render_size */

/* 0.1.8: with real GPU timers (0.1.7) the game's controller moved its cap about 5 times a second on a phone (S24 Ultra, 60 fps
 * mode: 300 changes in a minute), between 800 and 1600 wide, and the game makes its render targets again at each new size:
 * Cascade 44 fps with that, 54-57 without. Here the main view (the largest size asked for) follows the cap slowly and in steps:
 * a cap under the size for RES_DOWN_MS takes it one step down, a cap over it for g_up_ms one step up. When a step up is taken
 * back soon (RES_FAILED_MS) the wait for the next one doubles (up to RES_UP_MAX_MS), so the size settles where the GPU keeps up
 * and tries more only now and then. Views that ask for less go through untouched.
 * SMO_RES_HOLD=0: the game's cap as it is. SMO_RES_FIX=<width>: that width all the time (a test). */
#define RES_DOWN_MS    1000
#define RES_UP_MS      8000
#define RES_UP_MAX_MS  128000
#define RES_FAILED_MS  16000
#define RES_STEP       96
static void res_hold(uint64_t out, uint64_t in) {
    static int on = -1, fix, g_up_ms = RES_UP_MS, want_dir;      /* want_dir: the cap's side of the size, and since when */
    static uint32_t main_w, main_h, held_w;
    static int64_t want_at, up_at;
    static long long changes;
    if (on < 0) {
        const char *e = getenv("SMO_RES_HOLD");
        on = !e || atoi(e) != 0;
        fix = (e = getenv("SMO_RES_FIX")) ? atoi(e) : 0;
        printf("smo_quality: render size %s\n", fix ? "fixed (SMO_RES_FIX)" : on ? "follows the game's cap slowly, in steps" : "as the game's cap (SMO_RES_HOLD=0)");
    }
    if (!on) return;
    uint32_t iw = rd32(in), ih = rd32(in + 4), w = rd32(out);
    if (iw > main_w) { main_w = iw; main_h = ih; held_w = 0; }  /* a larger main view: start from its cap */
    if (iw != main_w) {
        /* 0.4.2: a view that asks for less gets the same cap as the main view: the game caps every view by one size (the quality
         * info's, calcRenderResolution = min(asked, cap)), and its buffers expect that. Before, these views kept the game's own
         * cap while the main view had the held size: during the intro cutscene of a new game the cap fell to 800 at once while
         * the main view stepped down from 1600, and the scene lost its shadows and lighting until the sizes met again (S24,
         * screencaps against dynamic_resolution = off; the game's own cap for every view, SMO_RES_HOLD=0, was right too). */
        static int sub = -1;                       /* TEST: SMO_RES_SUBVIEW=0 leaves these views at the game's own cap (as before) */
        if (sub < 0) { const char *e = getenv("SMO_RES_SUBVIEW"); sub = !e || atoi(e) != 0; }
        uint32_t gw = rd32(out), gh = rd32(out + 4);
        uint32_t cw = fix ? ((uint32_t)fix < main_w ? (uint32_t)fix : main_w) : held_w;
        if (sub && cw && main_w) {
            uint32_t ch = (uint32_t)((uint64_t)cw * main_h / main_w);
            wr32(out, iw < cw ? iw : cw); wr32(out + 4, ih < ch ? ih : ch);
        }
        {   static struct { uint32_t iw, ih, gw, gh, w, h; } seen[48]; static int nseen;      /* each new combination once */
            uint32_t w = rd32(out), h = rd32(out + 4); int k = 0;
            for (; k < nseen; k++) if (seen[k].iw == iw && seen[k].ih == ih && seen[k].gw == gw && seen[k].gh == gh && seen[k].w == w && seen[k].h == h) break;
            if (k == nseen && nseen < 48) {
                seen[nseen].iw = iw; seen[nseen].ih = ih; seen[nseen].gw = gw; seen[nseen].gh = gh; seen[nseen].w = w; seen[nseen].h = h; nseen++;
                printf("smo_quality: a smaller view asks %ux%u: the game's cap gives %ux%u, given %ux%u (main view %ux%u held at %u wide)\n", iw, ih, gw, gh, w, h, main_w, main_h, held_w); fflush(stdout);
            }
        }
        return;
    }
    if (fix) { w = (uint32_t)fix < iw ? (uint32_t)fix : iw; wr32(out, w); wr32(out + 4, (uint32_t)((uint64_t)w * ih / iw)); return; }
    if (!held_w) { held_w = w; want_dir = 0; return; }
    int dir = w < held_w ? -1 : w > held_w ? 1 : 0;
    int64_t now = plat_qpc(), ms = plat_qpf() / 1000;
    if (dir != want_dir) { want_dir = dir; want_at = now; }
    if (dir && now - want_at >= (dir < 0 ? RES_DOWN_MS : g_up_ms) * ms) {
        uint32_t next = dir < 0 ? (held_w > RES_STEP ? held_w - RES_STEP : held_w) : held_w + RES_STEP;
        if (dir < 0 && next < w) next = w;
        if (dir > 0 && next > w) next = w;
        if (dir < 0 && up_at && now - up_at < RES_FAILED_MS * ms) g_up_ms = g_up_ms * 2 < RES_UP_MAX_MS ? g_up_ms * 2 : RES_UP_MAX_MS;      /* the last step up did not hold */
        if (dir > 0) up_at = now;
        held_w = next; want_dir = 0; want_at = now;
        if (++changes <= 300) { printf("smo_quality: render size now %u wide (change %lld, next step up after %d s of room)\n", held_w, changes, g_up_ms / 1000); fflush(stdout); }
    }
    wr32(out, held_w); wr32(out + 4, (uint32_t)((uint64_t)held_w * ih / iw));
}

void __wrap_f_4000a58394(Ctx *c) {
    uint64_t out = c->x[1], in = c->x[2], view = c->x[3];
    if (g_full < 0) {
        const char *e = getenv("SMO_DYNRES");
        g_full = e ? !atoi(e) : !gfx_options()->dynres;
        printf("smo_quality: dynamic_resolution = %s\n", g_full ? "off: the 3D is drawn at the full size the game asks for" : "on: the game caps its 3D size by its quality setting");
    }
    __real_f_4000a58394(c);
    static int nlog; static uint32_t last_w, last_h;
    if (nlog < 300 && !rd32(view + 4) && (rd32(out) != last_w || rd32(out + 4) != last_h)) {      /* 0.1.7: when it changes */
        nlog++; last_w = rd32(out); last_h = rd32(out + 4);
        printf("smo_quality: render size asked %ux%u, the game's cap gives %ux%u%s\n", rd32(in), rd32(in + 4), rd32(out), rd32(out + 4), g_full ? " (cap not applied)" : "");
        fflush(stdout);
    }
    if (g_full && !rd32(view + 4)) { wr32(out, rd32(in)); wr32(out + 4, rd32(in + 4)); }
    else if (!rd32(view + 4)) res_hold(out, in);
    if (!rd32(view + 4) && rd32(in) >= g_rs_in_w) { g_rs_in_w = rd32(in); g_rs_w = rd32(out); g_rs_h = rd32(out + 4); }
}

/* 0.3: the main view's render size (the largest asked for since the last call) for android_perf.c's perf line; 0 when the game
 * asked for none. The next window starts afresh, so a change of mode (docked / handheld) shows within one line. */
void smo_render_size(unsigned *w, unsigned *h, unsigned *asked_w) {
    *w = g_rs_w; *h = g_rs_h; *asked_w = g_rs_in_w;
    g_rs_in_w = 0;
}

/* 0.4.2 diagnostics: what the game's quality controller (al::GraphicsQualityController::update [0xa56758]) changes besides
 * the render size: al::ModelShaderHolder::requestChangeVariatonAll [0x94ecc0] (name, value) switches every model's shader
 * variation and al::BloomDirector::setUseMipLevel [0x8bcc3c] the bloom. The intro cutscene of a new game lost its shading
 * and bloom while the controller sat in its Reduce state (S24, nerve log: five Reduce steps in the first 18 frames). Both
 * functions are called only by the controller (link wraps, tools/android/build_runtime.py). Each new value is logged. */
void __real_f_400094ecc0(Ctx *c);
void __real_f_40008bcc3c(Ctx *c);
void __wrap_f_400094ecc0(Ctx *c) {
    static char seen[24][96]; static int nseen;
    const char *a = c->x[1] ? (const char *)(uintptr_t)c->x[1] : "(null)", *b = c->x[2] ? (const char *)(uintptr_t)c->x[2] : "(null)";
    char key[96]; snprintf(key, sizeof key, "%s=%s", a, b);
    int k = 0;
    for (; k < nseen && strcmp(seen[k], key); k++) {}
    if (k == nseen && nseen < 24) { snprintf(seen[nseen++], sizeof seen[0], "%s", key); printf("smo_quality: the game's quality controller switches the models' shader variation %s to %s\n", a, b); fflush(stdout); }
    __real_f_400094ecc0(c);
}
void __wrap_f_40008bcc3c(Ctx *c) {
    static int last = -1;
    int v = (int)(c->x[1] & 1);
    if (v != last) { last = v; printf("smo_quality: the game's quality controller sets the bloom's mip level use to %d\n", v); fflush(stdout); }
    __real_f_40008bcc3c(c);
}
/* TEST ONLY, SMO_QUALITY_NO_REDUCE=1: the controller's Reduce state [nerve execute 0xa57f58 -> exeReduce 0xa57030] does
 * nothing, so the game never lowers its quality: whether the intro's missing shading and bloom come from those steps. */
void __real_f_4000a57f58(Ctx *c);
void __wrap_f_4000a57f58(Ctx *c) {
    static int off = -1, said;
    if (off < 0) { const char *e = getenv("SMO_QUALITY_NO_REDUCE"); off = e && atoi(e) != 0; }
    if (off) { if (!said) { said = 1; printf("smo_quality: TEST: the game's quality controller is kept from reducing (SMO_QUALITY_NO_REDUCE)\n"); fflush(stdout); } return; }
    __real_f_4000a57f58(c);
}
