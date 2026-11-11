/* tex_etc.c - Android port: BC texture blocks re-encoded as ETC2 / EAC blocks of the same size, for a GPU that cannot read
 * BC (Mali, PowerVR). Before this the blocks were decoded to RGBA8 (BC1: 8 times the memory, BC3 / BC7: 4 times), which on an
 * 8 GB Mali phone grew the game's Vulkan memory to about 2 GB, and every sample read 4-8 times the bytes. ETC2 and EAC are read
 * by every Vulkan GPU on Android (textureCompressionETC2). One block in, one block out, so the texture's layout is the BC one:
 *
 *   BC1 (no alpha)        -> ETC2 RGB8               8 bytes
 *   BC1 (1-bit alpha)     -> ETC2 RGB8 punch-through 8 bytes
 *   BC2, BC3, BC7         -> ETC2 RGBA8 (EAC alpha)  16 bytes
 *   BC4 unorm / snorm     -> EAC R11 unorm / snorm   8 bytes
 *   BC5 unorm / snorm     -> EAC RG11 unorm / snorm  16 bytes
 *
 * The encoder is a fast one written for this (no outside code): ETC1's individual and differential modes in both flips (no T, H
 * or planar mode), the colour of each half its average, the modifier table chosen by trying all eight; EAC with the multiplier and
 * base fitted to the block's range for each of the 16 tables. The block is decoded by tex_decode first (BC7 too), so what is
 * encoded is what the GPU would have sampled. Bit layouts: Khronos Data Format Specification 1.3, sections 21 (ETC2) and 22
 * (EAC). Big-endian 64-bit words; pixel i = x * 4 + y (column by column). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tex_decode.h"
#if defined(__ANDROID__) || defined(__linux__)
#include <pthread.h>
#endif

static const int k_etc1[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
static const int k_eac[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12}, {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10}, {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},  {-2, -5, -8, -10, 1, 4, 7, 9},  {-2, -4, -8, -10, 1, 3, 7, 9},  {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},  {-1, -2, -3, -10, 0, 1, 2, 9},  {-4, -6, -8, -9, 3, 5, 7, 8},   {-3, -5, -7, -9, 2, 4, 6, 8}};

static const uint8_t k_eac_ord[8] = {3, 2, 1, 0, 4, 5, 6, 7};      /* the indices of a table row in ascending order */
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline void put64(uint8_t *o, uint64_t v) { for (int i = 0; i < 8; i++) o[i] = (uint8_t)(v >> (56 - 8 * i)); }

/* ---------------------------------------------------------------- ETC2 RGB (ETC1 modes) */
/* The pixels of each half: k_half[flip][h][j] (flip 0: left / right 2x4, flip 1: top / bottom 4x2). */
static const uint8_t k_half[2][2][8] = {{{0, 1, 2, 3, 4, 5, 6, 7}, {8, 9, 10, 11, 12, 13, 14, 15}},
                                       {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15}}};
/* One half of a block: the best table and indices for base colour e. ptz: punch-through with the opaque bit clear (modifiers 0,
 * +b, -b; transparent pixels take index 2). A modifier moves R, G and B alike, so (clamping aside) its error for a pixel is
 * q - 2 m d + 3 m^2 with d the sum and q the sum of squares of the pixel's differences from e: the table is chosen on that,
 * the error returned is the exact one (with clamping). idx[i] gets (msb << 1) | lsb for the pixels of this half. */
