/* nvn_vk_objmo.c - 0.3.13, frame generation step 4b: the motion of every object, not only the camera's (docs/FRAME_GENERATION_PLAN.md).
 * #included by nvn_vk_motion.c (which nvn_vk_draw.c includes).
 *
 * The camera motion pass (nvn_vk_motion.c) gives each pixel the motion the camera gave it, as if the world stood still. What moves
 * on its own (Mario's arms and legs, a chain chomp, enemies) then stays put in a made frame and updates at the real frame rate. Here:
 *   1. Each draw of the scene's G-buffer pass is noted when it is recorded (draw_prepare -> mo_capture, the draw -> mo_note_draw):
 *      what a replay needs (programs, vertex layout and streams, index buffer, its buffers' ring places, viewport) and a copy of
 *      every uniform buffer its vertex stage reads (c3 and up) into a keep buffer that lives one frame more.
 *   2. At the present (mv_present, after the camera motion pass) the draws are matched with the frame before: same programs and
 *      draw arguments (as Power Moon Recomp's interp.c: MIT, Copyright (c) 2026 Peiikai (LAS Recomp), Power Moon Recomp
 *      contributors, Super Odyssey PC Recomp contributors); several draws of one key are paired by their nearest own data. A
 *      buffer every draw of the pass names (the camera, the pass) is shared; the others are each object's.
 *   3. A matched draw whose own data changed is drawn again into the motion image (RG16F) with a motion vertex stage: the game's
 *      vertex code (translated again) run twice, once with the last frame's copies of its uniform buffers (the previous clip
 *      position) and once as it is (mo_vs_text). Its fragment stage reads the scene depth and writes (previous ndc - ndc) in
 *      pixels where its depth is the scene's: the object's whole motion, the camera's included, replaces the camera-only motion.
 *   Draws that cannot be replayed (a vertex stage with storage buffers, an instance divisor the pipeline cannot take) keep the
 *   camera motion. Texture reads in a vertex stage (lighting) are left out of the motion stage (they do not move a vertex).
 * SMO_VK_FG_OBJ=0 turns it off (then Mario's capsule, step 4, is used); SMO_VK_MO_MEASURE=1 also prints the matching statistics;
 * SMO_VK_MO_TOL=<d> the depth tolerance (default 4e-6). */

#define MO_MAX 1024
#define MO_KEEP (2u << 20)                         /* bytes of each of the keep buffer's 4 parts: a frame writes one, the next frame's
                                                    * replay reads it, and it is written again 4 frames later (the GPU may still be
                                                    * reading the last frame's part while this frame's is written) */
typedef struct { VkBuffer buf; uint64_t off, size; int zero; } MoBuf;
typedef struct {
    uint64_t key, objh; uint32_t count, prog_vs;
    uint64_t addr[15]; uint32_t size[15]; uint64_t h[15];      /* the vertex stage's uniform buffers c3..c17 (index n - 3) */
    int ok; Prog *prog; VStage *vs; int topo;                  /* replay */
    uint32_t nb, na; VkVertexInputBindingDescription2EXT bd[17]; VkVertexInputAttributeDescription2EXT ad[16]; VkBuffer vb[17]; VkDeviceSize vo[17];
    MoBuf cb[18]; uint32_t cbmask;
    VkBuffer ib; uint64_t iboff; int itype;                    /* itype: -1 not indexed, else the VkIndexType */
    uint32_t first, inst, base_inst; int32_t base_vertex;
    VkViewport vp; VkRect2D sc;
    int dbias; float bias[3];                                  /* the draw's depth bias: enabled, constant, clamp, slope (R.polyoff) */
    uint32_t keep_off[15];                                     /* its copies in this frame's half of the keep buffer (~0u: none) */
    uint32_t own;                                              /* the slots holding its own data (mo_classify) */
    uint32_t vmask; uint64_t vsize[17]; uint32_t vkeep_off[17];  /* bindings of GPU-written vertices (skinning): their bytes, their copies (mo_pass_ended; ~0u none) */
} MoRec;
static MoRec g_mo[2][MO_MAX]; static int g_mo_n[2], g_mo_cur, g_mo_on = -1, g_mo_classed[2], g_mo_vcopied;
static struct { int ok; Prog *prog; VStage *vs; int topo; uint32_t nb, na; VkVertexInputBindingDescription2EXT bd[17]; VkVertexInputAttributeDescription2EXT ad[16]; VkBuffer vb[17]; VkDeviceSize vo[17]; MoBuf cb[18]; uint32_t cbmask;
                uint32_t vmask; uint64_t vsize[17]; } g_mo_pend;
static VkViewport g_mo_vp; static VkRect2D g_mo_sc;          /* the viewport and scissor set last (set_dynamic_state) */
static float g_mo_campos[3], g_mo_marpos[3]; static int g_mo_marok;      /* the camera's place in the world this frame (mv_present); Mario's, for a test log */
static int64_t g_mo_frames, g_mo_draws, g_mo_matched, g_mo_same, g_mo_changed, g_mo_unmatched, g_mo_dupkeys, g_mo_over;
static uint32_t g_mo_shared_seen[15], g_mo_obj_seen[15];
static int fg_on(void);                            /* nvn_vk_motion.c, below */
static int mv_shot_frame(long long frame);          /* nvn_vk_motion.c, below */

