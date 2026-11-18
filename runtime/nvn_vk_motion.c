/* nvn_vk_motion.c - frame generation, step 1: camera motion vectors (docs/FRAME_GENERATION_PLAN.md). #included by nvn_vk_draw.c.
 *
 * Every pixel of the 3D scene gets the screen motion the CAMERA gave it since the frame before, from the scene's depth and the
 * camera of this frame and the last one: no frame is generated yet, this only makes and checks the vectors.
 *   - The camera: smo_camera.c reads it in StageScene::drawMain (main 0x4ccaf8) through the game's own getters (the view
 *     matrix, 3x4, and this frame's device projection, 4x4); nvn_gl.c queue_present hands that frame's camera to the render
 *     thread inside the PresentJob (cam_ok, cam_view, cam_proj), so a present and its camera always match.
 *   - The depth: the depth target of the frame's first G-buffer pass (pass_begin: 4 or more colour targets and a depth target).
 *   - The vectors: at the present, a compute pass (groups of 64x1) reads the depth and writes RG16F motion in pixels: where the
 *     pixel was in the frame before, minus where it is (+x right, +y down), from one clip-to-clip matrix made on the CPU (the
 *     last view-projection times the inverse of this one).
 *   - Depth: z = 2d - 1 (NVN's -1..1, as the fork has it). 0.3.9 took z = d because it gave a more uniform field, which was the
 *     wrong test: the camera orbits Mario, so the true field is not uniform. 0.3.11 tested it against the pictures (the next real
 *     frame rebuilt from the last one moved by k x the vectors, k fitted per screen strip, S24, camera turning in Cascade):
 *     z = 2d - 1 fits at k = 1.00 in every strip (mean error 6.5 grey levels against 20-23 unmoved); z = d needs k = 0.6-0.7 and
 *     fits worse (error 19-22). z = d made the vectors about twice the real background motion (37 px against 17), so every
 *     made frame overshot: the world shook.
 *   - Groups of 64x1, not 8x8: Qualcomm's driver ran an 8x8 group (SPIR-V 1.6 LocalSizeId 8 8 1) as 8x1, so only the top
 *     eighth of the image was written.
 * The camera reading, the matrix and the shader follow Super Odyssey PC Recomp's camera motion vectors for its temporal upscalers
 * (runtime/hle/odyssey_debug.c, runtime/nvn/vulkan/dlss_probe.c: MIT, Copyright (c) 2026 Peiikai (LAS Recomp), Power Moon Recomp
 * contributors, Super Odyssey PC Recomp contributors).
 *
 * SMO_VK_MOTION=1: the vectors every frame (to measure their cost). SMO_VK_MOTION_SHOT=<frame>[,<frame>...]: at those frames
 * the vectors are also read back, written as motion_<frame>.bmp in SMO_SHOT_DIR (red = x, green = y, 128 = none, 4 levels a
 * pixel of motion) and summed up in an nvn_vk_motion line (the mean and largest motion, the share of pixels that move). */

static Tex *g_mv_depth;                            /* this frame's scene depth (pass_begin) */
static VkImage g_mv_img; static VMem g_mv_mem; static VkImageView g_mv_view; static int g_mv_w, g_mv_h;
static VkImageView g_mv_dview; static VkImage g_mv_dview_img;      /* a depth-only view of the scene depth, made again when its image changes */
/* 0.3.14, the motion image's companions (its size): 0 each pixel's distance from the camera (R32F, written by the motion pass), 1 the
 * forward key (R32UI: the nearest distance that landed on a pixel of the made frame), 2 the forward motion (RG16F: that pixel's vector),
 * 3 the finished forward motion (gaps filled; the warp reads it), 4 the last frame's distance (0.3.18: a strip is fetched from the last
 * real frame only where that frame showed something as far there) */
static VkImage g_mv_aimg[5]; static VMem g_mv_amem[5]; static VkImageView g_mv_aview[5]; static int g_fs_pdist_ok;
static VkDescriptorSetLayout g_mv_dl; static VkPipelineLayout g_mv_pl; static VkPipeline g_mv_pipe; static VkDescriptorPool g_mv_pool;
static VkDescriptorSet g_mv_set[NSLOT]; static VkSampler g_mv_smp;
static int g_mv_state;                             /* 0 not tried, 1 ready, -1 failed */
static double g_mv_prev_vp[16]; static int g_mv_have_prev;
static float g_mv_prev_pl[3]; static int g_mv_prev_plok;      /* step 4: Mario's position in the last frame */

/* 0.3.12: the GPU time of frame generation's own work, from timestamps (BOTTOM_OF_PIPE pairs: the work recorded between them):
 *   0-1 the motion pass (vk_present2 -> mv_present, in the real frame's command buffer)
 *   2-3 the HUD copy (fg_hud_after_draw, in the real frame)
 *   4-6 the made frame's warp (its layouts and the compute pass), 6-5 its own present pass (upscaling, sharpening, the touch
 *       layer) up to its submission
 * Kept in a ring of 4 real frames, read back 4 frames later without waiting (a result not ready yet is skipped), averaged and
 * printed every 300 made frames as an nvn_vk_fgtime line. SMO_VK_FG_TIME=0 turns it off. Only while frame generation is on.
 * Measured on the S24 (Adreno 750; Cascade, camera turning, 1600x900 window texture, screen 2340x1080): motion 0.63 ms, HUD copy
 * 0.26, warp + present pass 3.70: 4.6 ms a pair; the frame's GPU busy went 14.2 -> 18.2 ms. The same on the Mali path. */
