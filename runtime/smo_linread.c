/* smo_linread.c - 0.3.7: the game's CPU reads of textures the GPU draws (Cap Kingdom's fog).
 *
 * al::getColorFromLinearTexture [main 0xa2b794], al::getF32FromLinearTextureF16 [0xa2b81c] and al::getF32FromLinearTextureF32
 * [0xa2b8e0] read one texel of a linear texture on the CPU: agl::TextureData +0 is its NVN memory pool, +8 its offset in
 * that pool, +0x4c its row stride; each maps the pool (nvnMemoryPoolMap) and reads the texel there. On the Switch the GPU
 * draws straight into that memory. Here the drawing is in a Vulkan image, and the pool memory kept what it held before, so
 * the game read zeroes. Cap Kingdom's fog surface comes from such a read (the fog's half-float wave texture): with zeroes the
 * game took the surface for flat at the wrong height and refused Mario's and Cappy's ripples in the fog.
 *
 * libsmo.so is linked with -Wl,--wrap for the three readers (tools/android/build_runtime.py). Before the game's own code
 * runs, the texture's GPU contents are asked for (nvn_front_linear_readback, once per frame per texture). On Vulkan the copy
 * is recorded without a wait and lands in the pool memory at a later read (vk_linread: the game reads what the GPU drew a
 * frame or two before; waiting at each read took the S24 from 60 to 30 fps in Cap). Found by Super Odyssey PC Recomp
 * (docs/FOG-READBACK.md, MIT; there the one texel is read at the reader's nvnMemoryPoolMap call). SMO_FOG_READBACK=0: off. */
#include <stdint.h>
#include <stdio.h>
#include "runtime.h"

void nvn_front_linear_readback(uint64_t pool, uint32_t offset);      /* nvn_gl.c */

void __real_f_4000a2b794(Ctx *c);
void __real_f_4000a2b81c(Ctx *c);
void __real_f_4000a2b8e0(Ctx *c);

static void sync_tex(Ctx *c) {
    uint64_t td = c->x[0];
    if (!td) return;
    nvn_front_linear_readback(rd64(td), rd32(td + 8));
}
void __wrap_f_4000a2b794(Ctx *c) { sync_tex(c); __real_f_4000a2b794(c); }
void __wrap_f_4000a2b81c(Ctx *c) {
    sync_tex(c);
    __real_f_4000a2b81c(c);
    static long long n, nz;                         /* the values read (s0): the first few, then a count every 2000 reads */
    float v = c->v[0].f[0];
    n++; nz += v != 0.0f;
    if (n <= 5 || n % 2000 == 0) { printf("smo_linread: fog texel read %lld: %g (%lld of %lld reads not zero)\n", n, (double)v, nz, n); }
}
void __wrap_f_4000a2b8e0(Ctx *c) { sync_tex(c); __real_f_4000a2b8e0(c); }