static int mo_obj_on(void) {                       /* replays the moving objects (with frame generation) */
    static int env = -2;
    if (env == -2) { const char *e = getenv("SMO_VK_FG_OBJ"); env = e ? atoi(e) != 0 : 1; }
    return env && fg_on() && !gfx_framegen_fast();      /* 0.3.22: quality fast, the camera only */
}
static int mo_want(void) {
    if (g_mo_on < 0) { const char *e = getenv("SMO_VK_MO_MEASURE"); g_mo_on = e && atoi(e); if (g_mo_on) printf("nvn_vk_mo: measuring the G-buffer pass's draws across frames (SMO_VK_MO_MEASURE)\n"); }
    return g_mo_on || mo_obj_on();
}
static uint64_t mo_hash(const uint8_t *p, uint32_t n) {      /* 0.3.13: 8 bytes a step (byte by byte, the hashing of ~500 draws' buffers took the render thread ~150 ms a frame on a Dimensity 8300) */
    uint64_t h = 1469598103934665603ull, w; uint32_t i = 0;
    for (; i + 8 <= n; i += 8) { memcpy(&w, p + i, 8); h ^= w; h *= 1099511628211ull; h ^= h >> 29; }
    for (; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

/* ---- the keep buffer: 4 parts in turn (g_mo_part: the part of each half of the notes) */
static VkBuffer g_mo_keep; static VMem g_mo_keep_mem; static uint32_t g_mo_keep_used[4]; static int g_mo_keep_state, g_mo_part[2] = {0, 1};
#define MO_DD 2048
static struct { uint64_t addr, hash, gen; uint32_t size, off; uint8_t hashed; } g_mo_dedup[2][MO_DD];      /* each buffer of the frame once: its copy (off, ~0u none) and its hash */
/* 0.3.16: a CPU copy of the keep buffer (ordinary memory) for every CPU read. The Mali tester's 0.3.15 log: his driver offers no
 * host-visible memory that is both coherent and CPU-cached ("keep buffer in memory type 0 (flags 0x7)"), so the reads of the copies
 * (matching last frame's draws, their matrices) stayed uncached: 4 ms a frame of the render thread. The GPU reads the keep buffer, the
 * CPU reads this. */
static uint8_t *g_mo_shadow;
static const uint8_t *mo_keep_at(int half, uint32_t off) { return (g_mo_shadow ? (const uint8_t *)g_mo_shadow : (const uint8_t *)g_mo_keep_mem.map) + (size_t)g_mo_part[half] * MO_KEEP + off; }
/* ---- the vertex keep buffer: copies of GPU-written vertex streams (SMO skins Mario and many characters with compute programs
 * that write posed vertices into a buffer each frame; the draw only reads them, so the uniform data alone does not show the limbs'
 * motion). The GPU copies each such stream when the G-buffer pass ends (the compute buffers are alive then), into the same part of
 * the ring of 4 as the uniform copies; the next frame's replay reads it as the previous vertices. */
#define MO_VKEEP (8u << 20)
static VkBuffer g_mo_vkeep; static VMem g_mo_vkeep_mem; static uint32_t g_mo_vkeep_used[4]; static int g_mo_vkeep_state;
static int64_t g_mo_vcopies, g_mo_vbytes, g_mo_vfull;
/* 0.3.13: the scene depth as the G-buffer pass left it. The replay's depth check had read the depth at the present, after every later
 * pass: Mario's body never passed it (S24: his re-drawn pixels 2.5e-4..6e-4 deeper than that depth, 25-55 units; no depth bias on
 * the body), as if a later pass wrote depth over him. SMO_VK_MO_DSNAP=0: the depth at the present, as before. */
static VkImage g_mo_ds_img; static VMem g_mo_ds_mem; static VkImageView g_mo_ds_view; static int g_mo_ds_w, g_mo_ds_h, g_mo_ds_fresh; static VkFormat g_mo_ds_fmt;
static void mo_dsnap(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("SMO_VK_MO_DSNAP"); on = e ? atoi(e) != 0 : 1; }
    VTex *dv = on && g_mv_depth && !g_mv_depth->dead ? g_mv_depth->vk : NULL;
    if (!dv || !(fmt_aspect(dv->fmt) & VK_IMAGE_ASPECT_DEPTH_BIT)) return;
    if (!g_mo_ds_img || g_mo_ds_w != dv->w || g_mo_ds_h != dv->h || g_mo_ds_fmt != dv->fmt) {
        if (g_mo_ds_img) { cmd_wait_all(); vkDestroyImageView(V.device, g_mo_ds_view, NULL); vkDestroyImage(V.device, g_mo_ds_img, NULL); vmem_free(g_mo_ds_mem); g_mo_ds_img = VK_NULL_HANDLE; }
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = dv->fmt; ci.extent.width = (uint32_t)dv->w; ci.extent.height = (uint32_t)dv->h; ci.extent.depth = 1;
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(V.device, &ci, NULL, &g_mo_ds_img) != VK_SUCCESS) { g_mo_ds_img = VK_NULL_HANDLE; on = 0; return; }
        VkMemoryRequirements rq; vkGetImageMemoryRequirements(V.device, g_mo_ds_img, &rq);
        g_mo_ds_mem = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        VKC(vkBindImageMemory(V.device, g_mo_ds_img, g_mo_ds_mem.mem, g_mo_ds_mem.off));
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = g_mo_ds_img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = dv->fmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        VKC(vkCreateImageView(V.device, &vi, NULL, &g_mo_ds_view));
        g_mo_ds_w = dv->w; g_mo_ds_h = dv->h; g_mo_ds_fmt = dv->fmt;
        printf("nvn_vk_mo: the scene depth is kept as the G-buffer pass leaves it (%dx%d, format %d) for the replay's depth check\n", dv->w, dv->h, (int)dv->fmt);
    }
    vt_layout(dv, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    image_barrier(g_mo_ds_img, fmt_aspect(dv->fmt), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy cp; memset(&cp, 0, sizeof cp);
    cp.srcSubresource.aspectMask = cp.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; cp.srcSubresource.layerCount = cp.dstSubresource.layerCount = 1;
    cp.extent.width = (uint32_t)dv->w; cp.extent.height = (uint32_t)dv->h; cp.extent.depth = 1;
    vkCmdCopyImage(R.cmd, dv->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_mo_ds_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    image_barrier(g_mo_ds_img, fmt_aspect(dv->fmt), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g_mo_ds_fresh = 1;
}
static void mo_pass_ended_(void) {                 /* pass_end, after vkCmdEndRendering (render thread) */
    if (!g_mo_in_gbuf || !mo_obj_on()) return;
    mo_dsnap();
    int c = g_mo_cur, part = g_mo_part[c];
    if (g_mo_vcopied >= g_mo_n[c]) return;
    if (g_mo_vkeep_state == 0) {
        g_mo_vkeep_state = -1;
        g_mo_vkeep = vbuf_new(4ull * MO_VKEEP, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_mo_vkeep_mem);
        if (g_mo_vkeep) g_mo_vkeep_state = 1; else printf("nvn_vk_mo: no vertex keep buffer: GPU-skinned objects keep the camera motion\n");
    }
    if (g_mo_vkeep_state < 0) { g_mo_vcopied = g_mo_n[c]; return; }
    int any = 0;
    for (int i = g_mo_vcopied; i < g_mo_n[c]; i++) {
        MoRec *r = &g_mo[c][i];
        if (!r->ok || !r->vmask) continue;
        for (uint32_t k = 0; k < r->nb; k++) {
            if (!(r->vmask >> k & 1)) continue;
            uint32_t off = ~0u;
            for (int i2 = 0; i2 < i && off == ~0u; i2++) {      /* the same stream copied for an earlier draw this frame */
                const MoRec *o = &g_mo[c][i2];
                for (uint32_t k2 = 0; k2 < o->nb; k2++) if ((o->vmask >> k2 & 1) && o->vkeep_off[k2] != ~0u && o->vb[k2] == r->vb[k] && o->vo[k2] == r->vo[k] && o->vsize[k2] >= r->vsize[k]) { off = o->vkeep_off[k2]; break; }
            }
            if (off == ~0u) {
                uint32_t o = (g_mo_vkeep_used[part] + 255u) & ~255u;
                if (o + r->vsize[k] > MO_VKEEP) { g_mo_vfull++; continue; }
                if (!any) {                        /* the vertices written by compute / read by the pass, before the copies read them */
                    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
                    any = 1;
                }
                VkBufferCopy bc = {r->vo[k], (VkDeviceSize)part * MO_VKEEP + o, r->vsize[k]};
                vkCmdCopyBuffer(R.cmd, r->vb[k], g_mo_vkeep, 1, &bc);
                g_mo_vkeep_used[part] = o + (uint32_t)r->vsize[k]; off = o; g_mo_vcopies++; g_mo_vbytes += (int64_t)r->vsize[k];
            }
            r->vkeep_off[k] = off;
        }
    }
    if (any) {                                     /* the copies before a later vertex read of them (the next frame's replay) */
        VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    }
    g_mo_vcopied = g_mo_n[c];
}
/* A buffer of this frame's notes, once a frame however many draws name it: copied into the keep buffer when `copy` (*off: its place,
 * ~0u none) and hashed; returns the hash. 0.3.13: the big shared buffers (camera, scene) had been hashed again for each of ~500 draws. */
/* 0.3.14: the writes into guest memory the render thread makes while it runs the frame (UpdateUniformBuffer, buffer copies and clears,
 * compute copy-backs: host_wrote). The game writes each object's constants with UpdateUniformBuffer into the same place before each draw,
 * so a buffer copied once a frame (the first 0.3.14 test) gave every later draw the first one's data: the same before and now, no
 * motion of their own (the user saw Mario and the enemies at 30 again, the camera smooth). A copy is now taken again when a write since
 * it overlaps its range; the big shared buffers, not written during the frame, are still copied once. */
#define MO_WL 1024
static struct { uint64_t lo, hi; } g_mo_wl[MO_WL];
static uint64_t g_mo_wgen;
static int64_t g_mo_recopies;
static void mo_host_wrote(uint64_t addr, uint64_t size) {
    g_mo_wl[g_mo_wgen & (MO_WL - 1)].lo = addr; g_mo_wl[g_mo_wgen & (MO_WL - 1)].hi = addr + size; g_mo_wgen++;
}
/* 0.3.15: the keep buffer in CPU-cached memory when the GPU has it. The Mali tester's 0.3.14 log: the render thread spent 5-14 ms a
 * frame on per-object motion (S24: 0.6 ms), most of its frame. vbuf_new takes the first host-visible coherent memory type, on Arm's
 * driver an uncached (write-combined) one: the copies were read back by the CPU (their hash, the matching of last frame's draws) at
 * uncached speed. The hash is also taken from the game's own memory now (the same bytes, just copied). */
static VkBuffer mo_keep_new(uint64_t size, VkFlags usage, VMem *mem) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkBuffer b; VkMemoryRequirements rq;
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKC(vkCreateBuffer(V.device, &bi, NULL, &b));
    vkGetBufferMemoryRequirements(V.device, b, &rq);
    VkFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cached = want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    int has = 0;
    for (uint32_t i = 0; i < D.mp.memoryTypeCount; i++) if ((rq.memoryTypeBits >> i & 1) && (D.mp.memoryTypes[i].propertyFlags & cached) == cached) has = 1;
    *mem = vmem_alloc(rq, has ? cached : want, 1);
    VKC(vkBindBufferMemory(V.device, b, mem->mem, mem->off));
    {   uint32_t t = 32; for (uint32_t i = 0; i < D.mp.memoryTypeCount && t == 32; i++) if ((rq.memoryTypeBits >> i & 1) && (D.mp.memoryTypes[i].propertyFlags & (has ? cached : want)) == (has ? cached : want)) t = i;
        printf("nvn_vk_mo: keep buffer in memory type %u (flags 0x%x): %s\n", t, t < 32 ? (unsigned)D.mp.memoryTypes[t].propertyFlags : 0u, has ? "CPU-cached" : "no CPU-cached type offered, uncached"); }
    return b;
}
static uint64_t mo_slot(int half, uint64_t addr, uint32_t size, int copy, uint32_t *off) {
    *off = ~0u;
    if (g_mo_keep_state == 0) {
        g_mo_keep_state = -1;
        g_mo_keep = mo_keep_new(4ull * MO_KEEP, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_mo_keep_mem);
        if (g_mo_keep && g_mo_keep_mem.map) { g_mo_keep_state = 1; g_mo_shadow = malloc(4ull * MO_KEEP); } else printf("nvn_vk_mo: no keep buffer: per-object motion off\n");
    }
    if (!addr || !size) return 0;
    if (size > 65536) size = 65536;
    unsigned slot = (unsigned)(((addr >> 4) ^ (addr >> 17) ^ size) * 2654435761u) & (MO_DD - 1), e = ~0u;
    for (unsigned k = 0; k < 16; k++) { unsigned s = (slot + k) & (MO_DD - 1); if (g_mo_dedup[half][s].addr == addr && g_mo_dedup[half][s].size == size) { e = s; break; } if (!g_mo_dedup[half][s].addr) { e = s; g_mo_dedup[half][s].addr = addr; g_mo_dedup[half][s].size = size; g_mo_dedup[half][s].off = ~0u; g_mo_dedup[half][s].hashed = 0; g_mo_dedup[half][s].gen = g_mo_wgen; break; } }
    if (e != ~0u && g_mo_dedup[half][e].gen != g_mo_wgen) {      /* written since its copy? (each logged write looked at once per entry) */
        uint64_t g = g_mo_dedup[half][e].gen, hi = addr + size; int changed = g_mo_wgen - g > MO_WL;
        for (; !changed && g < g_mo_wgen; g++) if (g_mo_wl[g & (MO_WL - 1)].lo < hi && g_mo_wl[g & (MO_WL - 1)].hi > addr) changed = 1;
        g_mo_dedup[half][e].gen = g_mo_wgen;
        if (changed) { g_mo_dedup[half][e].off = ~0u; g_mo_dedup[half][e].hashed = 0; g_mo_recopies++; }      /* a new copy and hash below (the old copy stays for the draws before) */
    }
    if (copy && g_mo_keep_state > 0 && (e == ~0u || g_mo_dedup[half][e].off == ~0u)) {
        uint64_t al = V.props.limits.minUniformBufferOffsetAlignment > V.props.limits.minStorageBufferOffsetAlignment ? V.props.limits.minUniformBufferOffsetAlignment : V.props.limits.minStorageBufferOffsetAlignment;
        if (al < 16) al = 16;
        int part = g_mo_part[half];
        uint32_t o = (uint32_t)((g_mo_keep_used[part] + al - 1) & ~(al - 1));
        if (o + size <= MO_KEEP) {
            memcpy((uint8_t *)g_mo_keep_mem.map + (size_t)part * MO_KEEP + o, (const void *)(uintptr_t)addr, size);
            if (g_mo_shadow) memcpy(g_mo_shadow + (size_t)part * MO_KEEP + o, (const void *)(uintptr_t)addr, size);
            g_mo_keep_used[part] = o + size;
            if (e != ~0u) g_mo_dedup[half][e].off = o; else *off = o;
        }
    }
    if (e == ~0u) return mo_hash((const uint8_t *)(uintptr_t)addr, size);      /* (the table is full here: no reuse) */
    if (copy) *off = g_mo_dedup[half][e].off;
    if (!g_mo_dedup[half][e].hashed) {
        g_mo_dedup[half][e].hash = mo_hash((const uint8_t *)(uintptr_t)addr, size);
        g_mo_dedup[half][e].hashed = 1;
    }
    return g_mo_dedup[half][e].hash;
}

/* ---- noting the draws (render thread) */
/* draw_prepare, once the draw's pipeline is there: what a replay needs (the vertex stage's buffers by constant buffer number) */
static void mo_capture(int topo, Prog *prog, VStage *vs, const MoBuf *cb, uint32_t cbmask, uint32_t nb, uint32_t na, const VkVertexInputBindingDescription2EXT *bd,
                       const VkVertexInputAttributeDescription2EXT *ad, const VkBuffer *vb, const VkDeviceSize *vo, const uint64_t *vsz) {
    g_mo_pend.ok = 0;
    if (!g_mo_in_gbuf || !mo_obj_on() || !vs || vs->ssbos || nb > 17 || na > 16) return;
    g_mo_pend.prog = prog; g_mo_pend.vs = vs; g_mo_pend.topo = topo; g_mo_pend.nb = nb; g_mo_pend.na = na; g_mo_pend.cbmask = cbmask;
    memcpy(g_mo_pend.bd, bd, sizeof bd[0] * nb); memcpy(g_mo_pend.ad, ad, sizeof ad[0] * na); memcpy(g_mo_pend.vb, vb, sizeof vb[0] * nb); memcpy(g_mo_pend.vo, vo, sizeof vo[0] * nb);
    memcpy(g_mo_pend.cb, cb, sizeof g_mo_pend.cb);
    g_mo_pend.vmask = 0;                           /* bindings read from a compute buffer of this batch: vertices the GPU wrote this frame (skinning) */
    for (uint32_t k = 0; k < nb; k++) { g_mo_pend.vsize[k] = vsz[k]; if (vb[k] != R.ring && vb[k] != R.zero && vsz[k] && vsz[k] <= (4u << 20)) g_mo_pend.vmask |= 1u << k; }
    if (!D.has_vids) for (uint32_t i = 0; i < nb; i++) if (bd[i].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE && bd[i].divisor > 1) return;      /* (the pipeline would need the divisor extension) */
    g_mo_pend.ok = 1;
}
/* after the draw's command: its key and own data; ib: the index buffer as bound (itype -1: not indexed) */
static void mo_note_draw_(unsigned prim, uint32_t count, uint32_t first, int32_t base_vertex, uint32_t inst, uint32_t base_inst, VkBuffer ib, uint64_t iboff, int itype) {
    if (!mo_want() || !g_mo_in_gbuf || !R.prog[0]) { g_mo_pend.ok = 0; return; }
    int c = g_mo_cur;
    if (g_mo_n[c] >= MO_MAX) { g_mo_over++; g_mo_pend.ok = 0; return; }
    MoRec *d = &g_mo[c][g_mo_n[c]++];
    uint64_t k = 1469598103934665603ull;
    uint64_t parts[7] = {(uint64_t)R.prog[0]->serial, R.prog[1] ? (uint64_t)R.prog[1]->serial : ~0ull, prim, count, first, (uint64_t)(int64_t)base_vertex, inst};
    for (int i = 0; i < 7; i++) { k ^= parts[i]; k *= 1099511628211ull; }
    d->key = k; d->count = count; d->prog_vs = (uint32_t)R.prog[0]->serial; d->objh = 0;
    int keep = mo_obj_on() && g_mo_pend.ok;
    for (int n = 0; n < 15; n++) {
        d->addr[n] = R.ubo[0][n].addr; d->size[n] = (uint32_t)R.ubo[0][n].size; d->h[n] = 0; d->keep_off[n] = ~0u;
        if (g_mo_pend.ok && !(g_mo_pend.cbmask >> (n + 3) & 1)) d->addr[n] = 0;      /* a binding its vertex stage does not read (left from another draw) */
        if (!d->addr[n] || !d->size[n]) continue;
        uint32_t sz = d->size[n] < 65536 ? d->size[n] : 65536;
        d->h[n] = mo_slot(c, d->addr[n], sz, keep, &d->keep_off[n]);
    }
    d->ok = 0;
    if (g_mo_pend.ok && g_mo_pend.vs) {
        d->ok = 1; d->prog = g_mo_pend.prog; d->vs = g_mo_pend.vs; d->topo = g_mo_pend.topo; d->nb = g_mo_pend.nb; d->na = g_mo_pend.na; d->cbmask = g_mo_pend.cbmask;
        memcpy(d->bd, g_mo_pend.bd, sizeof d->bd); memcpy(d->ad, g_mo_pend.ad, sizeof d->ad); memcpy(d->vb, g_mo_pend.vb, sizeof d->vb); memcpy(d->vo, g_mo_pend.vo, sizeof d->vo); memcpy(d->cb, g_mo_pend.cb, sizeof d->cb);
        d->ib = ib; d->iboff = iboff; d->itype = itype; d->first = first; d->inst = inst ? inst : 1; d->base_inst = base_inst; d->base_vertex = base_vertex;
        d->vp = g_mo_vp; d->sc = g_mo_sc;
        d->dbias = (R.poly[3] >> (R.poly[2] < 3 ? R.poly[2] : 2)) & 1; d->bias[0] = R.polyoff[1]; d->bias[1] = R.polyoff[2]; d->bias[2] = R.polyoff[0];
        d->vmask = g_mo_pend.vmask; memcpy(d->vsize, g_mo_pend.vsize, sizeof d->vsize); memset(d->vkeep_off, 0xff, sizeof d->vkeep_off);
        for (int n = 3; n < 18; n++) if ((d->cbmask >> n & 1) && d->addr[n - 3] && d->keep_off[n - 3] == ~0u) d->ok = 0;      /* (the keep buffer was full) */
    }
    g_mo_pend.ok = 0;
}

/* ---- matching (at the present) */
/* Which slots hold each draw's own data: a slot's buffer is shared where it is the one most draws of the pass name in that slot
 * (the camera, the pass). 0.3.13: "every draw names it" was too strict: with all ~500 G-buffer draws noted, a few (the sky?)
 * have a camera buffer of their own, the camera slot counted as every object's own, and every object "changed" whenever the
 * camera moved (248 draws drawn again a frame while running, 3.1 ms of GPU on the S24). */
static void mo_classify(int c) {
    if (g_mo_classed[c]) return;
    g_mo_classed[c] = 1;
    int n = g_mo_n[c];
    uint64_t maj[15];
    for (int s = 0; s < 15; s++) {                 /* the majority buffer of each slot (Boyer-Moore vote, then counted) */
        uint64_t cand = 0; int cnt = 0, used = 0, of = 0;
        for (int i = 0; i < n; i++) { uint64_t a = g_mo[c][i].addr[s]; if (!a) continue; used++; if (!cnt) { cand = a; cnt = 1; } else if (a == cand) cnt++; else cnt--; }
        for (int i = 0; i < n; i++) if (g_mo[c][i].addr[s] && g_mo[c][i].addr[s] == cand) of++;
        maj[s] = used > 1 && of * 2 > used ? cand : 0;
        if (used > 1) { if (maj[s]) g_mo_shared_seen[s]++; else g_mo_obj_seen[s]++; }
    }
    for (int i = 0; i < n; i++) {
        MoRec *d = &g_mo[c][i]; uint64_t h = 0; d->own = 0;
        for (int s = 0; s < 15; s++) if (d->addr[s] && d->addr[s] != maj[s]) { d->own |= 1u << s; h = h * 31 + d->h[s]; }
        d->objh = h;
    }
}
static float mo_dist(const MoRec *a, int ah, const MoRec *b, int bh) {      /* the first 16 words of their first own buffer, as floats */
    for (int s = 0; s < 15; s++) {
        if (!(a->own >> s & 1) || !(b->own >> s & 1) || a->keep_off[s] == ~0u || b->keep_off[s] == ~0u) continue;
        const float *x = (const float *)mo_keep_at(ah, a->keep_off[s]), *y = (const float *)mo_keep_at(bh, b->keep_off[s]);
        uint32_t n = (a->size[s] < b->size[s] ? a->size[s] : b->size[s]) / 4; if (n > 16) n = 16;
        float d = 0; for (uint32_t i = 0; i < n; i++) { float t = x[i] - y[i]; d += t == t ? (t < 0 ? -t : t) : 1e9f; }
        return d;
    }
    return 0;
}
static int mo_match(int c, int i, int *same) {     /* the last frame's draw for draw i of this frame, or -1 */
    int p = c ^ 1, best = -1; float bd = 0;
    *same = 0;
    for (int j = 0; j < g_mo_n[p]; j++) {
        if (g_mo[p][j].key != g_mo[c][i].key) continue;
        if (g_mo[p][j].objh == g_mo[c][i].objh) { *same = 1; return j; }
        float d = mo_dist(&g_mo[c][i], c, &g_mo[p][j], p);
        if (best < 0 || d < bd) { best = j; bd = d; }
    }
    return best;
}

/* ---- the motion vertex stage: the translated vertex code run twice (text transform of the translator's GLSL) */
typedef struct { char *s; size_t n, cap; int bad; } MoTxt;
static void mo_put(MoTxt *t, const char *p, size_t n) {
    if (t->bad) return;
    if (t->n + n + 1 > t->cap) { size_t c = (t->cap ? t->cap * 2 : 65536); while (c < t->n + n + 1) c *= 2; char *q = realloc(t->s, c); if (!q) { t->bad = 1; return; } t->s = q; t->cap = c; }
    memcpy(t->s + t->n, p, n); t->n += n; t->s[t->n] = 0;
}
static void mo_puts(MoTxt *t, const char *p) { mo_put(t, p, strlen(p)); }
static int mo_idch(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
/* body text with the previous-frame buffers (prev = 1: cN_r( / cN[ -> pN_r( / pN[ for N >= 3, gl_Position -> mo_pp, gl_PointSize ->
 * mo_pps, in_attrL -> mo_paL for the GPU-written inputs, g_mo_ploc) and every statement that reads a texture with its value replaced
 * by zeros of its type ("<type> <name> = ...mx_t...;") */
static uint8_t g_mo_ploc[16];                      /* location of input L's copy from last frame's vertices (0xff: none) */
static int mo_body(MoTxt *o, const char *b, size_t n, int prev) {
    size_t i = 0;
    while (i < n) {
        if (!strncmp(b + i, "mx_t", 4) && (i == 0 || !mo_idch(b[i - 1]))) {      /* a texture read: back to "<type> <name> = ", on to ';' */
            size_t st = o->n, eq = 0;
            while (st > 0 && o->s[st - 1] != ';' && o->s[st - 1] != '{' && o->s[st - 1] != '}') st--;
            for (size_t q = st; q + 3 <= o->n; q++) if (!strncmp(o->s + q, " = ", 3)) { eq = q; break; }
            if (!eq) return 0;
            size_t ts = st; while (ts < eq && (o->s[ts] == ' ' || o->s[ts] == '\n')) ts++;
            size_t te = ts; while (te < eq && mo_idch(o->s[te])) te++;
            char type[32]; size_t tl = te - ts; if (!tl || tl >= sizeof type) return 0;
            memcpy(type, o->s + ts, tl); type[tl] = 0;
            if (strcmp(type, "vec4") && strcmp(type, "ivec4") && strcmp(type, "uvec4") && strcmp(type, "float") && strcmp(type, "vec2") && strcmp(type, "vec3") && strcmp(type, "ivec2") && strcmp(type, "ivec3") && strcmp(type, "int") && strcmp(type, "uint")) return 0;
            o->n = eq + 3; o->s[o->n] = 0;
            mo_puts(o, type); mo_puts(o, "(0)");
            while (i < n && b[i] != ';') i++;
            continue;
        }
        if (prev) {
            if (!strncmp(b + i, "in_attr", 7) && (i == 0 || !mo_idch(b[i - 1])) && i + 7 < n && b[i + 7] >= '0' && b[i + 7] <= '9') {      /* a GPU-written input: last frame's copy */
                size_t q = i + 7; unsigned L = 0; while (q < n && b[q] >= '0' && b[q] <= '9') { L = L * 10 + (unsigned)(b[q] - '0'); q++; }
                if (L < 16 && g_mo_ploc[L] != 0xff) { char t[16]; snprintf(t, sizeof t, "mo_pa%u", L); mo_puts(o, t); i = q; continue; }
            }
            if (!strncmp(b + i, "gl_Position", 11)) { mo_puts(o, "mo_pp"); i += 11; continue; }
            if (!strncmp(b + i, "gl_PointSize", 12)) { mo_puts(o, "mo_pps"); i += 12; continue; }
            if (b[i] == 'c' && (i == 0 || !mo_idch(b[i - 1])) && i + 1 < n && b[i + 1] >= '0' && b[i + 1] <= '9') {
                size_t q = i + 1; unsigned num = 0; while (q < n && b[q] >= '0' && b[q] <= '9') { num = num * 10 + (unsigned)(b[q] - '0'); q++; }
                if (num >= 3 && q < n && (b[q] == '[' || (q + 2 < n && b[q] == '_' && b[q + 1] == 'r' && b[q + 2] == '('))) { char t[16]; snprintf(t, sizeof t, "p%u", num); mo_puts(o, t); i = q; continue; }
            }
        }
        mo_put(o, b + i, 1); i++;
    }
    return !o->bad;
}
static char *mo_vs_text(const char *g) {
    const char *m = strstr(g, "\nvoid main() {");
    const char *e = strrchr(g, '}');
    if (!m || !e || e < m) return NULL;
    MoTxt o; memset(&o, 0, sizeof o);
    /* the declarations, line by line */
    for (const char *l = g; l < m + 1; ) {
        const char *le = memchr(l, '\n', (size_t)(m + 1 - l)); size_t ln = le ? (size_t)(le - l) + 1 : (size_t)(m + 1 - l);
        char line[512]; size_t cl = ln < sizeof line - 1 ? ln : sizeof line - 1; memcpy(line, l, cl); line[cl] = 0;
        const char *ob = strstr(line, ") out ");
        if (!strncmp(line, "layout(location = ", 18) && ob) mo_puts(&o, ob + 6);                    /* an output of the game's: a plain global now */
        else if (!strncmp(line, "layout(set = 1", 14)) { }                                        /* a texture or image: not read any more */
        else {
            mo_put(&o, l, ln);
            unsigned num; char tail;
            unsigned L;
            if (sscanf(line, "layout(location = %u) in ", &L) == 1 && strstr(line, ") in ") && L < 16 && g_mo_ploc[L] != 0xff) {      /* its copy from last frame's vertices */
                const char *ty = strstr(line, ") in ") + 5, *nm = strstr(line, "in_attr");
                if (nm && nm > ty) { char dl[160]; snprintf(dl, sizeof dl, "layout(location = %u) in %.*smo_pa%u;\n", (unsigned)g_mo_ploc[L], (int)(nm - ty), ty, L); mo_puts(&o, dl); }
            }
            const char *cbp = strstr(line, "uniform CB"); if (!cbp) cbp = strstr(line, "buffer CB");
            const char *rp = !strncmp(line, "uint c", 6) ? line + 5 : NULL;
            if ((cbp && sscanf(strstr(cbp, "CB") + 2, "%u%c", &num, &tail) == 2 && num >= 3) || (rp && sscanf(rp + 1, "%u%c", &num, &tail) == 2 && tail == '_' && num >= 3)) {
                /* the last frame's copy of buffer N: binding 100 + N, names PCB<N>, p<N>_v, p<N>_r, p<N> */
                char cp[600]; size_t w = 0; const char *s = line;
                char a[16], bnum[32]; snprintf(a, sizeof a, "%u", num);
                while (*s && w < sizeof cp - 16) {
                    if (!strncmp(s, "binding = ", 10)) { unsigned bv = 0; const char *q = s + 10; while (*q >= '0' && *q <= '9') bv = bv * 10 + (unsigned)(*q++ - '0'); w += (size_t)snprintf(cp + w, sizeof cp - w, "binding = %u", 100 + bv); s = q; continue; }
                    if (!strncmp(s, "CB", 2) && !strncmp(s + 2, a, strlen(a)) && !mo_idch(s[2 + strlen(a)])) { w += (size_t)snprintf(cp + w, sizeof cp - w, "PCB%s", a); s += 2 + strlen(a); continue; }
                    if (*s == 'c' && (s == line || !mo_idch(s[-1])) && !strncmp(s + 1, a, strlen(a)) && !(s[1 + strlen(a)] >= '0' && s[1 + strlen(a)] <= '9')) { cp[w++] = 'p'; s++; continue; }
                    cp[w++] = *s++;
                }
                cp[w] = 0; (void)bnum;
                mo_puts(&o, cp);
            }
        }
        l += ln;
    }
    mo_puts(&o, "vec4 mo_pp; float mo_pps;\nlayout(location = 0) out vec4 mo_cur;\nlayout(location = 1) out vec4 mo_prev;\nvoid mo_main_prev() {");
    const char *b0 = m + strlen("\nvoid main() {");
    if (!mo_body(&o, b0, (size_t)(e - b0), 1)) { free(o.s); return NULL; }
    mo_puts(&o, "}\nvoid mo_main_cur() {");
    if (!mo_body(&o, b0, (size_t)(e - b0), 0)) { free(o.s); return NULL; }
    mo_puts(&o, "}\nvoid main() {\n mo_main_prev(); vec4 pp = mo_pp; mo_main_cur(); mo_prev = pp; mo_cur = gl_Position;\n}\n");
    if (o.bad) { free(o.s); return NULL; }
    return o.s;
}
typedef struct { VStage *vs; uint8_t ploc[16]; VkShaderModule mod; int state; } MoVs;
static MoVs g_mo_vs[512]; static int g_mo_nvs; static int64_t g_mo_vs_failed;
static VkShaderModule mo_vs_get(Prog *prog, VStage *vs, const uint8_t ploc[16]) {      /* ploc: where each GPU-written input's copy is read (0xff: none) */
    for (int i = 0; i < g_mo_nvs; i++) if (g_mo_vs[i].vs == vs && !memcmp(g_mo_vs[i].ploc, ploc, 16)) return g_mo_vs[i].mod;
    if (g_mo_nvs >= 512) return VK_NULL_HANDLE;
    MoVs *e = &g_mo_vs[g_mo_nvs++];
    e->vs = vs; memcpy(e->ploc, ploc, 16); e->mod = VK_NULL_HANDLE;
    memcpy(g_mo_ploc, ploc, 16);
    SSrc src;
    if (!ssrc_of(&src, prog, 0)) { g_mo_vs_failed++; return VK_NULL_HANDLE; }
    MxIn in; MxOut out;
    memset(&in, 0, sizeof in); memset(&out, 0, sizeof out);
    in.stage = 0; in.code = src.code; in.code_len = src.code_len; in.cdata = src.cdata; in.cdata_size = src.cdata ? src.cdata_size : 0;
    in.int_attrs = vs->key[0]; in.sint_attrs = vs->key[1]; in.int_tex = vs->key[2]; in.sint_tex = vs->key[3];
    in.target = 1; in.binding_base = 0; in.ssbo_base = 20; in.tex_base = 0; in.image_base = 32; in.wmask_binding = 38;
    if (!mx_translate(&in, &out)) { g_mo_vs_failed++; return VK_NULL_HANDLE; }
    char *t = mo_vs_text(out.glsl);
    free(out.glsl);
    if (!t) { static int nl; if (nl++ < 10) printf("nvn_vk_mo: program #%d: the motion vertex stage could not be written (a texture read it does not know)\n", prog->serial); g_mo_vs_failed++; return VK_NULL_HANDLE; }
    char what[64]; uint64_t h;
    snprintf(what, sizeof what, "program #%d motion vertex stage", prog->serial);
    e->mod = module_from_glsl(0, t, what, &h, NULL);
    free(t);
    if (!e->mod) g_mo_vs_failed++;
    return e->mod;
}

/* ---- the replay */
static VkDescriptorSetLayout g_mo_dl; static VkPipelineLayout g_mo_pl; static VkShaderModule g_mo_fs; static VkDescriptorPool g_mo_pool[NSLOT]; static uint64_t g_mo_pool_cb[NSLOT];
static int g_mo_state;
typedef struct { VkShaderModule mod; int topo; uint32_t nb, na; uint8_t rate[32]; uint8_t bind[32]; int32_t fmt[32]; uint32_t off[32]; uint8_t loc[32]; VkPipeline p; } MoPipe;
static MoPipe g_mo_pipes[512]; static int g_mo_npipes;
static int64_t g_mo_replayed, g_mo_cand, g_mo_nopipe, g_mo_rframes, g_mo_noprev, g_mo_dupskip;
static int64_t g_mo_cpu_ns;                         /* the render thread's time in per-object motion (the wrappers at the end of this file) */
/* 0.3.22, A TIME BUDGET FOR THE OBJECTS DRAWN AGAIN: at most 64 a frame (0.3.20) cost the S24 ~2.4 ms of GPU and 0.7 ms of the
 * render thread, a slower phone much more (the Helio G99 tester, 0.3.19: per-object motion 5.7-6.7 ms of the render thread). Now
 * the number follows what the replay costs: its render-thread time plus its GPU time (fgt 7-8, read back a few frames later),
 * averaged; every 30 frames a cost over the budget takes a quarter off the number (at least 8 stay: Mario and what is next to
 * him, nearest first), one under 60% of it adds 4 back (at most 64), only while the number was what limited the frame.
 * SMO_VK_MO_BUDGET_US=<us> (default 3000; 0: no budget, the fixed number as before). */
static int g_mo_cap_dyn = 64, g_mo_last_taken;
static int64_t g_mo_rep_ns_last;
static double g_mo_cost_avg = -1;
static int mo_init(void) {
    if (g_mo_state) return g_mo_state > 0;
    g_mo_state = -1;
    static const char fsg[] =
        "#version 460\n"
        "layout(location = 0) in vec4 mo_cur;\n"
        "layout(location = 1) in vec4 mo_prev;\n"
        "layout(set = 0, binding = 200) uniform sampler2D mo_depth;\n"
        "layout(push_constant) uniform MOPC { layout(offset = 16) vec4 sz; } pc;\n"      /* image width, height, depth tolerance */
        "layout(location = 0) out vec4 o;\n"
        "void main() {\n"
        "    float d = texelFetch(mo_depth, ivec2(gl_FragCoord.xy), 0).r;\n"
        "    if (abs(gl_FragCoord.z - d) > pc.sz.z || mo_cur.w <= 1e-6 || mo_prev.w <= 1e-6) discard;\n"      /* only where this surface is the one the scene shows */
        "    vec2 mv = (mo_prev.xy / mo_prev.w - mo_cur.xy / mo_cur.w) * pc.sz.xy * vec2(0.5, -0.5);\n"
        "    o = pc.sz.w > 0.0 ? vec4(pc.sz.w, (gl_FragCoord.z - d) * 1e5, 0.0, 0.0) : vec4(clamp(mv, -pc.sz.xy, pc.sz.xy), 0.0, 0.0);\n"      /* sz.w: SMO_VK_MO_MARK (test), a marker value and the depth difference x 1e5 instead of the motion */
        "}\n";
    uint64_t h;
    g_mo_fs = module_from_glsl(1, fsg, "motion objects fragment", &h, NULL);
    if (!g_mo_fs) { printf("nvn_vk_mo: the fragment stage could not be made: per-object motion off\n"); return 0; }
    VkDescriptorSetLayoutBinding b[37]; int nbd = 0;
    for (unsigned n = 0; n < 18; n++) { VkDescriptorSetLayoutBinding x = {n, CB_DT, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}; b[nbd++] = x; VkDescriptorSetLayoutBinding y = {100 + n, CB_DT, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}; b[nbd++] = y; }
    { VkDescriptorSetLayoutBinding z = {200, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}; b[nbd++] = z; }
    VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = (uint32_t)nbd; li.pBindings = b;
    if (vkCreateDescriptorSetLayout(V.device, &li, NULL, &g_mo_dl) != VK_SUCCESS) return 0;
    VkPushConstantRange pr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &g_mo_dl; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
    if (vkCreatePipelineLayout(V.device, &pli, NULL, &g_mo_pl) != VK_SUCCESS) return 0;
    VkDescriptorPoolSize ps[2] = {{CB_DT, 256 * 36}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 256; pi.poolSizeCount = 2; pi.pPoolSizes = ps;
    for (int i = 0; i < NSLOT; i++) if (vkCreateDescriptorPool(V.device, &pi, NULL, &g_mo_pool[i]) != VK_SUCCESS) return 0;
    g_mo_state = 1;
    printf("nvn_vk_mo: per-object motion ready: moving objects of the G-buffer pass are drawn again into the motion image (SMO_VK_FG_OBJ=0: off)\n");
    return 1;
}
static VkPipeline mo_pipe_get(VkShaderModule mod, int topo, uint32_t nb, const VkVertexInputBindingDescription2EXT *bd, uint32_t na, const VkVertexInputAttributeDescription2EXT *ad) {      /* 0.3.13: the vertex layout given (the draw's and the copies of its GPU-written streams) */
    MoPipe k; memset(&k, 0, sizeof k);
    k.mod = mod; k.topo = topo;
    if (!D.has_vids) {
        k.nb = nb; k.na = na;
        for (uint32_t i = 0; i < nb; i++) k.rate[i] = bd[i].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? 1 : 0;
        for (uint32_t i = 0; i < na; i++) { k.bind[i] = (uint8_t)ad[i].binding; k.fmt[i] = (int32_t)ad[i].format; k.off[i] = ad[i].offset; k.loc[i] = (uint8_t)ad[i].location; }
    }
    for (int i = 0; i < g_mo_npipes; i++) { MoPipe *p = &g_mo_pipes[i]; if (p->mod == k.mod && p->topo == k.topo && p->nb == k.nb && p->na == k.na && !memcmp(p->rate, k.rate, sizeof k.rate) && !memcmp(p->bind, k.bind, sizeof k.bind) && !memcmp(p->fmt, k.fmt, sizeof k.fmt) && !memcmp(p->off, k.off, sizeof k.off) && !memcmp(p->loc, k.loc, sizeof k.loc)) return p->p; }
    if (g_mo_npipes >= 512) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, mod, "main", NULL},
                                             {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_mo_fs, "main", NULL}};
    VkVertexInputBindingDescription vbd[32]; VkVertexInputAttributeDescription vad[32];
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkDynamicState dyn[4] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; uint32_t ndyn = 2;
    if (D.has_vids) dyn[ndyn++] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
    else {
        for (uint32_t i = 0; i < nb; i++) { vbd[i].binding = i; vbd[i].stride = 0; vbd[i].inputRate = bd[i].inputRate; }
        for (uint32_t i = 0; i < na; i++) { vad[i].location = ad[i].location; vad[i].binding = ad[i].binding; vad[i].format = ad[i].format; vad[i].offset = ad[i].offset; }
        vi.vertexBindingDescriptionCount = nb; vi.pVertexBindingDescriptions = vbd; vi.vertexAttributeDescriptionCount = na; vi.pVertexAttributeDescriptions = vad;
        if (nb) dyn[ndyn++] = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
    }
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = (VkPrimitiveTopology)topo;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.pNext = vk_vp_next(); vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba; memset(&ba, 0, sizeof ba); ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = ndyn; dy.pDynamicStates = dyn;
    VkFormat cf = VK_FORMAT_R16G16_SFLOAT;
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &cf;
    VkGraphicsPipelineCreateInfo gi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.pNext = &ri; gi.stageCount = 2; gi.pStages = st; gi.pInputAssemblyState = &ia; gi.pViewportState = &vp; gi.pRasterizationState = &rs; gi.pMultisampleState = &ms;
    gi.pColorBlendState = &cb; gi.pDynamicState = &dy; gi.layout = g_mo_pl;
    if (!D.has_vids) gi.pVertexInputState = &vi;
    MoPipe *e = &g_mo_pipes[g_mo_npipes++];
    *e = k;
    if (vkCreateGraphicsPipelines(V.device, g_pcache, 1, &gi, NULL, &e->p) != VK_SUCCESS) { e->p = VK_NULL_HANDLE; static int nl; if (nl++ < 10) printf("nvn_vk_mo: a motion pipeline was refused by the driver\n"); }
    return e->p;
}
/* ---- coverage: an occlusion query around each replay counts the pixels it wrote (those past the depth check); read 4 frames
 * later without waiting. A key whose replays wrote nothing twice in a row is "dead": the selection leaves it out but for a retry. */
#define MO_QN 64
static VkQueryPool g_mo_qpool; static int g_mo_qstate; static uint64_t g_mo_qkey[4][MO_QN]; static int g_mo_qn[4]; static int64_t g_mo_qseq;
static struct { uint64_t key; uint8_t zeros; } g_mo_cov[2048];
static int64_t g_mo_cov_dead_n, g_mo_cov_live_n;
static int mo_cov_dead(uint64_t key) {
    unsigned s = (unsigned)(key ^ (key >> 29)) & 2047;
    for (unsigned k = 0; k < 8; k++) { unsigned t = (s + k) & 2047; if (g_mo_cov[t].key == key) return g_mo_cov[t].zeros >= 2; if (!g_mo_cov[t].key) return 0; }
    return 0;
}
static void mo_cov_note(uint64_t key, uint64_t samples) {
    unsigned s = (unsigned)(key ^ (key >> 29)) & 2047;
    for (unsigned k = 0; k < 8; k++) {
        unsigned t = (s + k) & 2047;
        if (g_mo_cov[t].key == key || !g_mo_cov[t].key) { g_mo_cov[t].key = key; if (samples) { g_mo_cov[t].zeros = 0; g_mo_cov_live_n++; } else { if (g_mo_cov[t].zeros < 255) g_mo_cov[t].zeros++; g_mo_cov_dead_n++; } return; }
    }
}
static int mo_q_begin(void) {                      /* outside a pass, before the replay pass: read the slot's last results, reset it */
    if (g_mo_qstate == 0) {
        VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_OCCLUSION; qi.queryCount = 4 * MO_QN;
        g_mo_qstate = vkCreateQueryPool(V.device, &qi, NULL, &g_mo_qpool) == VK_SUCCESS ? 1 : -1;
    }
    if (g_mo_qstate < 0) return -1;
    int r = (int)(g_mo_qseq++ & 3);
    for (int k = 0; k < g_mo_qn[r]; k++) {
        uint64_t v;
        if (vkGetQueryPoolResults(V.device, g_mo_qpool, (uint32_t)(r * MO_QN + k), 1, 8, &v, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) mo_cov_note(g_mo_qkey[r][k], v);
    }
    g_mo_qn[r] = 0;
    vkCmdResetQueryPool(R.cmd, g_mo_qpool, (uint32_t)(r * MO_QN), MO_QN);
    return r;
}
/* mv_present, after the camera motion pass wrote the motion image (no pass open): the moving objects drawn again into it */
static void mo_replay_(long long frame) {
    if (!mo_obj_on()) return;
    int c = g_mo_cur, p = c ^ 1;
    if (!g_mo_n[c] || !g_mo_n[p] || g_mo_keep_state <= 0 || !g_mv_dview || !mo_init()) return;
    mo_classify(c); mo_classify(p);
    int began = 0, qr = -1;
    float tol = 3e-5f;                             /* 0.3.13: 4e-6 rejected some of Mario's pieces (S24 markers: 93 of his ~350 red pixels written, 250 with no depth test) */
    { static float te = -1; if (te < 0) { const char *e = getenv("SMO_VK_MO_TOL"); te = e && atof(e) > 0 ? (float)atof(e) : 3e-5f; } tol = te; }
    VkDescriptorImageInfo di = {g_mv_smp, g_mo_ds_fresh && g_mo_ds_view ? g_mo_ds_view : g_mv_dview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};      /* the depth as the G-buffer pass left it (mo_dsnap) */
    /* Which moving draws are drawn again. Measured on the S24 (Cascade, running): all of them were ~92 draws, 112,000 vertices,
     * 1.6-1.8 ms of GPU (pixels only 0.2 of it: the game's vertex code run twice is the cost). Skinned meshes (characters,
     * creatures: their vertex stage reads integer inputs, the bone indices) always; rigid ones (coins, small props; the large
     * ones are mostly plants in the wind, a small motion) only while they fit a vertex budget, smallest first
     * (SMO_VK_MO_RIGID=<vertices a frame>, default 4000; SMO_VK_MO_ALL=1: every moving draw).
     * The cost is per draw, not per vertex (S24: 1 draw 0.04 ms, 10 draws 0.31, 57 draws 1.77: ~30 us each whatever its size), so
     * at most SMO_VK_MO_CAP draws a frame (default 24): skinned ones first, the nearest to the camera first, then the small rigid
     * ones. Nearest: the translation of the first bone matrix (3x4, bone to world: the skinning shaders multiply by it, then by
     * the camera) of the draw's first own buffer that holds whole matrices. By size first, Mario's head and cap were left out
     * (markers on the S24) while larger skinned meshes further away were drawn. */
    static int16_t pair[MO_MAX]; static uint8_t sel[MO_MAX];
    {
        static int rb = -1, all = -1, cap = -1;
        if (rb < 0) { const char *e = getenv("SMO_VK_MO_RIGID"); rb = e ? atoi(e) : 80000; e = getenv("SMO_VK_MO_ALL"); all = e && atoi(e); e = getenv("SMO_VK_MO_CAP"); cap = e ? atoi(e) : 64;      /* 0.3.20: 64 and 80000 (S24, Cascade: all ~100 moving draws cost 2.35 ms of GPU and 0.72 ms of render thread against 1.83 and 0.56 at 32; a third of them draw nothing). 0.3.19: 32 and 40000 (were 24 and 12000: Mario's cap was left out; the cost is per draw, and on the Mali tester's phone all of them took 0.13-0.19 ms of GPU) */ }
        static int rig[MO_MAX], sk[MO_MAX]; int nr = 0, ns = 0;
        for (int i = 0; i < g_mo_n[c]; i++) {
            MoRec *r = &g_mo[c][i];
            sel[i] = 0; pair[i] = -1;
            if (!r->ok) continue;
            int same, j = mo_match(c, i, &same);
            if (j < 0 || (same && !r->vmask)) continue;      /* (GPU-written vertices change without their uniforms: an idle animation) */
            pair[i] = (int16_t)j; g_mo_cand++;
            {   /* 0.3.13: the same mesh with the same own data drawn again in this pass (Mario is drawn twice, the same bones): one replay
                 * does (both landed once the depth check read the G-buffer's depth, and two sets of ~13 draws filled the cap) */
                int dup = 0;
                for (int k = 0; k < i && !dup; k++) if (pair[k] >= 0 && g_mo[c][k].count == r->count && g_mo[c][k].inst == r->inst && g_mo[c][k].objh == r->objh && g_mo[c][k].base_vertex == r->base_vertex) dup = 1;
                if (dup) { pair[i] = -1; g_mo_dupskip++; continue; }
            }
            if (all) sel[i] = 1; else if (r->vs->key[0] || r->vmask) sk[ns++] = i; else rig[nr++] = i;
        }
        #define MO_V(i) ((uint64_t)g_mo[c][i].count * g_mo[c][i].inst)
        static float dist[MO_MAX];
        /* the camera distance of each candidate (1e30: not known, last): the translation of the first 3x4 matrix of its first own
         * buffer (bones of a skinned mesh, the model matrix of a rigid one: Mario's cap had it in c7) */
        for (int pass = 0; pass < 2; pass++) for (int a = 0; a < (pass ? nr : ns); a++) {
            int i = pass ? rig[a] : sk[a]; MoRec *r = &g_mo[c][i]; dist[i] = 1e30f;
            for (int s = 0; s < 15; s++) {
                if (!(r->own >> s & 1) || r->keep_off[s] == ~0u || r->size[s] < 48) continue;
                const float *m = (const float *)mo_keep_at(c, r->keep_off[s]);
                float dx = m[3] - g_mo_campos[0], dy = m[7] - g_mo_campos[1], dz = m[11] - g_mo_campos[2], d2 = dx * dx + dy * dy + dz * dz;
                if (d2 == d2) dist[i] = d2;
                break;
            }
            /* 0.3.13: a draw whose last replays wrote no pixel (hidden, off screen, or drawn where the scene's depth is not: Mario is drawn
             * twice and one of the two lies ~50 units behind the depth the scene keeps) goes last, but is tried again every 32 frames */
            if (mo_cov_dead(r->key) && ((frame + (long long)(r->key & 31)) & 31)) dist[i] = 2e30f;
        }
        for (int a = 1; a < ns; a++) { int x = sk[a], b = a; while (b > 0 && dist[sk[b - 1]] > dist[x]) { sk[b] = sk[b - 1]; b--; } sk[b] = x; }      /* nearest first */
        for (int a = 1; a < nr; a++) { int x = rig[a], b = a; while (b > 0 && dist[rig[b - 1]] > dist[x]) { rig[b] = rig[b - 1]; b--; } rig[b] = x; }      /* nearest first */
        /* 0.3.19: skinned and rigid together, nearest first (merged from the two sorted lists). Skinned ones first had filled the cap
         * with Mario's body and the skinned characters around, and Mario's cap (rigid, 2736 + 3117 vertices in c7, as near as his head)
         * was never drawn again: in made frames it moved with the camera while his body moved on its own (the user: the hat late,
         * shaking, as if at 30). Rigid ones keep their vertex budget. */
        int taken = 0, ia = 0, ib = 0;
        uint64_t used = 0;
        int capx = cap < 0 || cap > g_mo_cap_dyn ? g_mo_cap_dyn : cap;      /* 0.3.22: the time budget (mo_budget) */
        while (taken < capx && (ia < ns || ib < nr)) {
            int isk = ib >= nr || (ia < ns && dist[sk[ia]] <= dist[rig[ib]]);
            int i = isk ? sk[ia++] : rig[ib++];
            if (dist[i] > 1.5e30f) { if (isk) ia = ns; else ib = nr; continue; }      /* the rest of that list is unknown or idle */
            if (!isk) { uint64_t v = MO_V(i); if (used + v > (uint64_t)rb) continue; used += v; }
            sel[i] = 1; taken++;
        }
        g_mo_last_taken = taken;
        if (mv_shot_frame(frame)) {                /* (test) every note of this frame: what it is and why it was or was not drawn again */
            int nok = 0, nsame = 0, nun = 0;
            for (int i = 0; i < g_mo_n[c]; i++) {
                MoRec *r = &g_mo[c][i];
                if (!r->ok) { nok++; continue; }
                int same, j = mo_match(c, i, &same);
                if (j < 0) { nun++; continue; }
                if (same && !r->vmask) { nsame++; continue; }
                int skinned = r->vs->key[0] != 0 || r->vmask;
                printf("nvn_vk_mo: frame %lld: moving: vertex program #%u, %u vertices x %u, %s, distance %.0f, %s\n", frame, r->prog_vs, r->count, r->inst, skinned ? "skinned" : "rigid",
                       dist[i] < 1e29f ? sqrt((double)dist[i]) : -1.0, sel[i] ? "DRAWN AGAIN" : "left (cap or budget)");
            }
            printf("nvn_vk_mo: frame %lld: %d notes: %d not replayable (storage buffers, divisor or keep buffer), %d not matched, %d unchanged\n", frame, g_mo_n[c], nok, nun, nsame);
            /* (test) the draws within 400 units of Mario: a 3x4 matrix in any of their buffers whose translation is there (bones,
             * a model matrix), whatever their state */
            for (int i = 0; g_mo_marok && i < g_mo_n[c]; i++) {
                MoRec *r = &g_mo[c][i]; int near = -1; float best = 1e30f;
                for (int s = 0; s < 15 && near < 0; s++) {
                    if (!r->addr[s] || r->size[s] < 48) continue;
                    const float *m = r->keep_off[s] != ~0u ? (const float *)mo_keep_at(c, r->keep_off[s]) : (const float *)(uintptr_t)r->addr[s];
                    uint32_t nm = r->size[s] / 48; if (nm > 64) nm = 64;
                    for (uint32_t k = 0; k < nm; k++) { const float *q = m + 12 * k; float dx = q[3] - g_mo_marpos[0], dy = q[7] - g_mo_marpos[1], dz = q[11] - g_mo_marpos[2], d2 = dx * dx + dy * dy + dz * dz; if (d2 < best) best = d2; if (d2 < 400.0f * 400.0f) { near = s; break; } }
                }
                if (near < 0) continue;
                int same = 0, j = r->ok ? mo_match(c, i, &same) : -2;
                printf("nvn_vk_mo: frame %lld: near Mario (%.0f): vertex program #%u, %u vertices x %u, buffer c%d, replayable %d, %s, integer inputs %x, GPU-written bindings %x (%llu bytes), %s | depth bias %s %g %g %g, depth range %g..%g, viewport %.0f %.0f %.0f %.0f\n", frame, sqrt((double)best), r->prog_vs, r->count, r->inst, near + 3,
                       r->ok, j == -2 ? "-" : j < 0 ? "not matched" : same ? "own data unchanged" : "changed", r->ok ? r->vs->key[0] : 0, r->vmask, (unsigned long long)(r->vmask ? r->vsize[__builtin_ctz(r->vmask)] : 0), sel[i] ? "DRAWN AGAIN" : "not drawn again",
                       r->dbias ? "ON" : "off", r->bias[0], r->bias[1], r->bias[2], r->vp.minDepth, r->vp.maxDepth, r->vp.x, r->vp.y, r->vp.width, r->vp.height);
            }
        }
        #undef MO_V
    }
    int nth = 0;                                   /* draws drawn again this frame (SMO_VK_MO_MARK) */
    for (int i = 0; i < g_mo_n[c]; i++) {
        MoRec *r = &g_mo[c][i];
        if (!sel[i]) continue;
        {   /* SMO_VK_MO_MAX=<n> (test): at most n draws a frame */
            static int mx = -2; static long long mf = -1; static int done;
            if (mx == -2) { const char *e = getenv("SMO_VK_MO_MAX"); mx = e ? atoi(e) : -1; }
            if (mf != frame) { mf = frame; done = 0; }
            if (mx >= 0 && done >= mx) continue;
            done++;
        }
        int j = pair[i];
        const MoRec *q = &g_mo[p][j];
        /* the vertex layout: the draw's bindings, then a copy of each GPU-written one from last frame's vertices (when the last
         * frame's draw has it); its attributes trimmed to the inputs the vertex stage reads, then the copies' inputs at free locations */
        VkVertexInputBindingDescription2EXT bd2[32]; VkVertexInputAttributeDescription2EXT ad2[32]; VkBuffer vb2[32]; VkDeviceSize vo2[32];
        uint32_t nb2 = r->nb, na2 = 0, pb[17]; uint8_t ploc[16];
        memset(ploc, 0xff, sizeof ploc);
        memcpy(bd2, r->bd, sizeof r->bd[0] * r->nb); memcpy(vb2, r->vb, sizeof r->vb[0] * r->nb); memcpy(vo2, r->vo, sizeof r->vo[0] * r->nb);
        uint32_t maxb = V.props.limits.maxVertexInputBindings < 32 ? V.props.limits.maxVertexInputBindings : 32, maxl = V.props.limits.maxVertexInputAttributes < 32 ? V.props.limits.maxVertexInputAttributes : 32;
        for (uint32_t k = 0; k < r->nb; k++) {
            pb[k] = ~0u;
            if (!(r->vmask >> k & 1) || k >= q->nb || !(q->vmask >> k & 1) || q->vkeep_off[k] == ~0u || q->bd[k].stride != r->bd[k].stride || nb2 >= maxb) continue;
            bd2[nb2] = r->bd[k]; bd2[nb2].binding = nb2; vb2[nb2] = g_mo_vkeep; vo2[nb2] = (VkDeviceSize)g_mo_part[p] * MO_VKEEP + q->vkeep_off[k]; pb[k] = nb2++;
        }
        uint32_t used = r->vs->in_attr & 0xffff, nextl = 0;
        for (uint32_t a = 0; a < r->na; a++) if (used >> r->ad[a].location & 1) ad2[na2++] = r->ad[a];
        for (uint32_t a = 0, n0 = na2; a < n0; a++) {
            uint32_t L = ad2[a].location, k = ad2[a].binding;
            if (L >= 16 || k >= r->nb || pb[k] == ~0u) continue;
            while (nextl < maxl && (used >> nextl & 1)) nextl++;
            if (nextl >= maxl || na2 >= 32) break;
            ad2[na2] = ad2[a]; ad2[na2].location = nextl; ad2[na2].binding = pb[k]; ploc[L] = (uint8_t)nextl; used |= 1u << nextl; na2++;
        }
        if (r->vmask && ploc[0] == 0xff) {         /* GPU-written vertices without last frame's copy (first frame, buffer full): its limbs would not move: left out */
            int anyp = 0; for (int L = 0; L < 16; L++) if (ploc[L] != 0xff) anyp = 1;
            if (!anyp) { g_mo_noprev++; continue; }
        }
        VkShaderModule mod = mo_vs_get(r->prog, r->vs, ploc);
        VkPipeline pipe = mod ? mo_pipe_get(mod, r->topo, nb2, bd2, na2, ad2) : VK_NULL_HANDLE;
        if (!pipe) { g_mo_nopipe++; continue; }
        if (!began) {
            pass_end(); cmd_begin(); fgt_mark(g_fgt_real, 7);      /* GPU time of the replay (nvn_vk_motion.c fgt) */
            VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};      /* the camera motion pass's writes, before this pass loads and writes the image */
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
            VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            ca.imageView = g_mv_view; ca.imageLayout = VK_IMAGE_LAYOUT_GENERAL; ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo rin = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            rin.renderArea.extent.width = (uint32_t)g_mv_w; rin.renderArea.extent.height = (uint32_t)g_mv_h; rin.layerCount = 1; rin.colorAttachmentCount = 1; rin.pColorAttachments = &ca;
            qr = mo_q_begin();                     /* (outside the pass: reset of this frame's queries) */
            vkCmdBeginRendering(R.cmd, &rin);
            began = 1;
        }
        if (g_mo_pool_cb[R.cur] != g_cb_begins + 1) { vkResetDescriptorPool(V.device, g_mo_pool[R.cur], 0); g_mo_pool_cb[R.cur] = g_cb_begins + 1; }
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = g_mo_pool[R.cur]; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_mo_dl;
        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(V.device, &ai, &set) != VK_SUCCESS) break;      /* (more than 256 in one command buffer) */
        VkDescriptorBufferInfo bi[36]; VkWriteDescriptorSet wr[37]; int nw = 0, nbi = 0;
        for (unsigned n = 0; n < 18; n++) {
            if (!(r->cbmask >> n & 1)) continue;
            const MoBuf *b = &r->cb[n];
            VkDescriptorBufferInfo cur = {b->zero ? R.zero : b->buf, b->zero ? 0 : b->off, b->zero ? 65536 : b->size};
            VkDescriptorBufferInfo prv = cur;
            if (n >= 3 && q->addr[n - 3] && q->keep_off[n - 3] != ~0u) { prv.buffer = g_mo_keep; prv.offset = (VkDeviceSize)g_mo_part[p] * MO_KEEP + q->keep_off[n - 3]; prv.range = q->size[n - 3] < 65536 ? q->size[n - 3] : 65536; }
            bi[nbi] = cur; VkWriteDescriptorSet w0 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, n, 0, 1, CB_DT, NULL, &bi[nbi], NULL}; wr[nw++] = w0; nbi++;
            bi[nbi] = prv; VkWriteDescriptorSet w1 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 100 + n, 0, 1, CB_DT, NULL, &bi[nbi], NULL}; wr[nw++] = w1; nbi++;
        }
        { VkWriteDescriptorSet w2 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 200, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di, NULL, NULL}; wr[nw++] = w2; }
        vkUpdateDescriptorSets(V.device, (uint32_t)nw, wr, 0, NULL);
        vkCmdBindPipeline(R.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(R.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_mo_pl, 0, 1, &set, 0, NULL);
        static int mark = -1; if (mark < 0) { const char *e = getenv("SMO_VK_MO_MARK"); mark = e && atoi(e); }      /* (test) each draw writes 1000 + its place in the selection */
        struct { uint32_t alpha[2]; float psize, pad; float sz[4]; } pc = {{0, 0}, 1.0f, 0.0f, {(float)g_mv_w, (float)g_mv_h, tol, mark ? 1000.0f + (float)nth : 0.0f}};
        vkCmdPushConstants(R.cmd, g_mo_pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        vkCmdSetViewport(R.cmd, 0, 1, &r->vp);
        {   /* SMO_VK_MO_NOPIX=1 (test): a 1x1 scissor, to time the replay without its pixels */
            static int np = -1; if (np < 0) { const char *e = getenv("SMO_VK_MO_NOPIX"); np = e && atoi(e); }
            VkRect2D sc = r->sc; if (np) { sc.extent.width = sc.extent.width ? 1 : 0; sc.extent.height = sc.extent.height ? 1 : 0; }
            vkCmdSetScissor(R.cmd, 0, 1, &sc);
        }
        if (D.has_vids) vkCmdSetVertexInputEXT(R.cmd, nb2, bd2, na2, ad2);
        if (nb2) {
            VkDeviceSize strides[32];
            for (uint32_t k = 0; k < nb2; k++) strides[k] = bd2[k].stride;
            if (D.has_vids) vkCmdBindVertexBuffers(R.cmd, 0, nb2, vb2, vo2);
            else vkCmdBindVertexBuffers2(R.cmd, 0, nb2, vb2, vo2, NULL, strides);
        }
        int qi = qr >= 0 && g_mo_qn[qr] < MO_QN ? g_mo_qn[qr]++ : -1;      /* (coverage: the pixels it writes) */
        if (qi >= 0) { g_mo_qkey[qr][qi] = r->key; vkCmdBeginQuery(R.cmd, g_mo_qpool, (uint32_t)(qr * MO_QN + qi), 0); }
        if (r->itype >= 0) { vkCmdBindIndexBuffer(R.cmd, r->ib, r->iboff, (VkIndexType)r->itype); vkCmdDrawIndexed(R.cmd, r->count, r->inst, 0, r->base_vertex, r->base_inst); }
        else vkCmdDraw(R.cmd, r->count, r->inst, r->first, r->base_inst);
        if (qi >= 0) vkCmdEndQuery(R.cmd, g_mo_qpool, (uint32_t)(qr * MO_QN + qi));
        g_mo_replayed++; nth++;
        if (mv_shot_frame(frame)) { int dup = 0; for (int k2 = 0; k2 < g_mo_n[c]; k2++) if (k2 != i && g_mo[c][k2].key == r->key) dup++; char ow[96] = ""; int ol = 0; for (int s = 0; s < 15 && ol < 80; s++) if (r->own >> s & 1) ol += snprintf(ow + ol, sizeof ow - ol, " c%d:%u", s + 3, r->size[s]);
            printf("nvn_vk_mo: frame %lld: drawn again: vertex program #%u, %u vertices x %u, integer inputs %x, %d other draws of its key, own data%s, distance to its pair %.3f\n", frame, r->prog_vs, r->count, r->inst, r->vs->key[0], dup, ow, (double)mo_dist(r, c, q, p)); }
    }
    if (began) {
        vkCmdEndRendering(R.cmd); fgt_mark(g_fgt_real, 8);
        R.pipe_bound = NULL; R.dyn_dirty = 1; R.vi_dirty = 1;      /* our own pipeline and state: the game's draws set theirs again */
    }
    if (++g_mo_rframes % 300 == 0) {
        printf("nvn_vk_mo: frame %lld: %.1f moving draws drawn again a frame (%.1f candidates; %lld without a motion stage or pipeline, %lld vertex stages not made, %d motion pipelines) | GPU-written vertex streams copied: %.1f a frame, %.0f KB a frame (%lld did not fit), %lld draws without last frame's copy\n", frame,
               (double)g_mo_replayed / 300.0, (double)g_mo_cand / 300.0, (long long)g_mo_nopipe, (long long)g_mo_vs_failed, g_mo_npipes, (double)g_mo_vcopies / 300.0, (double)g_mo_vbytes / 300.0 / 1024.0, (long long)g_mo_vfull, (long long)g_mo_noprev);
        printf("nvn_vk_mo: replays that wrote pixels: %lld, that wrote none: %lld; duplicates left out: %.1f a frame; the render thread's time for per-object motion: %.2f ms a frame; buffers copied again after a write: %.1f a frame\n", (long long)g_mo_cov_live_n, (long long)g_mo_cov_dead_n, (double)g_mo_dupskip / 300.0, (double)g_mo_cpu_ns / 300.0 / 1e6, (double)g_mo_recopies / 300.0); g_mo_dupskip = 0; g_mo_cpu_ns = 0; g_mo_recopies = 0;
        g_mo_replayed = g_mo_cand = g_mo_nopipe = 0; g_mo_vcopies = g_mo_vbytes = g_mo_vfull = g_mo_noprev = 0; g_mo_cov_live_n = g_mo_cov_dead_n = 0;
    }
}

/* vk_present2, each real frame, after mv_present: the statistics (SMO_VK_MO_MEASURE), then this frame's notes become the last frame's */
static void mo_frame_end(void) {
    int c = g_mo_cur, p = c ^ 1, n = g_mo_n[c];
    if (g_mo_on > 0 && n) {
        mo_classify(c);
        if (g_mo_n[p]) for (int i = 0; i < n; i++) {
            int found = 0, same = 0, dup = 0;
            for (int j = 0; j < g_mo_n[p]; j++) if (g_mo[p][j].key == g_mo[c][i].key) { found++; if (g_mo[p][j].objh == g_mo[c][i].objh) same = 1; }
            for (int j = 0; j < n; j++) if (j != i && g_mo[c][j].key == g_mo[c][i].key) { dup = 1; break; }
            g_mo_draws++;
            if (!found) g_mo_unmatched++; else { g_mo_matched++; if (same) g_mo_same++; else g_mo_changed++; }
            if (dup) g_mo_dupkeys++;
        }
        if (++g_mo_frames % 300 == 0 && g_mo_draws) {
            char sh[128] = "", ob[128] = ""; int a = 0, b = 0;
            for (int s = 0; s < 15; s++) { if (g_mo_shared_seen[s] > g_mo_obj_seen[s]) a += snprintf(sh + a, sizeof sh - a, " c%d", s + 3); else if (g_mo_obj_seen[s]) b += snprintf(ob + b, sizeof ob - b, " c%d", s + 3); }
            printf("nvn_vk_mo: %lld frames: %.0f G-buffer draws a frame; matched %.1f%% (own data unchanged %.1f%%, changed %.1f%% = %.0f draws a frame), unmatched %.1f%%, keys shared by several draws %.1f%%%s | vertex slots shared:%s | each object's:%s\n",
                   (long long)g_mo_frames, (double)g_mo_draws / 300.0, 100.0 * g_mo_matched / g_mo_draws, 100.0 * g_mo_same / g_mo_draws, 100.0 * g_mo_changed / g_mo_draws, (double)g_mo_changed / 300.0,
                   100.0 * g_mo_unmatched / g_mo_draws, 100.0 * g_mo_dupkeys / g_mo_draws, g_mo_over ? " (over the note limit)" : "", sh, ob);
            g_mo_draws = g_mo_matched = g_mo_same = g_mo_changed = g_mo_unmatched = g_mo_dupkeys = g_mo_over = 0;
            memset(g_mo_shared_seen, 0, sizeof g_mo_shared_seen); memset(g_mo_obj_seen, 0, sizeof g_mo_obj_seen);
        }
    }
    g_mo_cur = p; g_mo_n[p] = 0; g_mo_classed[p] = 0; g_mo_ds_fresh = 0; memset(g_mo_dedup[p], 0, sizeof g_mo_dedup[p]);
    g_mo_part[p] = (g_mo_part[c] + 1) & 3; g_mo_keep_used[g_mo_part[p]] = 0; g_mo_vkeep_used[g_mo_part[p]] = 0; g_mo_vcopied = 0;      /* the next part of the ring */
}

/* 0.3.13: the render thread's time in per-object motion (noting the draws, the copies at the G-buffer pass's end, the replay),
 * printed with the nvn_vk_mo line every 300 frames. The tester's Dimensity 8300 log had the render thread 96% busy and ~170 ms a
 * frame with frame generation on (its GPU work 2.5 ms): the per-draw hashing of the big shared buffers. */
static int64_t mo_now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec; }
static void mo_note_draw(unsigned prim, uint32_t count, uint32_t first, int32_t base_vertex, uint32_t inst, uint32_t base_inst, VkBuffer ib, uint64_t iboff, int itype) {
    if (!g_mo_in_gbuf) { g_mo_pend.ok = 0; return; }
    int64_t t0 = mo_now_ns(); mo_note_draw_(prim, count, first, base_vertex, inst, base_inst, ib, iboff, itype); g_mo_cpu_ns += mo_now_ns() - t0;
}
static void mo_pass_ended(void) { if (!g_mo_in_gbuf) return; int64_t t0 = mo_now_ns(); mo_pass_ended_(); g_mo_cpu_ns += mo_now_ns() - t0; }
int nvn_vk_fg_objects(int *cap) { if (cap) *cap = g_mo_cap_dyn; return g_mo_last_taken; }      /* 0.4: for the perf line (android_perf.c) */
static void mo_budget(void) {                       /* after each replay (0.3.22, above g_mo_cap_dyn) */
    static int budget_us = -1, n;
    if (budget_us < 0) { const char *e = getenv("SMO_VK_MO_BUDGET_US"); budget_us = e ? atoi(e) : 3000; printf("nvn_vk_mo: objects drawn again: %s%c", budget_us > 0 ? "as many as fit the time budget (at most 64)" : "a fixed number (SMO_VK_MO_BUDGET_US=0)", 10); }
    if (budget_us <= 0) { g_mo_cap_dyn = 1 << 30; return; }
    double cost = (double)g_mo_rep_ns_last / 1e6 + (g_fgt_obj_last > 0 ? g_fgt_obj_last : 0.0), b = budget_us / 1000.0;
    g_mo_cost_avg = g_mo_cost_avg < 0 ? cost : 0.9 * g_mo_cost_avg + 0.1 * cost;
    if (++n % 30) return;
    int old = g_mo_cap_dyn;
    /* 0.4: the S24 went from 64 to 36 objects at 2.8-3.5 ms with every real frame on time (a visible loss where nothing was late).
     * Fewer only while real frames are late (more than 5% over 40 ms, averaged) and the cost is over the budget, or the cost is over
     * twice the budget; more again while real frames are on time (under 2% late) and the cost under twice the budget, or under 60% of it. */
    int late = g_fg_late_avg > 0.05;
    if (((g_mo_cost_avg > b && late) || g_mo_cost_avg > 2.0 * b) && g_mo_cap_dyn > 8) { g_mo_cap_dyn = g_mo_cap_dyn * 3 / 4; if (g_mo_cap_dyn < 8) g_mo_cap_dyn = 8; }
    else if ((g_mo_cost_avg < 0.6 * b || (g_fg_late_avg < 0.02 && g_mo_cost_avg < 2.0 * b)) && g_mo_cap_dyn < 64 && g_mo_last_taken >= g_mo_cap_dyn) { g_mo_cap_dyn += 4; if (g_mo_cap_dyn > 64) g_mo_cap_dyn = 64; }
    static int64_t said_at;
    if (old != g_mo_cap_dyn && (n - said_at >= 600 || !said_at)) { said_at = n; printf("nvn_vk_mo: time budget %.1f ms: the replay cost %.2f ms a frame, real frames late %.0f%%, now at most %d objects (was %d)%c", b, g_mo_cost_avg, 100.0 * g_fg_late_avg, g_mo_cap_dyn, old, 10); }
}
static void mo_replay(long long frame) { int64_t t0 = mo_now_ns(); mo_replay_(frame); int64_t d = mo_now_ns() - t0; g_mo_cpu_ns += d; g_mo_rep_ns_last = d; mo_budget(); }