#define FGT_RING 4
#define FGT_N 9
static const int k_fgt_pair[5][2] = {{0, 1}, {2, 3}, {4, 6}, {6, 5}, {7, 8}};      /* 7-8 (0.3.13): the moving objects drawn again (nvn_vk_objmo.c) */
static VkQueryPool g_fgt_pool; static int g_fgt_state;      /* 0 not tried, 1 ready, -1 off or failed */
static long long g_fgt_key_of[FGT_RING] = {-1, -1, -1, -1}; static int g_fgt_bits[FGT_RING];
static long long g_fgt_real;                       /* real frames presented with frame generation on: the key of a frame's marks */
static double g_fgt_sum[5]; static int64_t g_fgt_cnt[5], g_fgt_lost;
static double g_fgt_obj_last = -1;                /* 0.3.22: the latest real frame's GPU time for the moving objects (nvn_vk_objmo.c mo_budget) */
static double g_fg_late_avg;                      /* 0.4: the share of real frames more than 40 ms after the last, averaged (mo_budget) */
static double g_fg_pair_ms = -1;                  /* 0.3.22: frame generation's GPU time per real + made pair, averaged (fg_auto_tick) */
static void fgt_harvest(int r) {
    if (g_fgt_key_of[r] < 0) return;
    double pair = 0; int parts = 0;
    for (int p = 0; p < 5; p++) {
        int a = k_fgt_pair[p][0], b = k_fgt_pair[p][1];
        if (!(g_fgt_bits[r] >> a & 1) || !(g_fgt_bits[r] >> b & 1)) continue;
        uint64_t ts[2];
        if (vkGetQueryPoolResults(V.device, g_fgt_pool, (uint32_t)(r * FGT_N + a), 1, 8, &ts[0], 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            vkGetQueryPoolResults(V.device, g_fgt_pool, (uint32_t)(r * FGT_N + b), 1, 8, &ts[1], 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && ts[1] >= ts[0]) {
            g_fgt_sum[p] += (double)(ts[1] - ts[0]) * (double)V.props.limits.timestampPeriod / 1e6; g_fgt_cnt[p]++;
            if (p == 4) g_fgt_obj_last = (double)(ts[1] - ts[0]) * (double)V.props.limits.timestampPeriod / 1e6;
            pair += (double)(ts[1] - ts[0]) * (double)V.props.limits.timestampPeriod / 1e6; if (p < 4) parts++;
        } else g_fgt_lost++;
    }
    if (parts >= 3) g_fg_pair_ms = g_fg_pair_ms < 0 ? pair : 0.9 * g_fg_pair_ms + 0.1 * pair;      /* motion, HUD copy, warp, present pass (+ objects) */
    g_fgt_key_of[r] = -1; g_fgt_bits[r] = 0;
}
static void fgt_mark(long long key, int k) {       /* outside a render pass */
    if (g_fgt_state == 0) {
        const char *e = getenv("SMO_VK_FG_TIME");
        g_fgt_state = -1;
        if (!(e && !atoi(e)) && V.props.limits.timestampComputeAndGraphics) {
            VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = FGT_RING * FGT_N;
            if (vkCreateQueryPool(V.device, &qi, NULL, &g_fgt_pool) == VK_SUCCESS) g_fgt_state = 1;
        }
        printf("nvn_vk_fgtime: %s\n", g_fgt_state > 0 ? "frame generation's GPU time is measured (motion pass, HUD copy, made frame), printed every 300 made frames" : "not measured");
    }
    if (g_fgt_state < 0 || key < 0) return;
    int r = (int)(key % FGT_RING);
    if (g_fgt_key_of[r] != key) { fgt_harvest(r); g_fgt_key_of[r] = key; g_fgt_bits[r] = 0; }
    cmd_begin();
    vkCmdResetQueryPool(R.cmd, g_fgt_pool, (uint32_t)(r * FGT_N + k), 1);
    vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_fgt_pool, (uint32_t)(r * FGT_N + k));
    g_fgt_bits[r] |= 1 << k;
}
static int64_t g_fgp_last, g_fgp_n[3];             /* 0.3.13: the time between real presents with frame generation on (fg_make): on time (up to 40 ms), one refresh late (40-56 ms: its made frame stayed up two refreshes), later */
static void fgt_report(void) {                     /* fg_make, each made frame */
    static int64_t made;
    if (g_fgt_state <= 0 || ++made % 300) return;
    {   int64_t t = g_fgp_n[0] + g_fgp_n[1] + g_fgp_n[2];
        if (t) printf("nvn_vk_fgtime: real frames: %.1f%% on time (up to 40 ms after the last), %.1f%% one refresh late (their made frame stayed up two refreshes), %.1f%% later\n",
                      100.0 * g_fgp_n[0] / t, 100.0 * g_fgp_n[1] / t, 100.0 * g_fgp_n[2] / t);
        g_fgp_n[0] = g_fgp_n[1] = g_fgp_n[2] = 0; }
    double v[5];
    for (int p = 0; p < 5; p++) v[p] = g_fgt_cnt[p] ? g_fgt_sum[p] / (double)g_fgt_cnt[p] : 0;
    double obj = g_fgt_cnt[4] ? g_fgt_sum[4] / (double)g_fgt_cnt[2] : 0;      /* per made frame, counting frames without moving objects as 0 */
    printf("nvn_vk_fgtime: GPU per real frame: motion pass %.3f ms, moving objects %.3f ms, HUD copy %.3f ms; per made frame: warp %.3f ms, present pass %.3f ms; in all %.2f ms a real+made pair (%lld samples, %lld not ready)\n",
           v[0], obj, v[1], v[2], v[3], v[0] + obj + v[1] + v[2] + v[3], (long long)g_fgt_cnt[2], (long long)g_fgt_lost);
    memset(g_fgt_sum, 0, sizeof g_fgt_sum); memset(g_fgt_cnt, 0, sizeof g_fgt_cnt); g_fgt_lost = 0;
}
static int g_mv_every = -1;                        /* SMO_VK_MOTION */
static int64_t g_mv_runs;
static long long g_mv_frame_ok = -1;               /* the last frame whose vectors were made */
static int g_mv_steps = 2;                         /* 0.3.11: the game's logic steps over that frame (the vectors span them) */
static int fg_on(void);                            /* step 2, below */

static int g_mo_in_gbuf;                           /* 0.3.13: the pass being recorded is the scene's G-buffer pass */
static int64_t g_fg_scene_px;                      /* 0.3.22: the last scene depth's pixels (fg_auto_tick's first estimate) */
static void mv_note_pass(void) {                   /* pass_begin, render thread */
    /* the frame's FIRST such pass: the G-buffer comes first; a later pass with as many targets (S24, Cascade: one at 1600x900)
     * had taken its place and given NaN motion */
    if (!g_mv_depth && R.ncol >= 4 && R.depth.tex && !R.depth.tex->dead && R.depth.tex->vk) { g_mv_depth = R.depth.tex; g_fg_scene_px = (int64_t)((VTex *)R.depth.tex->vk)->w * ((VTex *)R.depth.tex->vk)->h; }
    g_mo_in_gbuf = g_mv_depth && R.ncol >= 4 && R.depth.tex == g_mv_depth;      /* (again after a split of the pass) */
}

#include "nvn_vk_objmo.c"                           /* 0.3.13: per-object motion (frame generation step 4b) */
static void mv_vp(const float view[12], const float proj[16], double out[16]) {      /* proj (4x4) times view (3x4 with 0 0 0 1), row-major */
    static double fs = -1;                         /* SMO_VK_MOTION_FOCAL=<s> (test): the projection's x and y scale times s */
    if (fs < 0) { const char *e = getenv("SMO_VK_MOTION_FOCAL"); fs = e && atof(e) > 0 ? atof(e) : 1.0; if (fs != 1.0) printf("nvn_vk_motion: focal length x %.3f (test)\n", fs); }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        double v = c == 3 ? proj[r * 4 + 3] : 0;
        for (int k = 0; k < 3; k++) v += (double)proj[r * 4 + k] * (r < 2 ? fs : 1.0) * view[k * 4 + c];
        out[r * 4 + c] = v;
    }
}
static int mv_clip_to_clip(const double cur[16], const double prev[16], float out[16]) {      /* prev * inverse(cur), column-major for GLSL */
    double a[4][8];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 8; c++) { a[r][c] = c < 4 ? cur[r * 4 + c] : (c - 4 == r); if (!isfinite(a[r][c])) return 0; }
    for (int i = 0; i < 4; i++) {
        int pv = i;
        for (int r = i + 1; r < 4; r++) if (fabs(a[r][i]) > fabs(a[pv][i])) pv = r;
        if (fabs(a[pv][i]) < 1e-12) return 0;
        for (int c = 0; c < 8; c++) { double t = a[i][c]; a[i][c] = a[pv][c]; a[pv][c] = t; }
        double d = a[i][i];
        for (int c = 0; c < 8; c++) a[i][c] /= d;
        for (int r = 0; r < 4; r++) if (r != i) { double m = a[r][i]; for (int c = 0; c < 8; c++) a[r][c] -= m * a[i][c]; }
    }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        double x = 0;
        for (int k = 0; k < 4; k++) x += prev[r * 4 + k] * a[k][c + 4];
        if (!isfinite(x)) return 0;
        out[c * 4 + r] = (float)x;
    }
    return 1;
}
static int mv_init(void) {
    if (g_mv_state) return g_mv_state > 0;
    g_mv_state = -1;
    static const char glsl[] =
        "#version 450\n"
        "layout(local_size_x = 64) in;\n"      /* one row of 64: an 8x8 group ran as 8x1 on Qualcomm's driver (the SPIR-V 1.6 LocalSizeId form) */
        "layout(binding = 0) uniform sampler2D depth_tex;\n"
        "layout(binding = 1, rg16f) uniform writeonly image2D motion;\n"
        "layout(binding = 2, r32f) uniform writeonly image2D dist;\n"           /* 0.3.14: the distance, for the forward pass of frame generation */
        "layout(binding = 3) uniform sampler2D depth_snap;\n"                  /* the depth as the G-buffer pass left it (or the same as depth_tex) */
        /* m: clip to last frame's clip, the depth mapping folded in (input (ndc, d, 1)). k: the projection's x and y scale, and
         * A', B' with view z = -B' / (d + A') (the depth mapping folded in too). Step 4 (0.3.11), Mario: c = the centre of his
         * capsule in view space and its radius, s = his step since the last frame (view space) and the capsule's half height,
         * u = up in view space and 1 when he is known. A pixel inside the capsule was at (its point - his step) a frame ago. */
        "layout(push_constant) uniform P { mat4 m; vec4 k, c, s, u; } pc;\n"
        "void main() {\n"
        "    ivec2 p = ivec2(gl_GlobalInvocationID.xy), ext = imageSize(motion);\n"
        "    if (any(greaterThanEqual(p, ext))) return;\n"
        "    vec2 sz = vec2(ext), uv = (vec2(p) + 0.5) / sz, ndc = uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0);\n"
        "    float d = texelFetch(depth_tex, p, 0).r;\n"
        "    { float ds = texelFetch(depth_snap, p, 0).r, vzs = -pc.k.w / (ds + pc.k.z); imageStore(dist, p, vec4(vzs < 0.0 ? -vzs : 3.0e38, 0.0, 0.0, 0.0)); }\n"
        "    vec4 cur = vec4(ndc, d, 1.0);\n"
        "    if (pc.u.w > 0.5) {\n"
        "        float vz = -pc.k.w / (d + pc.k.z);\n"
        "        vec3 v = vec3(ndc.x * -vz / pc.k.x, ndc.y * -vz / pc.k.y, vz), r = v - pc.c.xyz;\n"
        "        float h = clamp(dot(r, pc.u.xyz), -pc.s.w, pc.s.w);\n"
        "        if (dot(r - h * pc.u.xyz, r - h * pc.u.xyz) < pc.c.w * pc.c.w) {\n"
        "            vec3 o = v - pc.s.xyz;\n"
        "            cur = vec4(pc.k.x * o.x, pc.k.y * o.y, pc.k.z * o.z + pc.k.w, -o.z);\n"
        "        }\n"
        "    }\n"
        "    vec4 old = pc.m * cur;\n"
        "    vec2 mv = old.w > 1e-6 ? (old.xy / old.w - ndc) * sz * vec2(0.5, -0.5) : vec2(0.0);\n"
        "    imageStore(motion, p, vec4(clamp(mv, -sz, sz), 0.0, 0.0));\n"
        "}\n";
    uint64_t h;
    VkShaderModule mod = module_from_glsl(5, glsl, "motion vectors", &h, NULL);
    if (!mod) { printf("nvn_vk_motion: the motion shader could not be made: no motion vectors\n"); return 0; }
    VkDescriptorSetLayoutBinding b[4] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 4; li.pBindings = b;
    VKC(vkCreateDescriptorSetLayout(V.device, &li, NULL, &g_mv_dl));
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &g_mv_dl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr;
    VKC(vkCreatePipelineLayout(V.device, &pl, NULL, &g_mv_pl));
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = mod; ci.stage.pName = "main"; ci.layout = g_mv_pl;
    VkResult r = vkCreateComputePipelines(V.device, g_pcache, 1, &ci, NULL, &g_mv_pipe);
    vkDestroyShaderModule(V.device, mod, NULL);
    if (r != VK_SUCCESS) { printf("nvn_vk_motion: the motion pipeline was refused (%s): no motion vectors\n", vk_result_name(r)); return 0; }
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * NSLOT}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * NSLOT}};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = NSLOT; pi.poolSizeCount = 2; pi.pPoolSizes = ps;
    VKC(vkCreateDescriptorPool(V.device, &pi, NULL, &g_mv_pool));
    for (int i = 0; i < NSLOT; i++) {
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = g_mv_pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_mv_dl;
        VKC(vkAllocateDescriptorSets(V.device, &ai, &g_mv_set[i]));
    }
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxLod = 0;
    VKC(vkCreateSampler(V.device, &si, NULL, &g_mv_smp));
    g_mv_state = 1;
    printf("nvn_vk_motion: camera motion vectors ready (compute 64x1, RG16F)\n");
    return 1;
}
static int mv_aux(int i, VkFormat f, VkImageUsageFlags u, int w, int h) {      /* 0.3.14: one of g_mv_aimg */
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements rq;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = f; ci.extent.width = (uint32_t)w; ci.extent.height = (uint32_t)h; ci.extent.depth = 1;
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = u; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(V.device, &ci, NULL, &g_mv_aimg[i]) != VK_SUCCESS) { g_mv_aimg[i] = VK_NULL_HANDLE; return 0; }
    vkGetImageMemoryRequirements(V.device, g_mv_aimg[i], &rq);
    g_mv_amem[i] = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VKC(vkBindImageMemory(V.device, g_mv_aimg[i], g_mv_amem[i].mem, g_mv_amem[i].off));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g_mv_aimg[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = f;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    VKC(vkCreateImageView(V.device, &vi, NULL, &g_mv_aview[i]));
    return 1;
}
static int mv_image(int w, int h) {                /* the motion target, the scene depth's size */
    if (g_mv_img && g_mv_w == w && g_mv_h == h) return 1;
    if (g_mv_img) { cmd_wait_all(); vkDestroyImageView(V.device, g_mv_view, NULL); vkDestroyImage(V.device, g_mv_img, NULL); vmem_free(g_mv_mem); g_mv_img = VK_NULL_HANDLE; }
    g_fs_pdist_ok = 0;
    for (int i = 0; i < 5; i++) if (g_mv_aimg[i]) { cmd_wait_all(); vkDestroyImageView(V.device, g_mv_aview[i], NULL); vkDestroyImage(V.device, g_mv_aimg[i], NULL); vmem_free(g_mv_amem[i]); g_mv_aimg[i] = VK_NULL_HANDLE; g_mv_aview[i] = VK_NULL_HANDLE; }
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements rq;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = VK_FORMAT_R16G16_SFLOAT; ci.extent.width = (uint32_t)w; ci.extent.height = (uint32_t)h; ci.extent.depth = 1;
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;      /* 0.3.13: the moving objects are drawn into it (nvn_vk_objmo.c) */
    if (vkCreateImage(V.device, &ci, NULL, &g_mv_img) != VK_SUCCESS) { g_mv_img = VK_NULL_HANDLE; return 0; }
    vkGetImageMemoryRequirements(V.device, g_mv_img, &rq);
    g_mv_mem = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VKC(vkBindImageMemory(V.device, g_mv_img, g_mv_mem.mem, g_mv_mem.off));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g_mv_img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R16G16_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    VKC(vkCreateImageView(V.device, &vi, NULL, &g_mv_view));
    g_mv_w = w; g_mv_h = h;
    if (!mv_aux(0, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, w, h) || !mv_aux(4, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, w, h) || !mv_aux(1, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, w, h) ||
        !mv_aux(2, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, w, h) || !mv_aux(3, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, w, h)) { printf("nvn_vk_motion: the forward images could not be made: no motion vectors\n"); vkDestroyImageView(V.device, g_mv_view, NULL); vkDestroyImage(V.device, g_mv_img, NULL); vmem_free(g_mv_mem); g_mv_img = VK_NULL_HANDLE; return 0; }
    pass_end(); cmd_begin();
    image_barrier(g_mv_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    for (int i = 0; i < 5; i++) image_barrier(g_mv_aimg[i], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    return 1;
}
static float mv_half(uint16_t hv) {
    uint32_t s = (uint32_t)(hv >> 15) << 31, e = (hv >> 10) & 31, m = hv & 1023, f;
    if (!e) { if (!m) f = s; else { e = 1; while (!(m & 1024)) { m <<= 1; e--; } m &= 1023; f = s | ((e + 112) << 23) | (m << 13); } }
    else if (e == 31) f = s | 0x7f800000u | (m << 13);
    else f = s | ((e + 112) << 23) | (m << 13);
    float r; memcpy(&r, &f, 4); return r;
}
static const char *g_mv_shot_tag = "";            /* 0.3.13: "_cam" for the picture before the moving objects are drawn again */
static void mv_shot(long long frame, const double vp[16]) {      /* the vectors read back: a picture and a summary line */
    uint64_t size = (uint64_t)g_mv_w * g_mv_h * 4;
    VMem bm;
    VkBuffer buf = vbuf_new(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bm);
    if (!buf) return;
    image_barrier(g_mv_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c;
    memset(&c, 0, sizeof c);
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1;
    c.imageExtent.width = (uint32_t)g_mv_w; c.imageExtent.height = (uint32_t)g_mv_h; c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(R.cmd, g_mv_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &c);
    image_barrier(g_mv_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    cmd_wait_all();
    const uint16_t *px = bm.map;
    int w = g_mv_w, h = g_mv_h, row = (w * 3 + 3) & ~3;
    uint8_t *bmp = calloc((size_t)row * h + 54, 1);
    double sum = 0, mx = 0, sx = 0, sy = 0; int64_t moving = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float fx = mv_half(px[(y * w + x) * 2]), fy = mv_half(px[(y * w + x) * 2 + 1]);
        double m = sqrt((double)fx * fx + (double)fy * fy);
        sum += m; sx += fx; sy += fy; if (m > mx) mx = m; if (m > 0.25) moving++;
        if (bmp) {
            uint8_t *o = bmp + 54 + (size_t)(h - 1 - y) * row + (size_t)x * 3;      /* BMP rows bottom-up, BGR */
            int r = 128 + (int)lrintf(fx * 4.0f), g = 128 + (int)lrintf(fy * 4.0f);
            o[2] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r); o[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g); o[0] = 128;
        }
    }
    double n = (double)w * h;
    printf("nvn_vk_motion: frame %lld: %dx%d, motion mean %.2f px (average vector %.2f, %.2f), largest %.1f px, %.1f%% of pixels moving more than 0.25 px | view-projection row 0: %.3f %.3f %.3f %.3f\n",
           frame, w, h, sum / n, sx / n, sy / n, mx, 100.0 * (double)moving / n, vp[0], vp[1], vp[2], vp[3]);
    if (bmp) {
        char path[320]; const char *dir = getenv("SMO_SHOT_DIR");
        snprintf(path, sizeof path, "%s%smotion_%05lld%s.bmp", dir ? dir : "", dir ? "/" : "", frame, g_mv_shot_tag);
        uint32_t fsz = (uint32_t)(54 + (size_t)row * h);
        uint8_t *hd = bmp;
        hd[0] = 'B'; hd[1] = 'M'; memcpy(hd + 2, &fsz, 4); uint32_t off = 54, hs = 40; memcpy(hd + 10, &off, 4); memcpy(hd + 14, &hs, 4);
        int32_t ww = w, hh = h; memcpy(hd + 18, &ww, 4); memcpy(hd + 22, &hh, 4); uint16_t planes = 1, bpp = 24; memcpy(hd + 26, &planes, 2); memcpy(hd + 28, &bpp, 2);
        FILE *f = fopen(path, "wb"); if (f) { fwrite(bmp, 1, fsz, f); fclose(f); }
        free(bmp);
        /* 0.3.11: the raw vectors too (RG16F, rows top-down, no header): the picture clamps at 32 px */
        snprintf(path, sizeof path, "%s%smotion_%05lld%s_%dx%d.rg16f", dir ? dir : "", dir ? "/" : "", frame, g_mv_shot_tag, w, h);
        if ((f = fopen(path, "wb"))) { fwrite(px, 1, (size_t)size, f); fclose(f); }
    }
    vkDestroyBuffer(V.device, buf, NULL); vmem_free(bm);
}
static int mv_shot_frame(long long frame) {
    static char list[256]; static int read;
    if (!read) { read = 1; const char *e = getenv("SMO_VK_MOTION_SHOT"); if (e) snprintf(list, sizeof list, "%s", e); }
    if (!list[0]) return 0;
    for (const char *p = list; *p; ) { if (atoll(p) == frame) return 1; p = strchr(p, ','); if (!p) break; p++; }
    return 0;
}
/* 0.3.11, step 4 research: the scene depth's stencil at a shot frame, as stencil_<frame>.bmp (each value its own grey: value * 37
 * mod 256 so that neighbouring bits differ) and a count of each value, to see whether the game marks the player's pixels. */
static void mv_stencil_shot(VTex *dv, long long frame) {
    if (dv->fmt != VK_FORMAT_D24_UNORM_S8_UINT && dv->fmt != VK_FORMAT_D32_SFLOAT_S8_UINT) { printf("nvn_vk_motion: frame %lld: the scene depth has no stencil (format %d)\n", frame, (int)dv->fmt); return; }
    int w = dv->w, h = dv->h;
    VMem bm; VkBuffer buf = vbuf_new((uint64_t)w * h, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bm);
    if (!buf) return;
    VkImageLayout was = dv->layout;
    vt_layout(dv, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    pass_end(); cmd_begin();
    VkBufferImageCopy c; memset(&c, 0, sizeof c);
    c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT; c.imageSubresource.layerCount = 1;
    c.imageExtent.width = (uint32_t)w; c.imageExtent.height = (uint32_t)h; c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(R.cmd, dv->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &c);
    vt_layout(dv, was);
    cmd_wait_all();
    const uint8_t *s = bm.map;
    int64_t cnt[256] = {0}; int64_t sx[256] = {0}, sy[256] = {0};
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { uint8_t v = s[(size_t)y * w + x]; cnt[v]++; sx[v] += x; sy[v] += y; }
    char line[1024]; int n = 0;
    for (int v = 0; v < 256 && n < (int)sizeof line - 64; v++) if (cnt[v]) n += snprintf(line + n, sizeof line - n, " 0x%02x: %.2f%% (centre %lld,%lld)", v, 100.0 * (double)cnt[v] / ((double)w * h), (long long)(sx[v] / cnt[v]), (long long)(sy[v] / cnt[v]));
    printf("nvn_vk_motion: frame %lld: stencil %dx%d:%s\n", frame, w, h, line);
    int row = (w * 3 + 3) & ~3; uint8_t *bmp = calloc((size_t)row * h + 54, 1);
    if (bmp) {
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { uint8_t g = (uint8_t)(s[(size_t)y * w + x] * 37u); uint8_t *o = bmp + 54 + (size_t)(h - 1 - y) * row + (size_t)x * 3; o[0] = o[1] = o[2] = g; }
        uint32_t fsz = (uint32_t)(54 + (size_t)row * h), off = 54, hs = 40; int32_t ww = w, hh = h; uint16_t planes = 1, bpp = 24;
        bmp[0] = 'B'; bmp[1] = 'M'; memcpy(bmp + 2, &fsz, 4); memcpy(bmp + 10, &off, 4); memcpy(bmp + 14, &hs, 4); memcpy(bmp + 18, &ww, 4); memcpy(bmp + 22, &hh, 4); memcpy(bmp + 26, &planes, 2); memcpy(bmp + 28, &bpp, 2);
        char path[320]; const char *dir = getenv("SMO_SHOT_DIR");
        snprintf(path, sizeof path, "%s%sstencil_%05lld.bmp", dir ? dir : "", dir ? "/" : "", frame);
        FILE *f = fopen(path, "wb"); if (f) { fwrite(bmp, 1, fsz, f); fclose(f); }
        free(bmp);
    }
    vkDestroyBuffer(V.device, buf, NULL); vmem_free(bm);
}
/* vk_present2, before the present pass: the vectors of this frame (when asked for), then this camera becomes the last one */
static void mv_present(const PresentJob *j, long long frame) {
    if (g_mv_every < 0) { const char *e = getenv("SMO_VK_MOTION"); g_mv_every = e && atoi(e); if (g_mv_every) printf("nvn_vk_motion: SMO_VK_MOTION=1: camera motion vectors every frame\n"); }
    Tex *dt = g_mv_depth;
    g_mv_depth = NULL;
    if (fg_on()) g_fgt_real++;                     /* 0.3.12: this real frame's key for the GPU timestamps */
    if (!j->cam_ok) { g_mv_have_prev = 0; g_mv_prev_plok = 0; return; }
    double vp[16];
    mv_vp(j->cam_view, j->cam_proj, vp);
    {   /* SMO_VK_FG_LOG=<from>-<to>: each present's camera in those frames (the view matrix's translation and first row), to
         * see whether consecutive presents get evenly spaced cameras */
        static long long lf = -2, lt;
        if (lf == -2) { const char *e = getenv("SMO_VK_FG_LOG"); lf = e ? atoll(e) : -1; lt = e && strchr(e, '-') ? atoll(strchr(e, '-') + 1) : lf; }
        if (lf >= 0 && frame >= lf && frame <= lt)
            printf("nvn_vk_fgcam: frame %lld: view row 0 %.4f %.4f %.4f translation %.2f %.2f %.2f | depth %s\n", frame, j->cam_view[0], j->cam_view[1], j->cam_view[2],
                   j->cam_view[3], j->cam_view[7], j->cam_view[11], dt ? "yes" : "none");
    }
    int want = g_mv_every || fg_on() || mv_shot_frame(frame);
    float m[32];                                   /* the push constants: the matrix, then k, c, s, u (the shader's comment) */
    float zx, zy;
    {   /* SMO_VK_MOTION_ZMAP: 1 = z = 2d - 1 (the default since 0.3.11: measured, see the top of this file), 0 = z = d */
        static int zm = -1;
        if (zm < 0) { const char *e = getenv("SMO_VK_MOTION_ZMAP"); zm = e ? atoi(e) != 0 : 1; }
        zx = zm ? 2.0f : 1.0f; zy = zm ? -1.0f : 0.0f;
    }
    memset(m, 0, sizeof m);
    int mario = 0, cc_ok = 0;
    if (want && g_mv_have_prev && dt && !dt->dead && dt->vk && (cc_ok = mv_clip_to_clip(vp, g_mv_prev_vp, m))) {
        /* the depth mapping folded into the matrix: input (ndc, d, 1), z = zx d + zy (column 2 times zx, column 3 plus zy x column 2) */
        for (int r = 0; r < 4; r++) { m[12 + r] += zy * m[8 + r]; m[8 + r] *= zx; }
        const float *P = j->cam_proj, *Vw = j->cam_view;
        m[16] = P[0]; m[17] = P[5]; m[18] = (zy + P[10]) / zx; m[19] = P[11] / zx;
        /* step 4 (0.3.11): Mario's own motion, inside a capsule around him. SMO_VK_FG_MARIO=0 turns it off; =<r>,<h> sets the
         * capsule's radius and height in game units (default 60,180) */
        static int mon = -1; static float mr = 60.0f, mh = 180.0f;      /* 0.3.13: by default only while per-object motion is off (nvn_vk_objmo.c gives Mario his own) */
        if (mon < 0) { const char *e = getenv("SMO_VK_FG_MARIO"); mon = !e ? 2 : !!strcmp(e, "0"); if (e && strchr(e, ',')) { mr = (float)atof(e); mh = (float)atof(strchr(e, ',') + 1); } if (mh < 2 * mr) mh = 2 * mr; }
        if ((mon == 1 || (mon == 2 && !mo_obj_on())) && j->pl_ok && g_mv_prev_plok && fabsf(P[14] + 1.0f) < 1e-3f && fabsf(P[2]) < 1e-3f && fabsf(P[6]) < 1e-3f && P[0] > 0 && P[5] > 0) {
            float st[3] = {j->pl[0] - g_mv_prev_pl[0], j->pl[1] - g_mv_prev_pl[1], j->pl[2] - g_mv_prev_pl[2]};
            if (st[0] * st[0] + st[1] * st[1] + st[2] * st[2] < 300.0f * 300.0f) {      /* more is a warp or a respawn */
                float cw[3] = {j->pl[0], j->pl[1] + mh * 0.5f, j->pl[2]};                /* SMO's up is +y */
                for (int r = 0; r < 3; r++) {
                    m[20 + r] = Vw[r * 4] * cw[0] + Vw[r * 4 + 1] * cw[1] + Vw[r * 4 + 2] * cw[2] + Vw[r * 4 + 3];
                    m[24 + r] = Vw[r * 4] * st[0] + Vw[r * 4 + 1] * st[1] + Vw[r * 4 + 2] * st[2];
                    m[28 + r] = Vw[r * 4 + 1];
                }
                m[23] = mr; m[27] = mh * 0.5f - mr; m[31] = 1.0f;
                mario = 1;
            }
        }
    }
    memcpy(g_mv_prev_pl, j->pl, sizeof g_mv_prev_pl); g_mv_prev_plok = j->pl_ok;
    if (mv_shot_frame(frame)) printf("nvn_vk_motion: frame %lld: Mario %s at %.1f %.1f %.1f, step %.1f %.1f %.1f (view)\n", frame, mario ? "moved on his own" : "not used",
                                     j->pl[0], j->pl[1], j->pl[2], m[24], m[25], m[26]);
    if (cc_ok && mv_init()) {
        VTex *dv = dt->vk;
        if (mv_image(dv->w, dv->h)) {
            if (g_mv_dview_img != dv->img) {           /* a depth-only view (the image may hold stencil too) */
                if (g_mv_dview) { cmd_wait_all(); vkDestroyImageView(V.device, g_mv_dview, NULL); g_mv_dview = VK_NULL_HANDLE; }
                VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                vi.image = dv->img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = dv->fmt;
                vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
                if (vkCreateImageView(V.device, &vi, NULL, &g_mv_dview) == VK_SUCCESS) g_mv_dview_img = dv->img; else { g_mv_dview = VK_NULL_HANDLE; g_mv_dview_img = VK_NULL_HANDLE; }
            }
            if (g_mv_dview) {
                vt_layout(dv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);      /* ends a pass and begins a command buffer if needed */
                pass_end(); cmd_begin();
                VkDescriptorSet set = g_mv_set[R.cur];
                VkDescriptorImageInfo di = {g_mv_smp, g_mv_dview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, mi = {VK_NULL_HANDLE, g_mv_view, VK_IMAGE_LAYOUT_GENERAL};
                VkDescriptorImageInfo xi = {VK_NULL_HANDLE, g_mv_aview[0], VK_IMAGE_LAYOUT_GENERAL},
                                      si2 = {g_mv_smp, g_mo_ds_fresh && g_mo_ds_view && g_mo_ds_w == dv->w && g_mo_ds_h == dv->h ? g_mo_ds_view : g_mv_dview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet wr[4] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di, NULL, NULL},
                                              {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &mi, NULL, NULL},
                                              {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &xi, NULL, NULL},
                                              {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &si2, NULL, NULL}};
                vkUpdateDescriptorSets(V.device, 4, wr, 0, NULL);
                {   /* what the passes wrote (the depth: light barriers order it for fragment shaders only, g_cs_debt) and the
                     * last frame's write of the motion image, before this dispatch */
                    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
                    g_cs_debt = 0;
                }
                if (fg_on()) fgt_mark(g_fgt_real, 0);
                vkCmdBindPipeline(R.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_mv_pipe);
                vkCmdBindDescriptorSets(R.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_mv_pl, 0, 1, &set, 0, NULL);
                vkCmdPushConstants(R.cmd, g_mv_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, m);
                vkCmdDispatch(R.cmd, (uint32_t)(g_mv_w + 63) / 64, (uint32_t)g_mv_h, 1);      /* a group of 64 pixels per row piece */
                if (fg_on()) fgt_mark(g_fgt_real, 1);
                R.pipe_bound = NULL;                   /* (the graphics state cache: a new bind after this) */
                g_mv_runs++;
                g_mv_frame_ok = frame;                 /* step 2: this frame has its vectors (fg_present) */
                g_mv_steps = j->cam_steps >= 1 && j->cam_steps <= 8 ? j->cam_steps : 2;
                {   const float *Vc = j->cam_view;          /* the camera's place: -R^T t of the view matrix (rows R, translation t) */
                    for (int k = 0; k < 3; k++) g_mo_campos[k] = -(Vc[k] * Vc[3] + Vc[4 + k] * Vc[7] + Vc[8 + k] * Vc[11]);
                    memcpy(g_mo_marpos, j->pl, sizeof g_mo_marpos); g_mo_marok = j->pl_ok; }
                if (mv_shot_frame(frame) && mo_obj_on()) { g_mv_shot_tag = "_cam"; mv_shot(frame, vp); g_mv_shot_tag = ""; }      /* (the camera's motion alone, to compare) */
                mo_replay(frame);                      /* 0.3.13: the moving objects' own motion over the camera's (nvn_vk_objmo.c) */
                if (mv_shot_frame(frame)) {
                    mv_shot(frame, vp);
                    const float *P = j->cam_proj, *Vw = j->cam_view;
                    printf("nvn_vk_motion: frame %lld: steps %d, projection rows %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f; view rows %.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f\n",
                           frame, j->cam_steps, P[0], P[1], P[2], P[3], P[4], P[5], P[6], P[7], P[8], P[9], P[10], P[11], P[12], P[13], P[14], P[15],
                           Vw[0], Vw[1], Vw[2], Vw[4], Vw[5], Vw[6], Vw[8], Vw[9], Vw[10]);
                    mv_stencil_shot(dv, frame);
                }
            }
        }
    }
    memcpy(g_mv_prev_vp, vp, sizeof vp); g_mv_have_prev = 1;
}

/* ---------------------------------------------------------------- step 2: the generated frame
 * After a real frame is presented, one more frame is made from it and presented right after it: the window texture (the whole
 * picture, the HUD too for now: step 3 draws the HUD fresh) warped half a frame ahead along the camera motion of this frame
 * (extrapolation: nothing waits for the next real frame). The swapchain is FIFO while this is on (nvn_vk.c), so the display
 * shows the real frame for one refresh and the made one for the next: at 30 real frames a second on 60 Hz, 60 on screen.
 *   Warp, per output pixel q (a gather): the content that will be at q came from p with p - t * mv(p) = q (mv: where a pixel was
 *   a frame ago minus where it is, so its motion over the next frame is -mv), solved with two steps p = q + t * mv(p) from p = q.
 *   Uncovered places take what the gather finds there (a stretch of the neighbours), screen edges clamp.
 * The made frame goes through the same present pass as a real one (upscaling, sharpening, touch controls), as a VTex that shares
 * the window texture's description and has an image of its own (g_fgvt). SMO_VK_FRAMEGEN=1 turns it on (test switch);
 * SMO_VK_FRAMEGEN_T=<t> a fixed step ahead in frames instead.
 * How far ahead (0.3.11): the made frame is shown one refresh after the real one, and the game runs one 1/60 s logic step per
 * refresh, so it is ONE STEP ahead: t = 1 / (the frame's logic steps), not a fixed half frame. Measured on the S24 (fps cap 30,
 * steady speed, camera turning): 82 of 1800 frames had 1 or 3 steps instead of 2 (a real frame a little early or late), and a
 * 3-step frame's vectors are 1.5x as long (48 px against 32). With t = 0.5 its made frame went 1.5 steps ahead, and the next real
 * frame only a little further: big step, small step, seen as shaking. */
static void save_bmp_vk(const char *path, VTex *vt);      /* nvn_vk_draw.c, further down */
static int g_fg_on = -1; static float g_fg_t = 0.0f;      /* 0 = one logic step (the default) */
static int64_t g_fg_tsteps[4];                    /* made frames by the steps of their real frame (1, 2, 3+), for the log */
static int g_fg_env = -2, g_fg_saved_mode = -1;
/* 0.3.12: the setting, graphics.ini frame_generation (the in-game menu changes it live; SMO_VK_FRAMEGEN=0/1 wins). Checked at
 * each real present (vk_present2, before anything of the frame is presented): on a change the present mode becomes FIFO (each real
 * and made frame one refresh) or goes back to the one chosen at the start, and the swapchain is made again. */
static void fg_update(void) {
    if (g_fg_env == -2) {
        const char *e = getenv("SMO_VK_FRAMEGEN"); g_fg_env = e ? atoi(e) != 0 : -1;
        if ((e = getenv("SMO_VK_FRAMEGEN_T")) && atof(e) > 0 && atof(e) <= 1.0) g_fg_t = (float)atof(e);
    }
    int want = g_fg_env >= 0 ? g_fg_env : gfx_framegen_on();      /* 0.3.22: auto included (fg_auto_tick) */
    if (want == g_fg_on) return;
    int first = g_fg_on < 0;
    g_fg_on = want;
    if (want && g_fg_t > 0) printf("nvn_vk_fg: frame generation on: one frame made after each real frame, %.2f of a frame ahead along the camera motion (FIFO presents)\n", g_fg_t);
    else if (want) printf("nvn_vk_fg: frame generation on: one frame made after each real frame, one logic step ahead along the camera and Mario's motion (FIFO presents)\n");
    else if (!first) printf("nvn_vk_fg: frame generation off\n");
    if (want) { if (V.mode != VK_PRESENT_MODE_FIFO_KHR) { g_fg_saved_mode = V.mode; V.mode = VK_PRESENT_MODE_FIFO_KHR; } }
    else if (g_fg_saved_mode >= 0) { V.mode = g_fg_saved_mode; g_fg_saved_mode = -1; }
    else if (first) return;
    if (V.swapchain) { cmd_wait_all(); swapchain_destroy(); }      /* made again with the new mode at this present */
    g_mv_have_prev = 0; g_mv_prev_plok = 0;          /* no motion across the switch */
}
static int fg_on(void) {
    if (g_fg_on < 0) fg_update();
    return g_fg_on;
}
static VTex g_fgvt;                                /* the made frame: the window texture's description, its own image */
static VkImage g_fg_img; static VMem g_fg_mem; static VkImageView g_fg_store, g_fg_srcview; static VkImage g_fg_srcview_img;
static VkDescriptorSetLayout g_fg_dl; static VkPipelineLayout g_fg_pl; static VkPipeline g_fg_pipe; static VkDescriptorPool g_fg_pool;
static VkDescriptorSet g_fg_set[NSLOT]; static VkSampler g_fg_lin;
static int g_fg_state;                             /* 0 not tried, 1 ready, -1 failed */
static int64_t g_fg_made, g_fg_skipped;
static VkFormat fg_raw(VkFormat f) {               /* the same bytes as UNORM (storage images cannot be sRGB), 0 = not handled */
    return f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM : f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_UNDEFINED;
}
/* 0.3.14: THE FORWARD PASS. The warp builds each pixel of the made frame by looking up the motion AT THAT PIXEL and fetching the
 * picture from where it points. Where the motion is smooth (the camera) that is right; for a moving part (an arm, a walking
 * creature) the pixel it moves to still holds the background's motion, and the pixel it leaves still points at itself, so moving
 * parts stayed where they were in the made frame: the user saw Mario's and the enemies' animations at 30 while the camera was smooth
 * (the per-object motion data was right: measured on the S24, ~2000 of Mario's pixels got their own vectors). Now every pixel of
 * the motion image is pushed to where it lands in the made frame (x - t m); where several land on one pixel the nearest to the camera
 * wins (an atomic minimum of the distance the motion pass wrote, then a second pass stores the winner's vector). The warp uses the
 * winner's vector; a gap (a place something moved away from) takes its farthest landed neighbour's (the background), else one step
 * back along its own vector. SMO_VK_FG_SCATTER=0: the old look-up. */
static VkDescriptorSetLayout g_fs_dl; static VkPipelineLayout g_fs_pl; static VkPipeline g_fs_pipe; static VkDescriptorPool g_fs_pool;
static VkDescriptorSet g_fs_set[NSLOT]; static int g_fs_ok;
static int fs_init(void) {
    static int want = -1;
    if (want < 0) { const char *e = getenv("SMO_VK_FG_SCATTER"); want = !(e && !atoi(e)); }
    if (!want) return 1;
    static const char glsl[] =
        "#version 450\n"
        "layout(local_size_x = 64) in;\n"
        "layout(binding = 0, rg16f) uniform readonly image2D motion;\n"
        "layout(binding = 1, r32f) uniform readonly image2D dist;\n"
        "layout(binding = 2, r32ui) uniform coherent uimage2D fkey;\n"
        "layout(binding = 3, rg16f) uniform image2D fmv;\n"
        "layout(binding = 5, r32f) uniform readonly image2D pdist;\n"      /* the last frame's distance; a.z 1 = it is there */
        "layout(binding = 4, rgba16f) uniform writeonly image2D ffin;\n"      /* xy the vector, z 1: fetch from the last real frame (fg_make) */
        "layout(push_constant) uniform P { vec4 a; } pc;\n"      /* a.x: steps ahead; a.y: 0 push forward, 1 the gaps filled */
        "vec2 dec(uint k, vec2 own) { uint mx = (k >> 10) & 1023u, my = k & 1023u; if (mx == 1023u && my == 1023u) return own; vec2 s = (vec2(float(mx), float(my)) - 512.0) / 22.58; return sign(s) * s * s; }\n"
        "void main() {\n"
        "    ivec2 x = ivec2(gl_GlobalInvocationID.xy), ext = imageSize(motion);\n"
        "    if (any(greaterThanEqual(x, ext))) return;\n"
        "    vec2 m = imageLoad(motion, x).xy;\n"
        "    if (pc.a.y > 0.5) {\n"      /* a gap (something moved away): its farthest landed neighbour's vector (the background), else one step back along its own */
        "        uint kq = imageLoad(fkey, x).r; vec2 f = m; float fl = 0.0;\n"
        "        if (kq != 0xffffffffu) f = dec(kq, m);\n"
        "        else {\n"
        "            uint best = 0u; for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) { uint kn = imageLoad(fkey, clamp(x + ivec2(i, j), ivec2(0), ext - 1)).r; if (kn != 0xffffffffu && kn >= best) { best = kn; f = dec(kn, m); } }\n"
        "            ivec2 s = clamp(ivec2(floor(vec2(x) + 0.5 + pc.a.x * f)), ivec2(0), ext - 1);\n"
        "            float dx = imageLoad(dist, x).r;\n"
        "            if (imageLoad(dist, s).r < 0.85 * dx) {\n"
        "                ivec2 sp = clamp(ivec2(floor(vec2(x) + 0.5 + (pc.a.x + 1.0) * f)), ivec2(0), ext - 1);\n"
        "                if (dx < 1.0e30 && pc.a.z > 0.5 && imageLoad(pdist, sp).r >= 0.85 * dx) fl = 1.0; else f = vec2(0.0);\n"
        "            }\n"
        "        }\n"
        "        if (fl < 0.5) { ivec2 s = clamp(ivec2(floor(vec2(x) + 0.5 + pc.a.x * f)), ivec2(0), ext - 1); float ds = imageLoad(dist, s).r; vec2 away = vec2(0.0);\n"
        "          for (int r = 1; r <= 2; r++) { if (imageLoad(dist, clamp(s + ivec2(r, 0), ivec2(0), ext - 1)).r < 0.85 * ds) away.x -= 1.0; if (imageLoad(dist, clamp(s - ivec2(r, 0), ivec2(0), ext - 1)).r < 0.85 * ds) away.x += 1.0;\n"
        "                                      if (imageLoad(dist, clamp(s + ivec2(0, r), ivec2(0), ext - 1)).r < 0.85 * ds) away.y -= 1.0; if (imageLoad(dist, clamp(s - ivec2(0, r), ivec2(0), ext - 1)).r < 0.85 * ds) away.y += 1.0; }\n"
        "          if (dot(away, away) > 0.0 && pc.a.x > 0.01) { vec2 d = normalize(away) * 2.0; ivec2 s2 = clamp(ivec2(floor(vec2(s) + 0.5 + d)), ivec2(0), ext - 1); if (imageLoad(dist, s2).r >= 0.85 * ds) f += d / pc.a.x; } }\n"
        "        imageStore(ffin, x, vec4(f, fl, 0.0)); return;\n"
        "    }\n"
        "    ivec2 q = ivec2(floor(vec2(x) + 0.5 - pc.a.x * m));\n"
        "    if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, ext))) return;\n"
        "    uint dq = uint(clamp(log2(max(imageLoad(dist, x).r, 1.0)) * 192.0, 0.0, 4095.0));\n"
        "    ivec2 v = ivec2(round(sign(m) * sqrt(abs(m)) * 22.58)) + 512;\n"      /* 0.3.17: the square root of the vector, +-512 pixels (0.1 pixel steps near 0, 2 near 512) */
        "    if (any(lessThan(v, ivec2(0))) || any(greaterThan(v, ivec2(1022)))) v = ivec2(1023);\n"
        "    imageAtomicMin(fkey, q, (dq << 20) | (uint(v.x) << 10) | uint(v.y));\n"
        "}\n";
    uint64_t h;
    VkShaderModule mod = module_from_glsl(5, glsl, "frame generation forward pass", &h, NULL);
    if (!mod) return 0;
    VkDescriptorSetLayoutBinding b[6];
    for (int i = 0; i < 6; i++) { b[i].binding = (uint32_t)i; b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; b[i].pImmutableSamplers = NULL; }
    VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 6; li.pBindings = b;
    VKC(vkCreateDescriptorSetLayout(V.device, &li, NULL, &g_fs_dl));
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &g_fs_dl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr;
    VKC(vkCreatePipelineLayout(V.device, &pl, NULL, &g_fs_pl));
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = mod; ci.stage.pName = "main"; ci.layout = g_fs_pl;
    VkResult r = vkCreateComputePipelines(V.device, g_pcache, 1, &ci, NULL, &g_fs_pipe);
    vkDestroyShaderModule(V.device, mod, NULL);
    if (r != VK_SUCCESS) return 0;
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 6 * NSLOT};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = NSLOT; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    VKC(vkCreateDescriptorPool(V.device, &pi, NULL, &g_fs_pool));
    for (int i = 0; i < NSLOT; i++) {
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = g_fs_pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_fs_dl;
        VKC(vkAllocateDescriptorSets(V.device, &ai, &g_fs_set[i]));
    }
    g_fs_ok = 1;
    printf("nvn_vk_fg: forward pass ready: moving parts are pushed to where they land in the made frame (SMO_VK_FG_SCATTER=0: off)\n");
    return 1;
}
static int fs_run(float t) {                       /* in fg_make, after the barrier before the warp; 1: the warp may use it */
    if (gfx_framegen_fast()) return 0;             /* 0.3.22: quality fast, the camera's look-up only (no forward pass, no copy of the last real frame) */
    if (!g_fs_ok || !g_mv_aview[0] || !g_mv_aview[1] || !g_mv_aview[2] || !g_mv_aview[3] || !g_mv_aview[4]) return 0;
    VkDescriptorSet set = g_fs_set[R.cur];
    VkDescriptorImageInfo ii[6] = {{VK_NULL_HANDLE, g_mv_view, VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, g_mv_aview[0], VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, g_mv_aview[1], VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, g_mv_aview[2], VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, g_mv_aview[3], VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, g_mv_aview[4], VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[6];
    for (int i = 0; i < 6; i++) { VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, (uint32_t)i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ii[i], NULL, NULL}; wr[i] = w; }
    vkUpdateDescriptorSets(V.device, 6, wr, 0, NULL);
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;      /* the last made frame's reads of the key */
    vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    VkClearColorValue cv; memset(&cv, 0xff, sizeof cv);
    VkImageSubresourceRange sr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(R.cmd, g_mv_aimg[1], VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &sr);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    vkCmdBindPipeline(R.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_fs_pipe);
    vkCmdBindDescriptorSets(R.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_fs_pl, 0, 1, &set, 0, NULL);
    for (int pass = 0; pass < 2; pass++) {
        float pc[4] = {t, (float)pass, g_fs_pdist_ok ? 1.0f : 0.0f, 0};
        vkCmdPushConstants(R.cmd, g_fs_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
        vkCmdDispatch(R.cmd, (uint32_t)(g_mv_w + 63) / 64, (uint32_t)g_mv_h, 1);
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    }
    {   /* 0.3.18: this frame's distance for the next one (after the fill pass read the last one) */
        mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        VkImageCopy c; memset(&c, 0, sizeof c);
        c.srcSubresource.aspectMask = c.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.srcSubresource.layerCount = c.dstSubresource.layerCount = 1;
        c.extent.width = (uint32_t)g_mv_w; c.extent.height = (uint32_t)g_mv_h; c.extent.depth = 1;
        vkCmdCopyImage(R.cmd, g_mv_aimg[0], VK_IMAGE_LAYOUT_GENERAL, g_mv_aimg[4], VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        g_fs_pdist_ok = 1;
    }
    return 1;
}
static int fg_init(void) {
    if (g_fg_state) return g_fg_state > 0;
    g_fg_state = -1;
    static const char glsl[] =
        "#version 450\n"
        "layout(local_size_x = 64) in;\n"
        "layout(binding = 0) uniform sampler2D src;\n"            /* the real frame (bytes as UNORM: the warp moves them as they are) */
        "layout(binding = 1) uniform sampler2D motion;\n"         /* RG16F, pixels of the scene */
        "layout(binding = 2, rgba8) uniform writeonly image2D dst;\n"
        "layout(binding = 3) uniform sampler2D pre;\n"            /* step 3: the HUD target before the HUD was drawn (the scene, linear) */
        "layout(binding = 4) uniform sampler2D post;\n"           /* step 3: the HUD target after it (scene + HUD, linear) */
        "layout(binding = 6) uniform sampler2D prv;\n"           /* 0.3.18: the last real frame (as src, or as pre when the HUD is apart); a.w 1 = it is there */
        "layout(binding = 5) uniform sampler2D ffin;\n"          /* 0.3.14: the forward pass's vectors (fs_init): where the pixel that lands here came from */
        "layout(push_constant) uniform P { vec4 a; vec4 b; } pc;\n"      /* a.x: steps ahead (frames); a.y: 1 = the HUD apart (pre / post); b: the picture in dst (x, y, w, h pixels) */
        "vec3 enc(vec3 x) { return mix(12.92 * x, 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), x)); }\n"
        "void main() {\n"
        "    ivec2 ext = imageSize(dst), q = ivec2(gl_GlobalInvocationID.xy) + ivec2(pc.b.xy);\n"      /* 0.3.14: dispatched over the picture only (the present's crop: handheld, the top-left 1280x720 of the 1600x900 window texture) */
        "    vec2 cs = pc.b.zw, c0 = pc.b.xy;\n"
        "    if (any(greaterThanEqual(q, ext)) || any(greaterThanEqual(vec2(q) - c0, cs))) return;\n"
        "    vec2 uq = (vec2(q) - c0 + 0.5) / cs, msz = vec2(textureSize(motion, 0));\n"      /* 0..1 over the picture: the scene, the vectors, the HUD textures */
        "    vec2 p; bool fromprev = false;\n"
        "    if (pc.a.z > 0.5) {\n"      /* 0.3.14: the pixel that lands here (pushed forward, the nearest wins); a gap takes its farthest neighbour's, else one step back */
        "        ivec2 mq = clamp(ivec2(uq * msz), ivec2(0), ivec2(msz) - 1);\n"
        "        vec3 fv = texelFetch(ffin, mq, 0).xyz;\n"
        "        if (fv.z < 0.5) p = clamp(uq + pc.a.x * fv.xy / msz, vec2(0.0), vec2(1.0));\n"
        "        else if (pc.a.w > 0.5) { fromprev = true; p = clamp(uq + (pc.a.x + 1.0) * fv.xy / msz, vec2(0.0), vec2(1.0)); }\n"      /* hidden now behind something nearer: one real frame further back along its vector, in the last real frame */
        "        else p = uq;\n"
        "    } else {\n"      /* SMO_VK_FG_SCATTER=0: 0.3.13's look-up at the pixel itself (moving parts stayed where they were) */
        "        p = uq + pc.a.x * texture(motion, uq).xy / msz;\n"
        "        p = clamp(uq + pc.a.x * texture(motion, p).xy / msz, vec2(0.0), vec2(1.0));\n"
        "    }\n"
        "    vec2 ps = clamp(c0 + p * cs, c0 + 0.5, c0 + cs - 0.5) / vec2(ext);\n"      /* in the window texture, kept off the crop edge */
        "    if (pc.a.y < 0.5) { if (fromprev) imageStore(dst, q, texture(prv, ps)); else imageStore(dst, q, texture(src, ps)); return; }\n"
        "    vec3 a = textureLod(pre, uq, 0.0).rgb, b = textureLod(post, uq, 0.0).rgb;\n"
        "    if (any(greaterThan(abs(a - b), vec3(0.004)))) { imageStore(dst, q, texelFetch(src, q, 0)); return; }\n"      /* the HUD drew here: the real pixel, where it is */
        "    if (fromprev) imageStore(dst, q, vec4(enc(textureLod(prv, p, 0.0).rgb), 1.0)); else imageStore(dst, q, vec4(enc(textureLod(pre, p, 0.0).rgb), 1.0));\n"     /* the scene, moved, encoded as the game's last copy does (sRGB) */
        "}\n";
    uint64_t h;
    VkShaderModule mod = module_from_glsl(5, glsl, "frame generation warp", &h, NULL);
    if (!mod) { printf("nvn_vk_fg: the warp shader could not be made: no frame generation\n"); return 0; }
    VkDescriptorSetLayoutBinding b[7] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
                                         {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 7; li.pBindings = b;
    VKC(vkCreateDescriptorSetLayout(V.device, &li, NULL, &g_fg_dl));
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &g_fg_dl; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr;
    VKC(vkCreatePipelineLayout(V.device, &pl, NULL, &g_fg_pl));
    VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = mod; ci.stage.pName = "main"; ci.layout = g_fg_pl;
    VkResult r = vkCreateComputePipelines(V.device, g_pcache, 1, &ci, NULL, &g_fg_pipe);
    vkDestroyShaderModule(V.device, mod, NULL);
    if (r != VK_SUCCESS) { printf("nvn_vk_fg: the warp pipeline was refused (%s): no frame generation\n", vk_result_name(r)); return 0; }
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 * NSLOT}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, NSLOT}};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = NSLOT; pi.poolSizeCount = 2; pi.pPoolSizes = ps;
    VKC(vkCreateDescriptorPool(V.device, &pi, NULL, &g_fg_pool));
    for (int i = 0; i < NSLOT; i++) {
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = g_fg_pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &g_fg_dl;
        VKC(vkAllocateDescriptorSets(V.device, &ai, &g_fg_set[i]));
    }
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; si.maxLod = 0;
    VKC(vkCreateSampler(V.device, &si, NULL, &g_fg_lin));
    if (!fs_init()) printf("nvn_vk_fg: the forward pass could not be made: moving parts keep 0.3.13's look-up\n");
    g_fg_state = 1;
    printf("nvn_vk_fg: warp ready (compute 64x1)\n");
    return 1;
}
static void fg_drop_image(void) {
    if (!g_fg_img) return;
    cmd_wait_all();
    for (VView *n = g_fgvt.views, *nx; n; n = nx) { nx = n->next; vkDestroyImageView(V.device, n->view, NULL); free(n); }
    g_fgvt.views = NULL;
    vkDestroyImageView(V.device, g_fg_store, NULL); vkDestroyImage(V.device, g_fg_img, NULL); vmem_free(g_fg_mem);
    g_fg_img = VK_NULL_HANDLE;
}
static int fg_image(VTex *vt) {                    /* the made frame's image: the window texture's size and format */
    if (g_fg_img && g_fgvt.t == vt->t && g_fgvt.w == vt->w && g_fgvt.h == vt->h && g_fgvt.fmt == vt->fmt) return 1;
    fg_drop_image();
    VkFormat raw = fg_raw(vt->fmt);
    if (!raw || vt->samples != 1 || vt->is3d || vt->aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
        static int said; if (!said++) printf("nvn_vk_fg: the window texture (Vulkan format %d, %d samples) is not handled: no frames made\n", (int)vt->fmt, vt->samples);
        return 0;
    }
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements rq;
    ci.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = vt->fmt; ci.extent.width = (uint32_t)vt->w; ci.extent.height = (uint32_t)vt->h; ci.extent.depth = 1;
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(V.device, &ci, NULL, &g_fg_img) != VK_SUCCESS) { g_fg_img = VK_NULL_HANDLE; return 0; }
    vkGetImageMemoryRequirements(V.device, g_fg_img, &rq);
    g_fg_mem = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VKC(vkBindImageMemory(V.device, g_fg_img, g_fg_mem.mem, g_fg_mem.off));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g_fg_img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = raw;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    VKC(vkCreateImageView(V.device, &vi, NULL, &g_fg_store));
    g_fgvt = *vt;                                  /* its description (format, size, the texture it stands for), then its own parts */
    g_fgvt.img = g_fg_img; g_fgvt.mem = g_fg_mem; g_fgvt.views = NULL; g_fgvt.next = g_fgvt.prev = NULL;
    g_fgvt.levels = 1; g_fgvt.layout = VK_IMAGE_LAYOUT_UNDEFINED; g_fgvt.vread = 0;
    return 1;
}
/* ---- step 3: the HUD apart. SMO finishes the scene into a texture of the window texture's size (RGB10A2, linear), draws the HUD into
 * that same texture in one pass (the layout shaders, blended), and one last draw copies it into the window texture with the sRGB
 * curve (measured on the S24: 21 -> 81, 132 -> 190). So that texture, copied when the HUD pass begins, is the scene without the HUD.
 * The candidates are the passes into a texture of the window texture's size that is not the window texture; the frame's last one
 * is the HUD pass, and from then on only that texture is copied (g_hud_pref). The warp takes a pixel as HUD where the texture
 * changed during the HUD pass (pre vs post) and keeps the real pixel there; elsewhere it moves the snapshot and encodes it. */
static Tex *g_fg_win_tex; static int g_fg_win_w, g_fg_win_h;      /* the last real frame's window texture */
static Tex *g_fg_wins[8]; static int g_fg_nwin;    /* all the window's textures (it has 3 and takes them in turn: the final copy into the next one is no HUD pass) */
/* 0.3.22, FRAME GENERATION AUTO (frame_generation = auto): on while the game cannot hold 60 but is fast enough that a real frame
 * and a made one fit (5 one-second windows in a row with real frames 18.5-27 ms on average: ~37-54 fps; frames over 100 ms, a load
 * or a pause, left out), off again when it cannot keep up (4 windows in a row with under 85% of the real frames within 40 ms of the
 * last one). After that it waits before it tries again: 30 s, doubled (up to 8 minutes) when it went off again within a minute of
 * coming on, so a phone at the edge does not switch back and forth. Off by itself under ~37 fps: frame generation's own work
 * would push the real frames under 30 there. With GPU times (SMO_VK_GPUTIME, on by default) it also needs room on the GPU: the
 * GPU time of a real frame plus frame generation's own (measured while it was on, 8 ms before the first time: the S24's) under
 * 30 ms (S24 at resolution 1.70: 52 fps but frame generation's work at that size did not fit; it came on and off again).
 * Each switch goes into log.txt (nvn_vk_fg: auto). */
static void fg_auto_tick(void) {
    if (g_fg_env >= 0 || gfx_options()->framegen != 2) return;
    static int64_t last, win_start; static double win_sum, hold = 30.0, hold_until, on_at, busy0 = -1; static int win_n, win_late, slow_run, bad_run, on;
    int64_t now = tk(); double f = (double)plat_qpf();
    double ms = last ? (double)(now - last) * 1000.0 / f : 0;
    last = now;
    if (ms <= 0 || ms > 100.0) return;
    if (!win_n) win_start = now;
    win_sum += ms; win_n++; if (ms > 40.0) win_late++;
    if ((double)(now - win_start) < f) return;
    double avg = win_sum / win_n, ontime = 1.0 - (double)win_late / win_n, t = (double)now / f;
    double gpu = g_ts_on > 0 && busy0 >= 0 ? (g_ts_busy_ns - busy0) / 1e6 / win_n : -1;      /* GPU time per real frame in this window */
    busy0 = g_ts_on > 0 ? g_ts_busy_ns : -1;
    int nw = win_n;
    win_sum = 0; win_n = win_late = 0;
    if (!on) {
        /* before the first measurement: the S24's 8 ms at a 1408x792 scene, its present pass (2.8 ms, screen sized) and the rest by the scene's pixels */
        double fgc = g_fg_pair_ms > 0 ? g_fg_pair_ms : 2.8 + 5.2 * (g_fg_scene_px > 0 ? (double)g_fg_scene_px / (1408.0 * 792.0) : 1.0);
        int room = gpu < 0 || gpu + fgc <= 30.0;
        slow_run = avg > 18.5 && avg <= 27.0 && room && nw > 5 ? slow_run + 1 : 0;
        if (slow_run >= 5 && t >= hold_until) {
            on = 1; on_at = t; slow_run = bad_run = 0;
            gfx_framegen_auto_set(1);
            printf("nvn_vk_fg: auto: on (%.1f fps for 5 s, between what holds 60 and what a real and a made frame need; GPU %.1f ms a frame + %.1f for frame generation)\n", 1000.0 / avg, gpu, fgc);
        }
    } else {
        bad_run = ontime < 0.5 ? bad_run + 2 : ontime < 0.85 ? bad_run + 1 : 0;      /* under half on time: off after 2 s */
        if (bad_run >= 4) {
            on = 0; bad_run = slow_run = 0;
            hold = t - on_at < 60.0 ? (hold * 2 > 480.0 ? 480.0 : hold * 2) : 30.0;
            hold_until = t + hold;
            gfx_framegen_auto_set(0);
            printf("nvn_vk_fg: auto: off (real frames %.0f%% on time for 4 s); not tried again for %.0f s\n", 100.0 * ontime, hold);
        }
    }
}
static void fg_note_window(const Window *w) {      /* vk_present2, each real present */
    fg_auto_tick();
    g_fg_nwin = 0;
    if (w) for (int i = 0; i < w->count && i < 8; i++) if (w->tex[i]) g_fg_wins[g_fg_nwin++] = w->tex[i];
}
/* 0.3.14: the HUD target has the window's shape, not always its size. Docked: 1600x900 into the 1600x900 window; handheld (measured on
 * the S24, SMO_VK_PASSTIME): the HUD is drawn into a 1280x720 texture that the last draw scales into the 1600x900 window, so the
 * 0.3.13 test (the same size) found no HUD pass and the HUD was moved with the picture (the Mali tester: 'HUD copy 0.000 ms').
 * The warp reads the HUD textures at 0..1 coordinates (filtered), so any size of the same shape, at least half the window, works. */
static int fg_hud_shape(int w, int h, int ww, int wh) {
    if (w <= 0 || h <= 0 || ww <= 0 || wh <= 0 || 2 * w < ww || 2 * h < wh) return 0;
    long long d = (long long)w * wh - (long long)h * ww; if (d < 0) d = -d;
    return d * 100 <= (long long)w * wh;                  /* within 1% */
}
static Tex *g_hud_pref, *g_hud_tex; static int g_hud_pending;      /* a snapshot was taken since the last real present (its HUD pass comes before its present on this thread) */
static VkImage g_hud_img; static VMem g_hud_mem; static VkImageView g_hud_view; static int g_hud_w, g_hud_h; static VkFormat g_hud_fmt;
static VkImageView g_hud_postview; static VkImage g_hud_postview_img;
static int64_t g_hud_snaps;
static Tex *g_hud_arm;                             /* the HUD target of the pass just begun: copy it after the first draw */
static void fg_hud_note_pass(void) {               /* pass_begin, before the pass begins (render thread, no pass open) */
    static Tex *last_ct;                           /* the colour target of the pass before: the same one again is the same pass split by a submission */
    Tex *ct = R.ncol >= 1 ? R.col[0].tex : NULL, *prev = last_ct;
    last_ct = ct;
    if (g_fg_on <= 0 || !g_fg_win_tex || !ct || ct == prev) return;
    VTex *cv = !ct->dead ? ct->vk : NULL;
    for (int i = 0; i < g_fg_nwin; i++) if (ct == g_fg_wins[i]) return;      /* any of the window's textures (the game takes them in turn) */
    if (!cv || ct == g_fg_win_tex || !fg_hud_shape(cv->w, cv->h, g_fg_win_w, g_fg_win_h) || cv->aspect != VK_IMAGE_ASPECT_COLOR_BIT || cv->samples != 1 || cv->is3d) return;
    if (g_hud_pref && ct != g_hud_pref && !g_hud_pref->dead) return;      /* known: only the HUD target is copied */
    if (g_hud_img && (g_hud_w != cv->w || g_hud_h != cv->h || g_hud_fmt != cv->fmt)) { cmd_wait_all(); vkDestroyImageView(V.device, g_hud_view, NULL); vkDestroyImage(V.device, g_hud_img, NULL); vmem_free(g_hud_mem); g_hud_img = VK_NULL_HANDLE; }
    if (!g_hud_img) {
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkMemoryRequirements rq;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = cv->fmt; ci.extent.width = (uint32_t)cv->w; ci.extent.height = (uint32_t)cv->h; ci.extent.depth = 1;
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(V.device, &ci, NULL, &g_hud_img) != VK_SUCCESS) { g_hud_img = VK_NULL_HANDLE; return; }
        vkGetImageMemoryRequirements(V.device, g_hud_img, &rq);
        g_hud_mem = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        VKC(vkBindImageMemory(V.device, g_hud_img, g_hud_mem.mem, g_hud_mem.off));
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = g_hud_img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = cv->fmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        VKC(vkCreateImageView(V.device, &vi, NULL, &g_hud_view));
        g_hud_w = cv->w; g_hud_h = cv->h; g_hud_fmt = cv->fmt;
    }
    g_hud_arm = ct;                                /* copied after this pass's FIRST draw (fg_hud_after_draw) */
}
/* The pass's first draw puts this frame's scene into the HUD target (SMO keeps the texture from frame to frame: when the pass begins it
 * still holds the last frame's picture, HUD and all; measured on the S24, the snapshot taken at the pass's beginning was the frame
 * before). So after that draw the pass is ended, the texture copied (the scene without the HUD), and the next draw begins the pass
 * again (its target loaded): one break in the HUD pass a frame. */
static void fg_hud_after_draw(void) {              /* each draw, after its command (render thread, inside the pass) */
    Tex *ct = g_hud_arm;
    if (!ct) return;
    g_hud_arm = NULL;
    VTex *cv = R.ncol >= 1 && R.col[0].tex == ct && !ct->dead ? ct->vk : NULL;
    if (!cv || !g_hud_img) return;
    pass_end();                                    /* the first draw's result stored; the next draw begins the pass again */
    fgt_mark(g_fgt_real + 1, 2);                   /* (the frame being built: its key is given at its present) */
    vt_layout(cv, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    pass_end(); cmd_begin();
    image_barrier(g_hud_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy c; memset(&c, 0, sizeof c);
    c.srcSubresource.aspectMask = c.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.srcSubresource.layerCount = c.dstSubresource.layerCount = 1;
    c.extent.width = (uint32_t)cv->w; c.extent.height = (uint32_t)cv->h; c.extent.depth = 1;
    vkCmdCopyImage(R.cmd, cv->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_hud_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    image_barrier(g_hud_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    fgt_mark(g_fgt_real + 1, 3);
    g_hud_tex = ct; g_hud_pending = 1; g_hud_snaps++;
}
/* vk_present2 (a real frame, after its present): make the frame that follows it. 1: g_fgvt holds it, present it (vk_present2 mode 3) */
/* 0.3.18 (third step): THE LAST REAL FRAME. A strip that something in front hides in this real frame and that comes into view in the made
 * one (the background beside Mario while the camera circles him) has no picture in this frame; filled with the picture as it is there
 * (the second step) it showed as a seam in Mario's shape against a textured background (the user's third recording: the waterfall
 * cliffs). The last real frame showed it, one real frame further back along the background's vector. So each real frame's scene (the
 * window texture, or the HUD target's snapshot when the HUD is apart) is copied after the warp, and the next made frame fetches such
 * strips from it. */
static VkImage g_fgprev_img; static VMem g_fgprev_mem; static VkImageView g_fgprev_view; static int g_fgprev_w, g_fgprev_h, g_fgprev_kind, g_fgprev_ok; static VkFormat g_fgprev_fmt;
static int fgprev_image(int w, int h, VkFormat fmt, VkFormat vfmt, int kind) {
    if (g_fgprev_img && g_fgprev_w == w && g_fgprev_h == h && g_fgprev_fmt == fmt && g_fgprev_kind == kind) return 1;
    if (g_fgprev_img) { cmd_wait_all(); vkDestroyImageView(V.device, g_fgprev_view, NULL); vkDestroyImage(V.device, g_fgprev_img, NULL); vmem_free(g_fgprev_mem); g_fgprev_img = VK_NULL_HANDLE; }
    g_fgprev_ok = 0;
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements rq;
    ci.flags = vfmt != fmt ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent.width = (uint32_t)w; ci.extent.height = (uint32_t)h; ci.extent.depth = 1;
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(V.device, &ci, NULL, &g_fgprev_img) != VK_SUCCESS) { g_fgprev_img = VK_NULL_HANDLE; return 0; }
    vkGetImageMemoryRequirements(V.device, g_fgprev_img, &rq);
    g_fgprev_mem = vmem_alloc(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VKC(vkBindImageMemory(V.device, g_fgprev_img, g_fgprev_mem.mem, g_fgprev_mem.off));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = g_fgprev_img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = vfmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    VKC(vkCreateImageView(V.device, &vi, NULL, &g_fgprev_view));
    pass_end(); cmd_begin();
    image_barrier(g_fgprev_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g_fgprev_w = w; g_fgprev_h = h; g_fgprev_fmt = fmt; g_fgprev_kind = kind;
    return 1;
}
static int fg_make(VTex *vt, long long frame, const int *crop) {      /* crop: the picture in vt (x, y, w, h), as the present shows it */
    int hud_pending = g_hud_pending; g_hud_pending = 0;      /* this frame's snapshot, used or dropped here */
    {   static int lastq = -1;                         /* 0.4: the quality in the log when it is set or changed */
        int q = gfx_framegen_fast();
        if (fg_on() && q != lastq) { lastq = q; printf("nvn_vk_fg: quality %s\n", q ? "fast: made frames follow the camera only (no moving objects, no forward pass)" : "high: moving objects drawn again, forward pass"); }
    }
    int prev_ok = g_fgprev_ok; g_fgprev_ok = 0;    /* 0.3.18: the last real frame's copy, if that frame was made too */
    if (fg_on()) {
        int64_t now = tk(); double ms = g_fgp_last ? (double)(now - g_fgp_last) * 1000.0 / (double)plat_qpf() : 0;
        if (g_fgp_last && ms < 1000.0) { g_fgp_n[ms <= 40.0 ? 0 : ms <= 56.0 ? 1 : 2]++; g_fg_late_avg = 0.98 * g_fg_late_avg + 0.02 * (ms > 40.0); }
        g_fgp_last = now;
    } else g_fgp_last = 0;
    if (!fg_on() || !vt || g_mv_frame_ok != frame || !g_mv_img || !fg_init() || !fg_image(vt)) { if (fg_on()) g_fg_skipped++; return 0; }
    VkFormat raw = fg_raw(vt->fmt);
    {   /* the real frame's bytes as UNORM: a view kept in the window texture's own list (vtype -7: no lookup of vt_view matches it), so it
         * goes with the texture (vt_destroy). 0.3.13: the window takes its 3 textures in turn, and the view made again for each had waited
         * for the whole GPU every frame; a first cache by texture and image handle went stale when the game made its window textures
         * again (handles reused): made frames from an old picture, seen as shaking. */
        VView *n;
        for (n = vt->views; n && n->vtype != -7; n = n->next) {}
        if (!n) {
            n = calloc(1, sizeof *n);
            VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = vt->img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = raw;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
            if (!n || vkCreateImageView(V.device, &vi, NULL, &n->view) != VK_SUCCESS) { free(n); g_fg_srcview = VK_NULL_HANDLE; g_fg_srcview_img = VK_NULL_HANDLE; return 0; }
            n->vtype = -7; n->next = vt->views; vt->views = n; R.views++;
        }
        g_fg_srcview = n->view; g_fg_srcview_img = vt->img;
    }
    int cx = crop[0], cy = crop[1], cw = crop[2], chh = crop[3];
    if (cx < 0 || cy < 0 || cw <= 0 || chh <= 0 || cx + cw > vt->w || cy + chh > vt->h) { cx = cy = 0; cw = vt->w; chh = vt->h; }
    { static int lw, lh; if (cw != lw || chh != lh) { lw = cw; lh = chh; printf("nvn_vk_fg: frame %lld: the picture is %dx%d at %d,%d of the %dx%d window texture\n", frame, cw, chh, cx, cy, vt->w, vt->h); } }
    g_fg_win_tex = vt->t; g_fg_win_w = cw; g_fg_win_h = chh;      /* 0.3.14: the HUD target has the picture's shape */      /* step 3: which passes may be the HUD pass next frame (fg_hud_note_pass) */
    /* step 3: this frame's HUD apart? the snapshot taken at its HUD pass, and that texture now (the HUD drawn in it) */
    Tex *ht = hud_pending ? g_hud_tex : NULL;
    VTex *hv = ht && !ht->dead ? ht->vk : NULL;
    int hud = hv && g_hud_img && hv->w == g_hud_w && hv->h == g_hud_h && hv->fmt == g_hud_fmt && fg_hud_shape(hv->w, hv->h, cw, chh);
    if (hud) {
        g_hud_pref = ht;                           /* next frame copies only this texture */
        {   VView *n;                              /* 0.3.14: its view kept in the texture's own list (vtype -8), as the window's (-7) */
            for (n = hv->views; n && n->vtype != -8; n = n->next) {}
            if (!n && (n = calloc(1, sizeof *n)) != NULL) {
                VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                vi.image = hv->img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = hv->fmt;
                vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
                if (vkCreateImageView(V.device, &vi, NULL, &n->view) != VK_SUCCESS) { free(n); n = NULL; }
                else { n->vtype = -8; n->next = hv->views; hv->views = n; R.views++; }
            }
            if (n) { g_hud_postview = n->view; g_hud_postview_img = hv->img; } else { g_hud_postview = VK_NULL_HANDLE; g_hud_postview_img = VK_NULL_HANDLE; hud = 0; }
        }
        if (hud) vt_layout(hv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    pass_end();
    fgt_mark(g_fgt_real, 4);                       /* 0.3.12: the made frame's GPU time begins (its layouts, the warp, its present pass) */
    vt_layout(vt, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vt_layout(&g_fgvt, VK_IMAGE_LAYOUT_GENERAL);
    pass_end(); cmd_begin();
    VkDescriptorSet set = g_fg_set[R.cur];
    VkDescriptorImageInfo si = {g_fg_lin, g_fg_srcview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, mi = {g_fg_lin, g_mv_view, VK_IMAGE_LAYOUT_GENERAL},
                          di = {VK_NULL_HANDLE, g_fg_store, VK_IMAGE_LAYOUT_GENERAL},
                          pi_ = {g_fg_lin, hud ? g_hud_view : g_fg_srcview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                          po_ = {g_fg_lin, hud ? g_hud_postview : g_fg_srcview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                          fi = {g_fg_lin, g_mv_aview[3] ? g_mv_aview[3] : g_mv_view, VK_IMAGE_LAYOUT_GENERAL};
    int pvalid = prev_ok && g_fgprev_img && g_fgprev_kind == (hud ? 2 : 1) && (hud ? g_fgprev_w == g_hud_w && g_fgprev_h == g_hud_h && g_fgprev_fmt == g_hud_fmt : g_fgprev_w == vt->w && g_fgprev_h == vt->h && g_fgprev_fmt == vt->fmt);
    VkDescriptorImageInfo pv = {g_fg_lin, pvalid ? g_fgprev_view : hud ? g_hud_view : g_fg_srcview, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr[7] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &si, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &mi, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &di, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pi_, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 4, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &po_, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 5, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &fi, NULL, NULL},
                                  {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 6, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pv, NULL, NULL}};
    vkUpdateDescriptorSets(V.device, 7, wr, 0, NULL);
    { static int said = -1; if (said != hud) { said = hud; printf("nvn_vk_fg: frame %lld: the HUD %s\n", frame, hud ? "is kept apart (snapshot before its pass): drawn where it is, the scene moved under it" : "is moved with the picture (no HUD pass found)"); } }
    {   VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};      /* the vectors (compute) and the real frame (its passes) before the warp */
        mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(R.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        g_cs_debt = 0;
    }
    float t = g_fg_t > 0 ? g_fg_t : 1.0f / (float)g_mv_steps;
    g_fg_tsteps[g_mv_steps < 3 ? g_mv_steps : 3]++;
    if ((g_fg_tsteps[1] + g_fg_tsteps[2] + g_fg_tsteps[3]) % 1800 == 0)
        printf("nvn_vk_fg: made frames by their real frame's logic steps: 1: %lld, 2: %lld, 3: %lld (each one step ahead)\n", (long long)g_fg_tsteps[1], (long long)g_fg_tsteps[2], (long long)g_fg_tsteps[3]);
    int fwd = fs_run(t);                          /* 0.3.14: moving parts pushed to where they land (fs_init) */
    float pc[8] = {t, hud ? 1.0f : 0.0f, fwd ? 1.0f : 0.0f, pvalid ? 1.0f : 0.0f, (float)cx, (float)cy, (float)cw, (float)chh};
    vkCmdBindPipeline(R.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_fg_pipe);
    vkCmdBindDescriptorSets(R.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_fg_pl, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(R.cmd, g_fg_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 32, pc);
    vkCmdDispatch(R.cmd, (uint32_t)(cw + 63) / 64, (uint32_t)chh, 1);
    fgt_mark(g_fgt_real, 6);                       /* 0.3.12: the warp ends, the made frame's present pass follows */
    if (fwd) {                                     /* 0.3.18: this real frame's scene for the next made frame (fgprev_image) */
        int pw = hud ? g_hud_w : vt->w, ph = hud ? g_hud_h : vt->h;
        VkFormat pf = hud ? g_hud_fmt : vt->fmt;
        if (fgprev_image(pw, ph, pf, hud ? pf : raw, hud ? 2 : 1)) {
            if (!hud) vt_layout(vt, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            pass_end(); cmd_begin();
            if (hud) image_barrier(g_hud_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            image_barrier(g_fgprev_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy c; memset(&c, 0, sizeof c);
            c.srcSubresource.aspectMask = c.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.srcSubresource.layerCount = c.dstSubresource.layerCount = 1;
            c.extent.width = (uint32_t)pw; c.extent.height = (uint32_t)ph; c.extent.depth = 1;
            vkCmdCopyImage(R.cmd, hud ? g_hud_img : vt->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_fgprev_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            image_barrier(g_fgprev_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (hud) image_barrier(g_hud_img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            g_fgprev_ok = 1;
        }
    }
    if (mv_shot_frame(frame) && hud) {             /* step 3 check: the snapshot (pre) and the HUD target now (post), RGB10A2 decoded to 8 bits */
        for (int k = 0; k < 2; k++) {
            VkImage img = k ? hv->img : g_hud_img; VkImageLayout lay = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            uint64_t size = (uint64_t)g_hud_w * g_hud_h * 4;
            VMem bm; VkBuffer buf = vbuf_new(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bm);
            if (!buf) continue;
            if (k) vt_layout(hv, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL); else { pass_end(); cmd_begin(); image_barrier(img, VK_IMAGE_ASPECT_COLOR_BIT, lay, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL); }
            VkBufferImageCopy c; memset(&c, 0, sizeof c);
            c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1;
            c.imageExtent.width = (uint32_t)g_hud_w; c.imageExtent.height = (uint32_t)g_hud_h; c.imageExtent.depth = 1;
            vkCmdCopyImageToBuffer(R.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &c);
            if (k) vt_layout(hv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); else image_barrier(img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, lay);
            cmd_wait_all();
            int w = g_hud_w, h = g_hud_h, row = (w * 3 + 3) & ~3;
            uint8_t *bmp = calloc((size_t)row * h + 54, 1);
            if (bmp) {
                const uint32_t *px = bm.map;
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                    uint32_t v = px[(size_t)y * w + x]; uint8_t *o = bmp + 54 + (size_t)(h - 1 - y) * row + (size_t)x * 3;
                    o[2] = (uint8_t)((v & 1023) >> 2); o[1] = (uint8_t)(((v >> 10) & 1023) >> 2); o[0] = (uint8_t)(((v >> 20) & 1023) >> 2);
                }
                uint32_t fsz = (uint32_t)(54 + (size_t)row * h), off = 54, hs = 40; int32_t ww = w, hh = h; uint16_t planes = 1, bpp = 24;
                bmp[0] = 'B'; bmp[1] = 'M'; memcpy(bmp + 2, &fsz, 4); memcpy(bmp + 10, &off, 4); memcpy(bmp + 14, &hs, 4); memcpy(bmp + 18, &ww, 4); memcpy(bmp + 22, &hh, 4); memcpy(bmp + 26, &planes, 2); memcpy(bmp + 28, &bpp, 2);
                char path[320]; const char *dir = getenv("SMO_SHOT_DIR");
                snprintf(path, sizeof path, "%s%sfg_%05lld_%s.bmp", dir ? dir : "", dir ? "/" : "", frame, k ? "post" : "pre");
                FILE *f = fopen(path, "wb"); if (f) { fwrite(bmp, 1, fsz, f); fclose(f); }
                free(bmp);
            }
            vkDestroyBuffer(V.device, buf, NULL); vmem_free(bm);
        }
        printf("nvn_vk_fg: frame %lld: HUD target %dx%d (format %d), snapshots this run %lld\n", frame, g_hud_w, g_hud_h, (int)g_hud_fmt, (long long)g_hud_snaps);
    }
    if (mv_shot_frame(frame)) {                    /* SMO_VK_MOTION_SHOT frames: the real frame and the made one as pictures */
        char path[320]; const char *dir = getenv("SMO_SHOT_DIR");
        snprintf(path, sizeof path, "%s%sfg_%05lld_real.bmp", dir ? dir : "", dir ? "/" : "", frame);
        save_bmp_vk(path, vt);
        uint64_t size = (uint64_t)vt->w * vt->h * 4;
        VMem bm; VkBuffer buf = vbuf_new(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bm);
        if (buf) {
            vt_layout(&g_fgvt, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy c; memset(&c, 0, sizeof c);
            c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1;
            c.imageExtent.width = (uint32_t)vt->w; c.imageExtent.height = (uint32_t)vt->h; c.imageExtent.depth = 1;
            vkCmdCopyImageToBuffer(R.cmd, g_fg_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &c);
            cmd_wait_all();
            int w = vt->w, h = vt->h, row = (w * 3 + 3) & ~3, bgr = fg_raw(vt->fmt) == VK_FORMAT_B8G8R8A8_UNORM;
            uint8_t *bmp = calloc((size_t)row * h + 54, 1);
            if (bmp) {
                const uint8_t *px = bm.map;
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                    const uint8_t *s = px + ((size_t)y * w + x) * 4; uint8_t *o = bmp + 54 + (size_t)(h - 1 - y) * row + (size_t)x * 3;
                    o[0] = s[bgr ? 0 : 2]; o[1] = s[1]; o[2] = s[bgr ? 2 : 0];
                }
                uint32_t fsz = (uint32_t)(54 + (size_t)row * h), off = 54, hs = 40; int32_t ww = w, hh = h; uint16_t planes = 1, bpp = 24;
                bmp[0] = 'B'; bmp[1] = 'M'; memcpy(bmp + 2, &fsz, 4); memcpy(bmp + 10, &off, 4); memcpy(bmp + 14, &hs, 4); memcpy(bmp + 18, &ww, 4); memcpy(bmp + 22, &hh, 4); memcpy(bmp + 26, &planes, 2); memcpy(bmp + 28, &bpp, 2);
                snprintf(path, sizeof path, "%s%sfg_%05lld_made.bmp", dir ? dir : "", dir ? "/" : "", frame);
                FILE *f = fopen(path, "wb"); if (f) { fwrite(bmp, 1, fsz, f); fclose(f); }
                free(bmp);
            }
            vkDestroyBuffer(V.device, buf, NULL); vmem_free(bm);
        }
    }
    g_fg_made++;
    fgt_report();
    if (g_fg_made == 1 || g_fg_made % 600 == 0) printf("nvn_vk_fg: frames made so far %lld (skipped %lld: no vectors for that frame yet, a cut, or nothing to warp)\n", (long long)g_fg_made, (long long)g_fg_skipped);
    return 1;
}
