/* 0.4.2: the three Snow Kingdom videos (SnowRace, SnowWithShip, SnowWithoutShip) keep their index (the moov box) after the
 * picture data (ftyp mdat moov); every other video of the game has it first (ftyp moov uuid mdat). On a MediaTek phone
 * (issue 32, Infinix X6870, Android 16) Android's extractor opened SnowWithShip.mp4 from our in-memory data source with
 * 0 tracks, and the game crashed in MovieDecoderPlayer's thread when Mario walked into Shiveria Town (its big screen).
 * mp4_faststart moves the moov box in front of the first mdat, as "qt-faststart" does, and adds the moov box's size to every
 * chunk offset (stco, co64) so the samples are still found. The picture and sound bytes are not touched.
 * Returns the new buffer (and frees data) or data itself when nothing had to move or the file is not as expected. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fs_be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void fs_put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static uint64_t fs_be64(const unsigned char *p) { return (uint64_t)fs_be32(p) << 32 | fs_be32(p + 4); }
static void fs_put64(unsigned char *p, uint64_t v) { fs_put32(p, (uint32_t)(v >> 32)); fs_put32(p + 4, (uint32_t)v); }

/* one box at p (n bytes left): its size and header length; 0 when it does not fit */
static size_t fs_box(const unsigned char *p, size_t n, size_t *hdr) {
    if (n < 8) return 0;
    uint64_t sz = fs_be32(p); *hdr = 8;
    if (sz == 1) { if (n < 16) return 0; sz = fs_be64(p + 8); *hdr = 16; }
    else if (sz == 0) sz = n;                         /* to the end of the file */
    if (sz < *hdr || sz > n) return 0;
    return (size_t)sz;
}

/* add delta to every chunk offset inside the boxes of [p, p + n); returns 0 when an offset would not fit */
static int fs_patch(unsigned char *p, size_t n, uint64_t delta, int *patched) {
    size_t at = 0;
    while (at + 8 <= n) {
        size_t hdr, sz = fs_box(p + at, n - at, &hdr);
        if (!sz) return 0;
        unsigned char *b = p + at + hdr; size_t bn = sz - hdr;
        if (!memcmp(p + at + 4, "trak", 4) || !memcmp(p + at + 4, "mdia", 4) || !memcmp(p + at + 4, "minf", 4) || !memcmp(p + at + 4, "stbl", 4)) {
            if (!fs_patch(b, bn, delta, patched)) return 0;
        } else if (!memcmp(p + at + 4, "stco", 4) || !memcmp(p + at + 4, "co64", 4)) {
            int wide = p[at + 4] == 'c';
            if (bn < 8) return 0;
            uint32_t cnt = fs_be32(b + 4);
            size_t w = wide ? 8 : 4;
            if ((uint64_t)cnt * w > bn - 8) return 0;
            for (uint32_t i = 0; i < cnt; i++) {
                unsigned char *q = b + 8 + (size_t)i * w;
                if (wide) fs_put64(q, fs_be64(q) + delta);
                else { uint64_t v = (uint64_t)fs_be32(q) + delta; if (v > 0xffffffffu) return 0; fs_put32(q, (uint32_t)v); }
            }
            (*patched)++;
        }
        at += sz;
    }
    return 1;
}

static unsigned char *mp4_faststart(unsigned char *data, size_t *size, int *moved) {
    size_t n = *size, at = 0, mdat = (size_t)-1, moov = (size_t)-1, moov_sz = 0;
    *moved = 0;
    while (at + 8 <= n) {
        size_t hdr, sz = fs_box(data + at, n - at, &hdr);
        if (!sz) return data;                         /* not a box list we understand: hand it over as it is */
        if (!memcmp(data + at + 4, "mdat", 4) && mdat == (size_t)-1) mdat = at;
        if (!memcmp(data + at + 4, "moov", 4)) { moov = at; moov_sz = sz; }
        at += sz;
    }
    if (mdat == (size_t)-1 || moov == (size_t)-1 || moov < mdat) return data;      /* already moov first */
    unsigned char *out = malloc(n);
    if (!out) return data;
    memcpy(out, data, mdat);                          /* ftyp (and anything else before the picture data) */
    memcpy(out + mdat, data + moov, moov_sz);         /* the index */
    memcpy(out + mdat + moov_sz, data + mdat, moov - mdat);                         /* mdat .. up to the old moov */
    memcpy(out + mdat + moov_sz + (moov - mdat), data + moov + moov_sz, n - moov - moov_sz);   /* anything after it */
    size_t hdr; fs_box(out + mdat, moov_sz, &hdr);
    int patched = 0;
    if (!fs_patch(out + mdat + hdr, moov_sz - hdr, (uint64_t)moov_sz, &patched) || !patched) { free(out); return data; }
    free(data);
    *moved = patched;
    return out;
}