static int etc_half(const uint8_t px[16][4], const uint8_t *list, const uint8_t *transp, const int e[3], int ptz, int *table, uint8_t idx[16]) {
    int d[8], pi[8], n = 0;
    for (int j = 0; j < 8; j++) {
        int i = list[j];
        if (transp[i]) { idx[i] = 2; continue; }
        d[n] = px[i][0] - e[0] + px[i][1] - e[1] + px[i][2] - e[2]; pi[n++] = i;
    }
    if (!n) { *table = 0; return 0; }
    int best = 0x7fffffff, bt = 0;
    for (int t = 0; t < 8; t++) {
        int a = ptz ? 0 : k_etc1[t][0], b = k_etc1[t][1], err = 0;
        int ca = 3 * a * a, cb = 3 * b * b;
        for (int j = 0; j < n && err < best; j++) {
            int x = 2 * d[j];
            int e0 = ca - a * x, e1 = cb - b * x, e2 = ca + a * x, e3 = cb + b * x;      /* +a, +b, -a, -b */
            int m = e0 < e1 ? e0 : e1; if (!ptz && e2 < m) m = e2; if (e3 < m) m = e3;
            err += m;
        }
        if (err < best) { best = err; bt = t; }
    }
    int a = k_etc1[bt][0], b = k_etc1[bt][1], mod[4] = {ptz ? 0 : a, b, -a, -b}, err = 0;
    for (int j = 0; j < n; j++) {
        int i = pi[j], be = 0x7fffffff, bk = 0;
        for (int k = 0; k < 4; k++) {
            if (ptz && k == 2) continue;
            int dr = px[i][0] - clamp255(e[0] + mod[k]), dg = px[i][1] - clamp255(e[1] + mod[k]), db = px[i][2] - clamp255(e[2] + mod[k]);
            int dd = dr * dr + dg * dg + db * db;
            if (dd < be) { be = dd; bk = k; }
        }
        idx[i] = (uint8_t)bk; err += be;
    }
    *table = bt;
    return err;
}
/* Quantised base colours for the two halves from wanted colours w (0..255): differential (5 bits, the second within -4..3 of the
 * first; returns 1 if that had to be forced) or individual (4 bits). */
static int etc_quant(const int w[2][3], int diff, int q[2][3]) {
    int forced = 0;
    for (int h = 0; h < 2; h++) for (int c = 0; c < 3; c++) {
        int v = w[h][c] < 0 ? 0 : w[h][c] > 255 ? 255 : w[h][c];
        q[h][c] = diff ? (v * 31 + 127) / 255 : (v * 15 + 127) / 255;
    }
    if (diff) for (int c = 0; c < 3; c++) {       /* more than -4..3 would select T, H or planar */
        int dd = q[1][c] - q[0][c];
        if (dd > 3) { int m = (q[0][c] + q[1][c] - 3 + 1) / 2; q[0][c] = m; q[1][c] = m + 3; forced = 1; }
        else if (dd < -4) { int m = (q[0][c] + q[1][c] + 4 + 1) / 2; q[0][c] = m; q[1][c] = m - 4; forced = 1; }
    }
    return forced;
}
static int etc_eval(const uint8_t px[16][4], const uint8_t *transp, int flip, int diff, int ptz, int q[2][3], int t[2], uint8_t idx[16]) {
    int e[2][3];
    for (int h = 0; h < 2; h++) for (int c = 0; c < 3; c++) e[h][c] = diff ? (q[h][c] << 3) | (q[h][c] >> 2) : q[h][c] * 17;
    return etc_half(px, k_half[flip][0], transp, e[0], ptz, &t[0], idx) + etc_half(px, k_half[flip][1], transp, e[1], ptz, &t[1], idx);
}
static uint64_t etc_pack(int flip, int diff, int pt, int opaque, int q[2][3], const int t[2], const uint8_t idx[16]) {
    uint32_t hi, lo = 0;
    if (diff) hi = (uint32_t)q[0][0] << 27 | (uint32_t)((q[1][0] - q[0][0]) & 7) << 24 | (uint32_t)q[0][1] << 19 | (uint32_t)((q[1][1] - q[0][1]) & 7) << 16 |
                   (uint32_t)q[0][2] << 11 | (uint32_t)((q[1][2] - q[0][2]) & 7) << 8;
    else hi = (uint32_t)q[0][0] << 28 | (uint32_t)q[1][0] << 24 | (uint32_t)q[0][1] << 20 | (uint32_t)q[1][1] << 16 | (uint32_t)q[0][2] << 12 | (uint32_t)q[1][2] << 8;
    hi |= (uint32_t)t[0] << 5 | (uint32_t)t[1] << 2 | (uint32_t)(pt ? opaque : diff) << 1 | (uint32_t)flip;
    for (int i = 0; i < 16; i++) lo |= (uint32_t)(idx[i] >> 1) << (16 + i) | (uint32_t)(idx[i] & 1) << i;
    return (uint64_t)hi << 32 | lo;
}
/* pt: the punch-through format (RGB8A1): a pixel with alpha < 128 becomes transparent. Both flips in differential mode, individual
 * mode where the halves are too far apart for differential, then the base colours of the best one fitted again to the
 * modifiers chosen (one round). */
