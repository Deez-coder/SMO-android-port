/* smo_capture.c - the screen capture (al::ScreenCaptureExecutor / al::ScreenCapture) at two logic steps per frame (0.4.2).
 *
 * The fix is the requestCapture wrap below (the frozen first-capture cutscene). The copy wraps are a TEST that is off by
 * default: al::ScreenCapture::copyImageFromFrameBuffer [main 0xa80000] draws the source texture (the frame buffer's colour
 * target, RenderBuffer +32) with agl::utl::ImageFilter2D::drawTexture [0x793860] at a uv scale of (capture width / source
 * width, capture height / source height), a 1:1 copy of the capture-sized corner. I took a size mismatch for the coloured
 * area on the right of the first-capture cutscene, but on the S24 every copy logged had the same size (1600x900 from
 * 1600x900, scale 1). SMO_CAPTURE_UV=1 reads the whole source (scale 1, 1) when the sizes differ; the log names every copy
 * with a scale other than 1 either way.
 * libsmo.so is linked with -Wl,--wrap=f_40009d35b8, f_4000a80000 and f_4000793860 (the callers are in other objects). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "plat.h"
#include "runtime.h"

void __real_f_4000a80000(Ctx *c);
void __real_f_4000793860(Ctx *c);
void __real_f_40009d35b8(Ctx *c);
void smo_frame_draw_after_step(void);

/* THE FROZEN FIRST-CAPTURE CUTSCENE. A demo covers a camera cut with one frozen frame: al::requestCaptureScreenCover(actor, n)
 * sets the scene's ScreenCoverCtrl (+0 frames, +4 capture request). HakoniwaSequence::exePlayStage (each logic step) sees the
 * request and calls al::ScreenCaptureExecutor::requestCapture [0x9d35b8] (+8 capture, +9 draw off); the next DRAW copies the
 * frame and turns drawing of the capture on (+9). ScreenCoverCtrl::update counts the frames down and at 0 exePlayStage calls
 * offDraw (+9 = 0) and resets the counter to -1. With two logic steps before one draw (frame generation, steady speed under
 * 60 fps) the count reached 0 and offDraw ran BEFORE the draw; the capture request (+8) was still set, so the draw copied the
 * frame afterwards and showed it until the next cover: the cutscene's picture froze while its sound played on (S24, the first
 * frog capture, frame generation on). So the step that asks for a capture is followed by its draw at once (the steps it leaves
 * out are owed to the next frame, as for a cube map change in smo_framerate.c). -Wl,--wrap=f_40009d35b8 (callers in other objects). */
void __wrap_f_40009d35b8(Ctx *c) {                   /* (ScreenCaptureExecutor *this, bool draw_off, int index) */
    static int logged;
    if (logged < 16) { logged++; printf("smo_capture: screen capture asked for (index %d): the frame is drawn after this logic step\n", (int)(int32_t)c->x[2]); fflush(stdout); }
    __real_f_40009d35b8(c);
    smo_frame_draw_after_step();
}

static __thread int t_in_copy;
static __thread uint64_t t_cap_tex, t_src_tex;

static int capture_uv_on(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("SMO_CAPTURE_UV"); on = e && atoi(e) != 0; if (on) printf("smo_capture: TEST: the screen capture copy reads the whole source when the sizes differ (SMO_CAPTURE_UV)\n"); }
    return on;
}

void __wrap_f_4000a80000(Ctx *c) {                   /* (ScreenCapture *this, agl::DrawContext *, const agl::RenderBuffer *src) */
    t_cap_tex = rd64(c->x[0] + 8);                    /* the capture's TextureData */
    t_src_tex = rd64(c->x[2] + 32);                   /* the frame buffer's colour TextureData */
    t_in_copy = 1;
    __real_f_4000a80000(c);
    t_in_copy = 0;
}

void __wrap_f_4000793860(Ctx *c) {                   /* (ctx, const TextureSampler &, const sead::Viewport &, const Vector2f &uv_scale, const Vector2f &uv_offset) */
    if (t_in_copy) {
        t_in_copy = 0;                                /* the copy's one draw */
        uint64_t uv = c->x[3];
        uint32_t ux = rd32(uv), uy = rd32(uv + 4);
        float fx, fy; memcpy(&fx, &ux, 4); memcpy(&fy, &uy, 4);
        int on = capture_uv_on();
        static int logged;
        if (logged < 16 && (fx != 1.0f || fy != 1.0f)) {   /* only the copies whose sizes differ */
            logged++;
            printf("smo_capture: screen capture copy: capture %ux%u, source %ux%u, the game's uv scale %.3f %.3f%s\n",
                   t_cap_tex ? rd16(t_cap_tex + 48) : 0, t_cap_tex ? rd16(t_cap_tex + 50) : 0,
                   t_src_tex ? rd16(t_src_tex + 48) : 0, t_src_tex ? rd16(t_src_tex + 50) : 0, fx, fy,
                   on && (fx != 1.0f || fy != 1.0f) ? ", read as 1 1 (the whole frame)" : "");
            fflush(stdout);
        }
        if (on && (fx != 1.0f || fy != 1.0f)) {
            /* the Vector2 is the copy's own (its stack); the game reads it only for this draw */
            float one = 1.0f; uint32_t u; memcpy(&u, &one, 4);
            wr32(uv, u); wr32(uv + 4, u);
            __real_f_4000793860(c);
            wr32(uv, ux); wr32(uv + 4, uy);
            return;
        }
    }
    __real_f_4000793860(c);
}
