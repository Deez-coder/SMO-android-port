/* android_ovstats.c - the performance overlay's phone readings (0.5.3; the user asked for the items of Peanuts Native's
 * overlay: igawa6/peanuts-native, Overlay.java / Hud.java / SocZones.java, ideas only). Read on the render thread when the
 * overlay builds its lines (nvn_vk_draw.c ov_text_fill, twice a second); the slow files at most every 2 s.
 *   CPU: the main cluster's (the policy with the most cores) scaling_max_freq against its cpuinfo_max_freq, in percent: the
 *        clock cap the system has set (thermal limits show here first), as Peanuts' CPU row.
 *   SoC: the hottest /sys/class/thermal zone whose type starts with "cpu" or "gpu" (millidegrees above 1000; values below
 *        -100 or from 200 C on are left out); -1000 when none can be read (some phones refuse them).
 *   RAM: the game's resident memory (/proc/self/status VmRSS) and the phone's used / total (/proc/meminfo).
 *   battery: Android's ACTION_BATTERY_CHANGED, passed in by GameActivity every 2 s (the battery's file is refused to apps
 *        on the S24); thermal state: AThermal_getCurrentThermalStatus. */
#include <android/thermal.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile int g_bat_temp10 = -1000, g_bat_level = -1, g_bat_charging;
void android_ovstats_battery(int temp10, int level, int charging) { g_bat_temp10 = temp10; g_bat_level = level; g_bat_charging = charging; }

static long rd_num(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}
static int64_t ms_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; }

typedef struct { int cpu_pct, soc10, game_mb, used_mb, total_mb, bat10, bat_level, bat_charging, thermal, headroom; } OvStats;
static OvStats g_s = {-1, -1000, -1, -1, -1, -1000, -1, 0, -1, -1};
static char g_main_policy[96];

static void find_main_policy(void) {
    DIR *d = opendir("/sys/devices/system/cpu/cpufreq");
    struct dirent *e;
    int best = -1;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "policy", 6)) continue;
        char p[160], buf[128];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpufreq/%s/related_cpus", e->d_name);
        FILE *f = fopen(p, "r");
        int n = 0;
        if (f) { if (fgets(buf, sizeof buf, f)) { for (char *s = strtok(buf, " \n"); s; s = strtok(NULL, " \n")) n++; } fclose(f); }
        if (n > best) { best = n; snprintf(g_main_policy, sizeof g_main_policy, "%s", e->d_name); }
    }
    if (d) closedir(d);
}

void android_ovstats(int out[10]) {
    static int64_t slow_at;
    static AThermalManager *tm;
    static int tm_tried;
    int64_t now = ms_now();
    if (!slow_at || now - slow_at >= 2000) {
        slow_at = now;
        if (!g_main_policy[0]) find_main_policy();
        if (g_main_policy[0]) {
            char p[200], q[200];
            snprintf(p, sizeof p, "/sys/devices/system/cpu/cpufreq/%s/scaling_max_freq", g_main_policy);
            snprintf(q, sizeof q, "/sys/devices/system/cpu/cpufreq/%s/cpuinfo_max_freq", g_main_policy);
            long cap = rd_num(p), top = rd_num(q);
            g_s.cpu_pct = cap > 0 && top > 0 ? (int)(cap * 100 / top) : -1;
        }
        {   int hot = -1000;
            DIR *d = opendir("/sys/class/thermal");
            struct dirent *e;
            while (d && (e = readdir(d))) {
                if (strncmp(e->d_name, "thermal_zone", 12)) continue;
                char p[200], ty[64] = "";
                snprintf(p, sizeof p, "/sys/class/thermal/%s/type", e->d_name);
                FILE *f = fopen(p, "r");
                if (!f) continue;
                if (!fgets(ty, sizeof ty, f)) ty[0] = 0;
                fclose(f);
                if ((strncmp(ty, "cpu", 3) && strncmp(ty, "gpu", 3)) || strstr(ty, "trip")) continue;
                snprintf(p, sizeof p, "/sys/class/thermal/%s/temp", e->d_name);
                long v = rd_num(p);
                if (v == -1) continue;
                double c = v > 1000 ? v / 1000.0 : (double)v;
                if (c < -100 || c >= 200) continue;
                if ((int)(c * 10) > hot) hot = (int)(c * 10);
            }
            if (d) closedir(d);
            g_s.soc10 = hot;
        }
        {   FILE *f = fopen("/proc/self/status", "r");
            char l[160];
            g_s.game_mb = -1;
            while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "VmRSS:", 6)) { g_s.game_mb = (int)(atol(l + 6) / 1024); break; }
            if (f) fclose(f);
            f = fopen("/proc/meminfo", "r");
            long tot = -1, av = -1;
            while (f && fgets(l, sizeof l, f)) { if (!strncmp(l, "MemTotal:", 9)) tot = atol(l + 9); else if (!strncmp(l, "MemAvailable:", 13)) av = atol(l + 13); }
            if (f) fclose(f);
            g_s.total_mb = tot > 0 ? (int)(tot / 1024) : -1;
            g_s.used_mb = tot > 0 && av >= 0 ? (int)((tot - av) / 1024) : -1;
        }
        if (!tm_tried) { tm_tried = 1; tm = AThermal_acquireManager(); }
        g_s.thermal = tm ? (int)AThermal_getCurrentThermalStatus(tm) : -1;
        {   /* the 10 s headroom forecast (Android 12+; looked up so that Android 11 starts too): 1.0 = where the phone throttles */
            static float (*headroom)(AThermalManager *, int);
            static int looked;
            if (!looked) { looked = 1; headroom = (float (*)(AThermalManager *, int))dlsym(RTLD_DEFAULT, "AThermal_getThermalHeadroom"); }
            float h = tm && headroom ? headroom(tm, 10) : -1.0f;
            g_s.headroom = h >= 0.0f && h < 10.0f ? (int)(h * 100.0f) : -1;      /* NaN or absurd: unknown */
        }
    }
    g_s.bat10 = g_bat_temp10; g_s.bat_level = g_bat_level; g_s.bat_charging = g_bat_charging;
    out[0] = g_s.cpu_pct; out[1] = g_s.soc10; out[2] = g_s.game_mb; out[3] = g_s.used_mb; out[4] = g_s.total_mb;
    out[5] = g_s.bat10; out[6] = g_s.bat_level; out[7] = g_s.bat_charging; out[8] = g_s.thermal; out[9] = g_s.headroom;
}