static void etc_rgb_block(const uint8_t px[16][4], int pt, uint8_t out[8]) {
    uint8_t transp[16];
    int nt = 0;
    for (int i = 0; i < 16; i++) { transp[i] = (uint8_t)(pt && px[i][3] < 128); nt += transp[i]; }
    int opaque = !nt, ptz = pt && !opaque;
    int be = 0x7fffffff, bflip = 0, bdiff = 1, bq[2][3] = {{0}}, bt[2] = {0, 0};
    uint8_t bidx[16] = {0};
    for (int flip = 0; flip < 2; flip++) {
        int w[2][3];
        for (int h = 0; h < 2; h++) {
            int sum[3] = {0, 0, 0}, n = 0;
            for (int j = 0; j < 8; j++) { int i = k_half[flip][h][j]; if (!transp[i]) { sum[0] += px[i][0]; sum[1] += px[i][1]; sum[2] += px[i][2]; n++; } }
            if (!n) for (int j = 0; j < 8; j++) { int i = k_half[flip][!h][j]; if (!transp[i]) { sum[0] += px[i][0]; sum[1] += px[i][1]; sum[2] += px[i][2]; n++; } }
            for (int c = 0; c < 3; c++) w[h][c] = n ? (sum[c] + n / 2) / n : 0;
        }
        for (int diff = 1; diff >= (pt ? 1 : 0); diff--) {
            int q[2][3], t[2];
            uint8_t idx[16] = {0};
            int forced = etc_quant(w, diff, q);
            int e = etc_eval(px, transp, flip, diff, ptz, q, t, idx);
            if (e < be) { be = e; bflip = flip; bdiff = diff; memcpy(bq, q, sizeof q); memcpy(bt, t, sizeof bt); memcpy(bidx, idx, 16); }
            if (diff && !forced) break;            /* individual mode only where differential could not hold the two colours */
        }
    }
    if (be) {                                      /* refit: each half's colour = the mean of (pixel - its modifier) */
        int w[2][3];
        for (int h = 0; h < 2; h++) {
            int sum[3] = {0, 0, 0}, n = 0, a = k_etc1[bt[h]][0], b = k_etc1[bt[h]][1], mod[4] = {ptz ? 0 : a, b, -a, -b};
            for (int j = 0; j < 8; j++) { int i = k_half[bflip][h][j]; if (transp[i]) continue; for (int c = 0; c < 3; c++) sum[c] += px[i][c] - mod[bidx[i]]; n++; }
            for (int c = 0; c < 3; c++) w[h][c] = n ? (sum[c] >= 0 ? (sum[c] + n / 2) / n : -((-sum[c] + n / 2) / n)) : (bdiff ? (bq[h][c] << 3) | (bq[h][c] >> 2) : bq[h][c] * 17);
        }
        int q[2][3], t[2];
        uint8_t idx[16] = {0};
        etc_quant(w, bdiff, q);
        int e = etc_eval(px, transp, bflip, bdiff, ptz, q, t, idx);
        if (e < be) { be = e; memcpy(bq, q, sizeof q); memcpy(bt, t, sizeof bt); memcpy(bidx, idx, 16); }
    }
    put64(out, etc_pack(bflip, bdiff, pt, opaque, bq, bt, bidx));
}

/* ---------------------------------------------------------------- EAC (one channel) */
/* kind 0: 8-bit alpha of ETC2 RGBA8 (values 0..255); 1: R11 unorm (0..2047); 2: R11 snorm (-1023..1023). For each of the 16
 * tables: the multiplier that spans the block's range (and the next one up), the base that centres the table on it. */
