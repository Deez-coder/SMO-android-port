/* vk_shaderc_android.c - GLSL (Vulkan dialect) -> SPIR-V on Android: the same interface as vk_shaderc_shim.c, with shaderc
 * (the NDK's own sources, built by tools/android/build_shaderc.py) linked in instead of a DLL loaded at run time.
 * Settings as the shim's: Vulkan 1.3 environment, SPIR-V 1.6, no optimisation, entry point "main".
 * stage: 0 vertex, 1 fragment, 2 geometry, 5 compute (the MX_* numbers of mx_translate.h). Thread-safe: one compiler
 * object, made once; shaderc compilers may be used from several threads. Android port (2026-10-27). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shaderc/shaderc.h>
#include "plat.h"

static shaderc_compiler_t g_compiler;
static shaderc_compile_options_t g_options;
static plat_once g_once = PLAT_ONCE_INIT;

static void init_once(void) {
    g_compiler = shaderc_compiler_initialize();
    g_options = shaderc_compile_options_initialize();
    if (!g_options) return;
    shaderc_compile_options_set_target_env(g_options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    shaderc_compile_options_set_target_spirv(g_options, shaderc_spirv_version_1_6);
}

void smo_spirv_free(uint32_t *words) { free(words); }

int smo_glsl_to_spirv(int stage, const char *src, uint32_t **words, size_t *count, char *log, size_t log_size) {
    int ok = 0;
    if (log && log_size) log[0] = 0;
    *words = NULL; *count = 0;
    plat_once_run(&g_once, init_once);
    if (!g_compiler || !g_options) { if (log) snprintf(log, log_size, "shaderc initialisation failed"); return 0; }
    shaderc_shader_kind kind = stage == 0 ? shaderc_vertex_shader : stage == 1 ? shaderc_fragment_shader : stage == 2 ? shaderc_geometry_shader
                             : stage == 5 ? shaderc_compute_shader : (shaderc_shader_kind)-1;
    if ((int)kind < 0) { if (log) snprintf(log, log_size, "stage %d is not vertex (0), fragment (1), geometry (2) or compute (5)", stage); return 0; }
    shaderc_compilation_result_t r = shaderc_compile_into_spv(g_compiler, src, strlen(src), kind, "nvn_stage", "main", g_options);
    if (!r) { if (log) snprintf(log, log_size, "shaderc_compile_into_spv returned no result"); return 0; }
    const char *msg = shaderc_result_get_error_message(r);
    if (msg && msg[0] && log) snprintf(log, log_size, "%s", msg);
    if (shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success) {
        size_t n = shaderc_result_get_length(r) / 4;
        if (n) {
            *words = malloc(n * sizeof(uint32_t));
            if (*words) { memcpy(*words, shaderc_result_get_bytes(r), n * 4); *count = n; ok = 1; }
        }
    }
    shaderc_result_release(r);
    return ok;
}
