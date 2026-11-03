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
#include "plat.h"
#include "runtime.h"
#include "gfx_options.h"

void __real_f_4000a58394(Ctx *c);
static int g_full = -1;

void __wrap_f_4000a58394(Ctx *c) {
    uint64_t out = c->x[1], in = c->x[2], view = c->x[3];
    if (g_full < 0) {
        const char *e = getenv("SMO_DYNRES");
        g_full = e ? !atoi(e) : !gfx_options()->dynres;
        printf("smo_quality: dynamic_resolution = %s\n", g_full ? "off: the 3D is drawn at the full size the game asks for" : "on: the game caps its 3D size by its quality setting");
    }
    __real_f_4000a58394(c);
    static int nlog;
    if (nlog < 8 && !rd32(view + 4)) {
        nlog++;
        printf("smo_quality: render size asked %ux%u, the game's cap gives %ux%u%s\n", rd32(in), rd32(in + 4), rd32(out), rd32(out + 4), g_full ? " (cap not applied)" : "");
        fflush(stdout);
    }
    if (g_full && !rd32(view + 4)) { wr32(out, rd32(in)); wr32(out + 4, rd32(in + 4)); }
}
