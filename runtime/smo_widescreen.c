/* smo_widescreen.c - graphics.ini widescreen = on | off (0.5.5, on by default): the 3D view fills a phone's wider screen.
 *
 * The game draws a 16:9 picture; on a 19.5:9 or 20:9 phone the present pass leaves bars at the sides. Here (our own design,
 * from the game's functions; nothing of other ports):
 *   1. al::getSceneFrameBufferMainAspect [main 0x9cf324] (the frame buffer's width / height, which the scene's cameras take as
 *      their aspect) answers the window's aspect instead of 16:9, so the cameras see more at the sides (Hor+: the same
 *      vertical view, wider horizontally) into the same 16:9 picture, squeezed;
 *   2. the present pass (nvn_vk_draw.c) stretches the picture over the whole window, which undoes the squeeze;
 *   3. nn::ui2d::DrawInfo::SetProjectionMtx [0xafeca0] (every layout: the HUD, menus, messages) gets its matrix's first row
 *      scaled by 16:9 / the window's aspect, so the layout is squeezed by what the stretch adds: it ends up undistorted, in
 *      the middle 16:9 of the screen. Only projections of a 16:9 layout are changed (a layout drawn into a texture of
 *      another shape is left alone); a 16:9 layout drawn into a texture would be squeezed (not seen yet: to be checked).
 * The picture keeps its pixels, so it is softer by the stretch (19.5:9: 22%); resolution_scale can make up for it.
 * Read at the start (the scenes take their aspect when they are made). libsmo.so is linked with -Wl,--wrap=f_40009cf324
 * and -Wl,--wrap=f_4000afeca0 (their callers are in other objects). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "runtime.h"
#include "gfx_options.h"

void __real_f_40009cf324(Ctx *c);
void __real_f_4000afeca0(Ctx *c);
float nvn_vk_window_aspect(void);                    /* nvn_vk_draw.c: the window's width / height as the app sees it (0 until the first present) */

static int ws_on(void) {
    static int on = -1;
    if (on < 0) { on = gfx_options()->widescreen != 0; if (on) printf("smo_widescreen: on: the 3D view fills the window in the stage, demo and ending scenes; the layouts stay 16:9 in the middle\n"); }
    return on;
}
static float ws_aspect(void) {                       /* the window's aspect when it is wider than 16:9, else 0 */
    float a = nvn_vk_window_aspect();
    return a > 16.0f / 9.0f + 0.01f && a < 4.0f ? a : 0.0f;
}

/* Which scene is widened: the title screen is a full-screen layout over a small 3D scene, which widened showed the scene's
 * white and black at the sides of the 16:9 layout; it keeps its shape (bars) and the stage, demo and ending scenes are
 * widened. The scene is known by the call: TitleMenuScene::init asks at main 0x5059b4 (return address 0x5059b8). */
static volatile int g_ws_active;
int smo_widescreen_active(void) { return g_ws_active; }
void __wrap_f_40009cf324(Ctx *c) {                   /* float al::getSceneFrameBufferMainAspect(const al::Scene *) */
    uint64_t lr = c->x[30];
    __real_f_40009cf324(c);
    if (!ws_on()) return;
    float game = c->v[0].f[0], a = ws_aspect();
    if ((lr & 0xfffffffULL) == 0x5059b8ULL) { g_ws_active = 0; return; }      /* the title screen: 16:9 */
    if (a > 0.0f && game > 1.76f && game < 1.79f) {  /* a 16:9 frame buffer: the window's aspect instead */
        g_ws_active = 1;
        static int logged;
        if (!logged) { logged = 1; printf("smo_widescreen: scene aspect %.4f -> %.4f\n", game, a); fflush(stdout); }
        vzero(&c->v[0]); c->v[0].f[0] = a;
    }
}

void __wrap_f_4000afeca0(Ctx *c) {                   /* nn::ui2d::DrawInfo::SetProjectionMtx(this, const MatrixColumnMajor4x4f &) */
    uint64_t self = c->x[0];
    __real_f_4000afeca0(c);
    if (!ws_on() || !g_ws_active) return;
    float a = ws_aspect();
    if (a <= 0.0f) return;
    uint64_t m = self + 16;                          /* the matrix the call stored (column-major: element row r, column c at c * 4 + r) */
    float m00, m11;
    uint32_t u = rd32(m + 0); memcpy(&m00, &u, 4);
    u = rd32(m + 20); memcpy(&m11, &u, 4);
    if (m00 == 0.0f || m11 == 0.0f) return;
    float shape = (m11 > 0 ? m11 : -m11) / (m00 > 0 ? m00 : -m00);      /* an orthographic projection of a w x h layout: m00 = 2 / w, m11 = 2 / h */
    if (shape < 1.76f || shape > 1.79f) return;      /* not a 16:9 layout */
    float k = (16.0f / 9.0f) / a;
    for (int col = 0; col < 4; col++) {
        uint64_t p = m + (uint64_t)col * 16;
        float v; uint32_t w = rd32(p); memcpy(&v, &w, 4);
        v *= k; memcpy(&w, &v, 4); wr32(p, w);
    }
    static int logged;
    if (!logged) { logged = 1; printf("smo_widescreen: a 16:9 layout projection squeezed by %.3f\n", k); fflush(stdout); }
}