static void eac_block(const int v[16], int kind, uint8_t out[8]) {
    int lo = v[0], hi = v[0];
    for (int i = 1; i < 16; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    int S = kind ? 8 : 1, vmin = kind == 2 ? -1023 : 0, vmax = kind == 0 ? 255 : kind == 1 ? 2047 : 1023;
    int bmin = kind == 2 ? -127 : 0, bmax = kind == 2 ? 127 : 255;
    uint64_t best = 0;
    long long be = 0x7fffffffffffffffLL;
    for (int t = (lo == hi ? 13 : 0); t < 16 && be; t++) {      /* a flat block: table 13 holds a 0 */
        int tmin = k_eac[t][3], tmax = k_eac[t][7], range = (tmax - tmin) * S;
        int m0 = (hi - lo + range / 2) / range;
        for (int m = m0; m <= m0 + 1; m++) {
            int mult = m < 1 ? 1 : m > 15 ? 15 : m;
            int mid2 = lo == hi ? 2 * lo : (hi + lo) - (tmax + tmin) * mult * S;      /* twice the wanted B0 */
            int base = kind == 0 ? (mid2 + 1) / 2 : kind == 1 ? (mid2 / 2) / 8 : (mid2 / 2 + (mid2 >= 0 ? 4 : -4)) / 8;
            base = base < bmin ? bmin : base > bmax ? bmax : base;
            int B0 = kind == 0 ? base : kind == 1 ? base * 8 + 4 : base * 8, r[8], mid[7];
            for (int j = 0; j < 8; j++) { int x = B0 + k_eac[t][k_eac_ord[j]] * mult * S; r[j] = x < vmin ? vmin : x > vmax ? vmax : x; }      /* ascending */
            for (int j = 0; j < 7; j++) mid[j] = r[j] + r[j + 1];
            long long err = 0;
            uint64_t idx = 0;
            for (int i = 0; i < 16 && err < be; i++) {      /* the nearest of the 8: a binary search on the midpoints */
                int x2 = 2 * v[i], j = x2 > mid[3] ? 4 : 0;
                j += x2 > mid[j + 1] ? 2 : 0;
                j += x2 > mid[j] ? 1 : 0;
                int d = r[j] - v[i];
                err += (long long)d * d;
                idx |= (uint64_t)k_eac_ord[j] << (45 - 3 * i);
            }
            if (err < be) { be = err; best = (uint64_t)(uint8_t)(int8_t)base << 56 | (uint64_t)mult << 52 | (uint64_t)t << 48 | idx; }
            if (m0 + 1 > 15 || lo == hi) break;
        }
    }
    put64(out, best);
}

/* ---------------------------------------------------------------- whole levels */
int tex_etc_target(int nf) {                       /* TEX_ETC_* for a BC format, 0 if it has none */
    switch (nf) {
    case 0x41: case 0x45: return TEX_ETC_RGB;
    case 0x42: case 0x46: return TEX_ETC_RGBA1;
    case 0x43: case 0x44: case 0x47: case 0x48: case 0x4D: case 0x4E: return TEX_ETC_RGBA8;
    case 0x49: return TEX_ETC_R11; case 0x4A: return TEX_ETC_R11S;
    case 0x4B: return TEX_ETC_RG11; case 0x4C: return TEX_ETC_RG11S;
    default: return 0;
    }
}
typedef struct { int nf, kind, w, h, by0, by1, rc; const uint8_t *src; uint8_t *dst; } EtcJob;
/* Rows of blocks by0..by1 of one slice: decoded (tex_decode), then encoded block by block. */
static void *etc_rows(void *arg) {
    EtcJob *j = arg;
    int kind = j->kind, w = j->w, wb = (w + 3) / 4, chan = kind >= TEX_ETC_R11;
    int bpb_in = tex_format(j->nf) ? tex_format(j->nf)->bpb : 16;
    int bpb = (kind == TEX_ETC_RGB || kind == TEX_ETC_RGBA1 || kind == TEX_ETC_R11 || kind == TEX_ETC_R11S) ? 8 : 16;
    int y0 = j->by0 * 4, rows = (j->by1 * 4 < j->h ? j->by1 * 4 : j->h) - y0;
    const uint8_t *src = j->src + (size_t)j->by0 * wb * bpb_in;
    size_t n = (size_t)w * rows;
    uint8_t *rgba = NULL;
    float *f = NULL;
    j->rc = -1;
    if (chan) { if (!(f = malloc(n * 16)) || tex_decode_rgba32f(j->nf, src, w, rows, f)) { free(f); return NULL; } }
    else if (!(rgba = malloc(n * 4)) || tex_decode_rgba8(j->nf, src, w, rows, rgba)) { free(rgba); return NULL; }
    uint8_t *o = j->dst + (size_t)j->by0 * wb * bpb;
    int snorm = kind == TEX_ETC_R11S || kind == TEX_ETC_RG11S;
    for (int by = 0; by < j->by1 - j->by0; by++)
        for (int bx = 0; bx < wb; bx++, o += bpb) {
            if (chan) {
                int v[2][16];
                for (int i = 0; i < 16; i++) {
                    int x = bx * 4 + (i >> 2), y = by * 4 + (i & 3);
                    if (x >= w) x = w - 1;
                    if (y >= rows) y = rows - 1;
                    for (int c = 0; c < 2; c++) {
                        float s = f[((size_t)y * w + x) * 4 + c];
                        float r = snorm ? s * 1023.0f : s * 2047.0f;
                        int q = (int)(r < 0 ? r - 0.5f : r + 0.5f);
                        v[c][i] = snorm ? (q < -1023 ? -1023 : q > 1023 ? 1023 : q) : (q < 0 ? 0 : q > 2047 ? 2047 : q);
                    }
                }
                eac_block(v[0], snorm ? 2 : 1, o);
                if (bpb == 16) eac_block(v[1], snorm ? 2 : 1, o + 8);
            } else {
                uint8_t px[16][4];
                int a[16];
                for (int i = 0; i < 16; i++) {
                    int x = bx * 4 + (i >> 2), y = by * 4 + (i & 3);
                    if (x >= w) x = w - 1;
                    if (y >= rows) y = rows - 1;
                    memcpy(px[i], rgba + ((size_t)y * w + x) * 4, 4);
                    a[i] = px[i][3];
                }
                if (kind == TEX_ETC_RGBA8) { eac_block(a, 0, o); etc_rgb_block(px, 0, o + 8); }
                else etc_rgb_block(px, kind == TEX_ETC_RGBA1, o);
            }
        }
    free(rgba); free(f);
    j->rc = 0;
    return NULL;
}
/* src: tightly packed BC blocks of one slice (w x h texels), dst: the ETC2 / EAC blocks in the same places (the same size). A large
 * level is shared out to up to 4 threads by rows of blocks (the calling thread takes the first share). */
int tex_transcode_etc(int nf, const void *src, int w, int h, void *dst) {
    int kind = tex_etc_target(nf);
    if (!kind || w <= 0 || h <= 0) return -1;
    int wb = (w + 3) / 4, hb = (h + 3) / 4, nt = 1;
    long long blocks = (long long)wb * hb;
#if defined(__ANDROID__) || defined(__linux__)
    {   static int lim = -1;                         /* SMO_ETC_THREADS=<1..4> (tests) */
        if (lim < 0) { const char *e = getenv("SMO_ETC_THREADS"); lim = e ? atoi(e) : 4; lim = lim < 1 ? 1 : lim > 4 ? 4 : lim; }
        nt = blocks >= 16384 ? 4 : blocks >= 4096 ? 2 : 1;      /* 512 x 512 and up: 4; 256 x 256 and up: 2 */
        if (nt > lim) nt = lim;
    }
    if (nt > hb) nt = hb;
#endif
    EtcJob job[4];
    for (int i = 0; i < nt; i++) {
        job[i].nf = nf; job[i].kind = kind; job[i].w = w; job[i].h = h; job[i].src = src; job[i].dst = dst; job[i].rc = -1;
        job[i].by0 = (int)((long long)hb * i / nt); job[i].by1 = (int)((long long)hb * (i + 1) / nt);
    }
#if defined(__ANDROID__) || defined(__linux__)
    pthread_t th[4];
    int started[4] = {0};
    for (int i = 1; i < nt; i++) started[i] = !pthread_create(&th[i], NULL, etc_rows, &job[i]);
    etc_rows(&job[0]);
    for (int i = 1; i < nt; i++) { if (started[i]) pthread_join(th[i], NULL); else etc_rows(&job[i]); }
#else
    etc_rows(&job[0]);
#endif
    for (int i = 0; i < nt; i++) if (job[i].rc) return -1;
    return 0;
}
