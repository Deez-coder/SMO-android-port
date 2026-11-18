/* smo_camera.c - 0.3.9: the game's camera for each frame, for the camera motion vectors of frame generation (nvn_vk_motion.c,
 * docs/FRAME_GENERATION_PLAN.md).
 *
 * StageScene::drawMain() const [main 0x4ccaf8] draws the 3D scene once a frame. libsmo.so is linked with
 * -Wl,--wrap=f_40004ccaf8 (tools/android/build_runtime.py): before the game's own code runs, the scene's camera (its
 * al::IUseCamera at StageScene + 0x18, view 0) is read through the game's own getters: al::getViewMtxPtr [0x83ba1c] for the view
 * matrix (3 rows of 4 floats), al::getProjection [0x83bb9c] and sead::Projection::getDeviceProjectionMatrix [0x75f4e0] for this
 * frame's device projection (4 rows of 4 floats; the projection is computed lazily, so it is asked for, not read from a cached
 * field). nvn_gl.c queue_present takes the camera for the frame it presents (smo_camera_get).
 * Where to read it (StageScene + 0x18, view 0, the device projection) is Super Odyssey PC Recomp's (runtime/hle/odyssey_debug.c;
 * MIT, Copyright (c) 2026 Peiikai (LAS Recomp), Power Moon Recomp contributors, Super Odyssey PC Recomp contributors). The
 * first try, al::GraphicsSystemInfo::updateViewGpu for view 0, gave the same matrices in every frame (S24, Cascade, standing and
 * jumping), so it is not this camera. SMO_CAMERA=0: no camera (no motion vectors). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"

void __real_f_40004ccaf8(Ctx *c);
void f_400083ba1c(Ctx *c);                         /* al::getViewMtxPtr(const al::IUseCamera *, int) */
void f_400083bb9c(Ctx *c);                         /* al::getProjection(const al::IUseCamera *, int) */
void f_400075f4e0(Ctx *c);                         /* sead::Projection::getDeviceProjectionMatrix() const */

static volatile uint32_t g_seq;                    /* odd while being written */
static float g_view[12], g_proj[16];
static int g_steps;                                /* 0.3.11: the logic steps behind this camera (smo_framerate.c) */
extern volatile int smo_frame_steps;
static volatile int g_have;
static int g_on = -1;

static int guest_ptr(uint64_t p, uint64_t n) { return p >= 0x10000 && p + n > p && p + n < 0x8000000000ull; }

/* 0.3.11, frame generation step 4: Mario's position. PlayerActorHakoniwa::movement() [main 0x41dd34] runs once a logic step;
 * libsmo.so is linked with -Wl,--wrap=f_400041dd34. After it, the actor's pose keeper (+0x50) holds the translation at +8 (three
 * floats, the layout Super Odyssey PC Recomp reads for its overlay: runtime/hle/odyssey_120.c, MIT, notice above). drawMain
 * below keeps the last one with the frame's camera, so a frame's camera and Mario's position always match. */
void __real_f_400041dd34(Ctx *c);
static float g_pl_now[3]; static volatile int g_pl_ok;
static float g_pl[3]; static int g_pl_have;            /* the copy kept with the camera (inside the seqlock) */
void __wrap_f_400041dd34(Ctx *c) {
    uint64_t actor = c->x[0];
    __real_f_400041dd34(c);
    if (guest_ptr(actor, 0x58)) {
        uint64_t keeper = *(const uint64_t *)(uintptr_t)(actor + 0x50);
        if (guest_ptr(keeper, 0x14)) {
            const float *t = (const float *)(uintptr_t)(keeper + 8);
            if (t[0] == t[0] && t[1] == t[1] && t[2] == t[2]) { g_pl_now[0] = t[0]; g_pl_now[1] = t[1]; g_pl_now[2] = t[2]; g_pl_ok = 1; }
        }
    }
}

void __wrap_f_40004ccaf8(Ctx *c) {
    if (g_on < 0) { const char *e = getenv("SMO_CAMERA"); g_on = !(e && !atoi(e)); }
    if (g_on && c->x[0]) {
        uint64_t iuc = c->x[0] + 0x18, view, proj, mtx = 0;
        Ctx t = *c;                                 /* a copy: the getters run on the caller's stack, below its frame */
        t.x[0] = iuc; t.x[1] = 0; f_400083ba1c(&t); view = t.x[0];
        t = *c; t.x[0] = iuc; t.x[1] = 0; f_400083bb9c(&t); proj = t.x[0];
        if (guest_ptr(proj, 0x100)) { t = *c; t.x[0] = proj; f_400075f4e0(&t); mtx = t.x[0]; }
        if (guest_ptr(view, 48) && guest_ptr(mtx, 64)) {
            __atomic_add_fetch(&g_seq, 1, __ATOMIC_ACQ_REL);
            memcpy(g_view, (const void *)(uintptr_t)view, sizeof g_view);
            memcpy(g_proj, (const void *)(uintptr_t)mtx, sizeof g_proj);
            g_steps = smo_frame_steps;
            g_pl_have = g_pl_ok; memcpy(g_pl, g_pl_now, sizeof g_pl); g_pl_ok = 0;      /* 0 when no player moved this frame (menus, cutscenes) */
            __atomic_add_fetch(&g_seq, 1, __ATOMIC_ACQ_REL);
            if (!g_have) { g_have = 1; printf("smo_camera: the scene camera is read in StageScene::drawMain (view row 0: %.3f %.3f %.3f %.3f)\n", g_view[0], g_view[1], g_view[2], g_view[3]); }
        }
    }
    __real_f_40004ccaf8(c);
}

/* The last camera of the scene (any thread), the logic steps that led to it and Mario's position then (*pl_ok = 0: none); 0
 * while there is no camera yet. */
int smo_camera_get(float view[12], float proj[16], int *steps, float pl[3], int *pl_ok) {
    if (!g_have) return 0;
    for (int tries = 0; tries < 8; tries++) {
        uint32_t s0 = __atomic_load_n(&g_seq, __ATOMIC_ACQUIRE);
        if (s0 & 1) continue;
        memcpy(view, g_view, sizeof g_view);
        memcpy(proj, g_proj, sizeof g_proj);
        *steps = g_steps;
        memcpy(pl, g_pl, sizeof g_pl); *pl_ok = g_pl_have;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g_seq, __ATOMIC_ACQUIRE) == s0) return 1;
    }
    return 0;
}
