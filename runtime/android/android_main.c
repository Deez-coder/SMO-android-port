/* android_main.c - the Android host (NativeActivity, android_native_app_glue).
 *
 * The activity's thread runs the looper: window, focus and lifecycle commands, input events (game controllers, a
 * hardware keyboard, the touch screen) and the motion sensors. The game runs on threads of its own: smo_main (boot.c)
 * is started once the files are found, and its display_init waits here for the window (plat_window_wait).
 *
 * Files (the app's external files folder, filled by tools/android/push_data.py or the launcher's import):
 *   <files>/romfs/...                    the game's RomFS
 *   <files>/manual/...                   the Help pages (optional)
 *   <files>/dump/base/elf/main.elf, sdk.elf (+ subsdk0.elf, rtld.elf), <files>/dump/base/exefs/main.npdm
 *   <files>/config/graphics.ini, controllers.ini (written with defaults at the first start)
 *   <files>/userdata/...                 save data; <files>/cache/vk/...  translated shaders and the pipeline cache
 *   <files>/log.txt                      everything the runtime prints (also in logcat, tag "smo")
 *   <files>/log.prev.txt                 the same for the run before
 * The working folder is <files>/bin, so the runtime's paths relative to its exe (../romfs, ../userdata) are the same as
 * on Windows. Android port (2026-10-27). */
#include <android/configuration.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/sensor.h>
#include <android/window.h>
#include <aaudio/AAudio.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "plat.h"
#include "android_host.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "smo", __VA_ARGS__)

static struct android_app *g_app;
static char g_root[PLAT_MAX_PATH];

/* ---------------------------------------------------------------- the window */
static pthread_mutex_t g_win_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_win_cv = PTHREAD_COND_INITIALIZER;
static ANativeWindow *g_window;                      /* the current window, NULL between TERM_WINDOW and INIT_WINDOW */
static volatile int g_focused, g_visible = 1, g_window_gen;

void *plat_window_wait(void) {
    pthread_mutex_lock(&g_win_lock);
    while (!g_window) pthread_cond_wait(&g_win_cv, &g_win_lock);
    void *w = g_window;
    pthread_mutex_unlock(&g_win_lock);
    return w;
}
int plat_window_size(void *win, int *width, int *height) {
    ANativeWindow *w = win ? (ANativeWindow *)win : g_window;
    if (!w || w != g_window) return 0;                /* a window that is gone has no size */
    int a = ANativeWindow_getWidth(w), b = ANativeWindow_getHeight(w);
    if (a <= 0 || b <= 0) return 0;
    *width = a; *height = b;
    return 1;
}
int plat_window_minimized(void *win) { (void)win; return !g_window || !g_visible; }
void plat_window_set_title(void *win, const char *title) {
    (void)win;
    static uint64_t last;
    uint64_t now = plat_ms64();
    if (now - last >= 10000) { last = now; LOGI("%s", title); }     /* the frame counter line, every 10 s in the log */
}
static volatile int g_menu_open;                      /* 0.2.3: the in-game menu (GameActivity) is shown: it takes the focus, but the game is not in the background */
int plat_window_focused(void) { return (g_focused || g_menu_open) && g_visible; }
const char *android_internal_path(void) { return g_app && g_app->activity ? g_app->activity->internalDataPath : NULL; }      /* android_driver.c */
int android_window_generation(void) { return g_window_gen; }
void *android_window_current(void) { return g_window; }

void plat_keep_screen_on(int on) {
    if (!g_app) return;
    if (on) ANativeActivity_setWindowFlags(g_app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    else ANativeActivity_setWindowFlags(g_app->activity, 0, AWINDOW_FLAG_KEEP_SCREEN_ON);
}
void plat_show_message(const char *title, const char *text) { LOGI("%s: %s", title, text); printf("%s: %s\n", title, text); }
int guide_show(const char *url) { (void)url; return 0; }      /* the Help pages: no web view yet (nnapplet.c reports it) */

/* ---------------------------------------------------------------- JNI: the display's rotation, the device's language */
static JNIEnv *jni_env(void) {
    JNIEnv *env = NULL;
    JavaVM *vm = g_app->activity->vm;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) (*vm)->AttachCurrentThread(vm, &env, NULL);
    return env;
}
/* Leaving the game: GameActivity.askExit shows the question (on the UI thread) and holds the game meanwhile
 * (nativeHold); Exit waits for any save in progress (nativeQuiesce) before the activity goes. */
void android_exit_request(void) {                    /* the looper thread (input) */
    JNIEnv *env = jni_env();
    jobject act = g_app->activity->clazz;
    jclass c = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, c, "askExit", "()V");
    if (m) (*env)->CallVoidMethod(env, act, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);      /* started as plain NativeActivity (no askExit) */
    (*env)->DeleteLocalRef(env, c);
}
void display_menu_hold(int on);                       /* display.c */
void fs_quiesce(void);                                /* nnfs.c */
void nvn_vk_pcache_save(const char *why);             /* nvn_vk_m4.c */
int nvn_vk_load_progress(long long *done, long long *total);      /* nvn_vk_m4.c */
long long nvn_vk_window_presents(void);               /* nvn_vk_draw.c */
int nvn_vk_first_prep(long long *done, long long *total);      /* nvn_vk_m4.c */
void nvn_vk_first_prep_skip(void);                    /* nvn_vk_m4.c */
static void JNICALL native_skip_prep(JNIEnv *env, jobject self) { (void)env; (void)self; nvn_vk_first_prep_skip(); }
static void JNICALL native_hold(JNIEnv *env, jobject self, jboolean on) { (void)env; (void)self; display_menu_hold(on ? 1 : 0); }
static void JNICALL native_quiesce(JNIEnv *env, jobject self) {
    (void)env; (void)self;
    printf("smo: leaving the game: waiting for any save in progress%c", 10);
    fs_quiesce();
    printf("smo: leaving the game: no save in progress%c", 10);
    fflush(stdout);
}
static void JNICALL native_save_cache(JNIEnv *env, jobject self, jboolean leaving) {      /* GameActivity: a thread of its own */
    (void)env; (void)self;
    nvn_vk_pcache_save(leaving ? "the game is left" : "the game went to the background");
}
/* GameActivity's loading screen, polled from the UI thread: {frames that reached the window, pipelines made ahead,
 * of how many, 1 when they are all made, first-start preparation running, programs seen, of how many} */
static void JNICALL native_load_state(JNIEnv *env, jobject self, jlongArray out) {
    (void)self;
    long long done = 0, total = 0, pdone = 0, ptotal = 0;
    int fin = nvn_vk_load_progress(&done, &total);
    int prep = nvn_vk_first_prep(&pdone, &ptotal);
    jlong v[7] = {nvn_vk_window_presents(), done, total, fin, prep, pdone, ptotal};
    (*env)->SetLongArrayRegion(env, out, 0, 7, v);
}
#include "../gfx_options.h"
void nvn_vk_present_info(int out[5]);                 /* nvn_vk_draw.c */
/* 0.2.3, the in-game menu (GameActivity): graphics.ini was written, the keys that take effect at once are read again */
/* without it the game counted as obscured while the menu has the focus and suspended itself (nnmisc.c oe_watch), so a
 * change of the upscaler could not be shown until the menu was closed */
static void JNICALL native_menu_open(JNIEnv *env, jobject self, jboolean on) { (void)env; (void)self; g_menu_open = on ? 1 : 0; }
static void JNICALL native_reload_options(JNIEnv *env, jobject self) { (void)env; (void)self; gfx_options_reload_live(); }
/* and what it shows: {the game's picture width, height, its size on the screen width, height, fps} */
static void JNICALL native_menu_info(JNIEnv *env, jobject self, jintArray out) {
    (void)self;
    int v[5];
    nvn_vk_present_info(v);
    (*env)->SetIntArrayRegion(env, out, 0, 5, v);
}
static void register_natives(void) {
    JNIEnv *env = jni_env();
    jclass c = (*env)->GetObjectClass(env, g_app->activity->clazz);
    JNINativeMethod m[8] = {{"nativeHold", "(Z)V", (void *)native_hold}, {"nativeQuiesce", "()V", (void *)native_quiesce}, {"nativeSaveCache", "(Z)V", (void *)native_save_cache},
                            {"nativeLoadState", "([J)V", (void *)native_load_state}, {"nativeSkipPrep", "()V", (void *)native_skip_prep},
                            {"nativeReloadOptions", "()V", (void *)native_reload_options}, {"nativeMenuInfo", "([I)V", (void *)native_menu_info},
                            {"nativeMenuOpen", "(Z)V", (void *)native_menu_open}};
    if ((*env)->RegisterNatives(env, c, m, 8) != 0) { (*env)->ExceptionClear(env); printf("smo: no GameActivity: no Exit button%c", 10); }
    (*env)->DeleteLocalRef(env, c);
}
static int display_rotation(void) {                  /* Surface.ROTATION_0 .. 270 as 0..3 */
    JNIEnv *env = jni_env();
    jobject act = g_app->activity->clazz;
    jclass ac = (*env)->GetObjectClass(env, act);
    jmethodID gwm = (*env)->GetMethodID(env, ac, "getWindowManager", "()Landroid/view/WindowManager;");
    jobject wm = gwm ? (*env)->CallObjectMethod(env, act, gwm) : NULL;
    int rot = 1;
    if (wm) {
        jclass wc = (*env)->GetObjectClass(env, wm);
        jmethodID gd = (*env)->GetMethodID(env, wc, "getDefaultDisplay", "()Landroid/view/Display;");
        jobject d = gd ? (*env)->CallObjectMethod(env, wm, gd) : NULL;
        if (d) {
            jclass dc = (*env)->GetObjectClass(env, d);
            jmethodID gr = (*env)->GetMethodID(env, dc, "getRotation", "()I");
            if (gr) rot = (*env)->CallIntMethod(env, d, gr);
        }
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); rot = 1; }
    return rot & 3;
}

/* 0.3.6, THE WINDOW'S FRAME RATE FOLLOWS FPS_CAP. The window asked for 60 Hz whatever the cap, so with fps_cap 40 a panel
 * left at 60 Hz showed the frames for one and two refreshes by turns (16.7 and 33.3 ms: judder, however steady the
 * game's 25 ms). A cap that does not divide 60 (40, 45, 50) is now asked for as the window's rate: Android then picks a
 * refresh rate that rate divides (120 Hz for 40, 90 for 45) where the panel has one, and stays at 60 Hz where it has not,
 * which plays as before. Caps that divide 60 (20, 30, 60) keep asking for 60 Hz (the heat fix of 2026-11-03). The render
 * thread asks (android_window_rate), the looper thread applies it and logs the display's refresh rate 1.5 s later.
 * The idea is from nx2android (perf_android.c: its 40 fps step asks the display for 120 Hz). */
static float g_win_rate = 60.0f;                     /* the rate the window is asked for (g_win_lock) */
static volatile int g_rate_req, g_rate_done;          /* requests from the render thread, applied by the looper thread */
static volatile float g_rate_want = 60.0f;
static int64_t g_rate_check_at;                       /* looper thread: when to log the display's refresh rate (0: not) */
static int64_t mono_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec; }
static jobject default_display(JNIEnv *env) {
    jobject act = g_app->activity->clazz, d = NULL;
    jclass ac = (*env)->GetObjectClass(env, act);
    jmethodID gwm = (*env)->GetMethodID(env, ac, "getWindowManager", "()Landroid/view/WindowManager;");
    jobject wm = gwm ? (*env)->CallObjectMethod(env, act, gwm) : NULL;
    if (wm) {
        jclass wc = (*env)->GetObjectClass(env, wm);
        jmethodID gd = (*env)->GetMethodID(env, wc, "getDefaultDisplay", "()Landroid/view/Display;");
        d = gd ? (*env)->CallObjectMethod(env, wm, gd) : NULL;
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); d = NULL; }
    return d;
}
static float display_refresh(void) {                 /* the display's refresh rate now (Display.getRefreshRate), 0 = unknown */
    JNIEnv *env = jni_env();
    jobject d = default_display(env);
    float hz = 0;
    if (d) {
        jclass dc = (*env)->GetObjectClass(env, d);
        jmethodID gr = (*env)->GetMethodID(env, dc, "getRefreshRate", "()F");
        if (gr) hz = (*env)->CallFloatMethod(env, d, gr);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); hz = 0; }
    return hz;
}
static void display_modes_log(void) {                /* once: the refresh rates the panel offers (Display.getSupportedModes) */
    JNIEnv *env = jni_env();
    jobject d = default_display(env);
    char t[256]; int n = 0; t[0] = 0;
    if (d) {
        jclass dc = (*env)->GetObjectClass(env, d);
        jmethodID gm = (*env)->GetMethodID(env, dc, "getSupportedModes", "()[Landroid/view/Display$Mode;");
        jobjectArray modes = gm ? (jobjectArray)(*env)->CallObjectMethod(env, d, gm) : NULL;
        float seen[16]; int ns = 0;
        int cnt = modes && !(*env)->ExceptionCheck(env) ? (*env)->GetArrayLength(env, modes) : 0;
        for (int i = 0; i < cnt && n < (int)sizeof t - 16; i++) {
            jobject m = (*env)->GetObjectArrayElement(env, modes, i);
            if (!m) continue;
            jclass mc = (*env)->GetObjectClass(env, m);
            jmethodID mr = (*env)->GetMethodID(env, mc, "getRefreshRate", "()F");
            float hz = mr ? (*env)->CallFloatMethod(env, m, mr) : 0;
            int k; for (k = 0; k < ns && fabsf(seen[k] - hz) > 0.5f; k++) {}
            if (k == ns && ns < 16 && hz > 0) { seen[ns++] = hz; n += snprintf(t + n, sizeof t - n, "%s%.0f", ns > 1 ? ", " : "", hz); }
            (*env)->DeleteLocalRef(env, mc); (*env)->DeleteLocalRef(env, m);
        }
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    printf("android: display refresh rates offered: %s Hz (now %.0f Hz)%c", t[0] ? t : "unknown", display_refresh(), 10);
}
/* The render thread: the rate the window should ask for (fps). Applied on the looper thread. */
void android_window_rate(float fps) {
    if (getenv("SMO_FRAME_RATE")) return;             /* a test's fixed rate wins */
    g_rate_want = fps;
    __atomic_add_fetch(&g_rate_req, 1, __ATOMIC_SEQ_CST);
    if (g_app && g_app->looper) ALooper_wake(g_app->looper);
}
static void rate_poll(void) {                         /* the looper thread, after each wake */
    int req = g_rate_req;
    if (req != g_rate_done) {
        g_rate_done = req;
        float fps = g_rate_want;
        pthread_mutex_lock(&g_win_lock);
        int changed = fps != g_win_rate, r = 0;
        g_win_rate = fps;
        if (changed && g_window) r = ANativeWindow_setFrameRate(g_window, fps, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
        pthread_mutex_unlock(&g_win_lock);
        if (changed) { printf("android: window frame rate %.0f fps asked for (fps_cap)%s%c", fps, r ? " (refused)" : "", 10); g_rate_check_at = mono_ns() + 1500000000LL; }
    }
    if (g_rate_check_at && mono_ns() >= g_rate_check_at) { g_rate_check_at = 0; printf("android: display refresh rate now %.0f Hz%c", display_refresh(), 10); }
}

/* ---------------------------------------------------------------- keyboard (PLAT_VK_* numbers) */
static volatile uint8_t g_keys[256];
int plat_key_down(int vk) { return vk > 0 && vk < 256 && g_keys[vk]; }
static int vk_of(int32_t code) {
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z) return 'A' + (code - AKEYCODE_A);
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9) return '0' + (code - AKEYCODE_0);
    if (code >= AKEYCODE_F1 && code <= AKEYCODE_F12) return PLAT_VK_F1 + (code - AKEYCODE_F1);
    switch (code) {
    case AKEYCODE_ENTER: return PLAT_VK_RETURN;
    case AKEYCODE_DEL: return PLAT_VK_BACK;
    case AKEYCODE_ESCAPE: return PLAT_VK_ESCAPE;
    case AKEYCODE_SPACE: return PLAT_VK_SPACE;
    case AKEYCODE_TAB: return PLAT_VK_TAB;
    case AKEYCODE_SHIFT_LEFT: case AKEYCODE_SHIFT_RIGHT: return PLAT_VK_SHIFT;
    case AKEYCODE_CTRL_LEFT: case AKEYCODE_CTRL_RIGHT: return PLAT_VK_CONTROL;
    case AKEYCODE_DPAD_LEFT: return PLAT_VK_LEFT;
    case AKEYCODE_DPAD_RIGHT: return PLAT_VK_RIGHT;
    case AKEYCODE_DPAD_UP: return PLAT_VK_UP;
    case AKEYCODE_DPAD_DOWN: return PLAT_VK_DOWN;
    }
    return 0;
}

/* ---------------------------------------------------------------- input */
static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    int32_t type = AInputEvent_getType(ev), source = AInputEvent_getSource(ev), dev = AInputEvent_getDeviceId(ev);
    int pad = (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(ev), act = AKeyEvent_getAction(ev);
        if (code == AKEYCODE_BACK && !pad) { android_pad_key(ANDROID_PAD_BACK_GESTURE, code, act == AKEY_EVENT_ACTION_DOWN); return 1; }   /* the back gesture = Minus on the touch pad */
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_VOLUME_MUTE) return 0;      /* the system's */
        if (pad || (code >= AKEYCODE_BUTTON_A && code <= AKEYCODE_BUTTON_MODE)) { if (android_pad_key(dev, code, act == AKEY_EVENT_ACTION_DOWN)) return 1; }
        int vk = vk_of(code);
        if (vk && act != AKEY_EVENT_ACTION_MULTIPLE) { g_keys[vk] = act == AKEY_EVENT_ACTION_DOWN; return 1; }
        return 0;
    }
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if (pad) {
            float ax[AMOTION_EVENT_AXIS_GENERIC_16 + 1];
            for (int a = 0; a <= AMOTION_EVENT_AXIS_GENERIC_16; a++) ax[a] = AMotionEvent_getAxisValue(ev, a, 0);
            android_pad_axes(dev, ax, AMOTION_EVENT_AXIS_GENERIC_16 + 1);
            return 1;
        }
        if ((source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN) {
            int32_t action = AMotionEvent_getAction(ev), kind = action & AMOTION_EVENT_ACTION_MASK;
            size_t idx = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            int w = g_window ? ANativeWindow_getWidth(g_window) : 1, h = g_window ? ANativeWindow_getHeight(g_window) : 1;
            size_t n = AMotionEvent_getPointerCount(ev);
            for (size_t i = 0; i < n; i++) {
                int id = AMotionEvent_getPointerId(ev, i);
                float x = AMotionEvent_getX(ev, i) / (float)(w > 0 ? w : 1), y = AMotionEvent_getY(ev, i) / (float)(h > 0 ? h : 1);
                int down = 1;
                if ((kind == AMOTION_EVENT_ACTION_UP || kind == AMOTION_EVENT_ACTION_POINTER_UP) && i == idx) down = 0;
                if (kind == AMOTION_EVENT_ACTION_UP || kind == AMOTION_EVENT_ACTION_CANCEL) down = kind == AMOTION_EVENT_ACTION_UP && i != idx;
                if (kind == AMOTION_EVENT_ACTION_CANCEL) down = 0;
                android_touch(id, x, y, down);
            }
            return 1;
        }
    }
    return 0;
}

/* 0.4: the phone's RAM, kB (/proc/meminfo MemTotal, MemAvailable; -1: not readable), for the start line, the low-memory note and
 * the memory line every 300 frames (nvn_vk_draw.c) */
void android_meminfo(long *total_kb, long *avail_kb) {
    *total_kb = *avail_kb = -1;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "MemTotal:", 9)) *total_kb = atol(line + 9);
        else if (!strncmp(line, "MemAvailable:", 13)) { *avail_kb = atol(line + 13); break; }
    }
    fclose(f);
}
/* ---------------------------------------------------------------- motion sensors: the phone as a handheld console */
static ASensorEventQueue *g_sq;
static const ASensor *g_accel, *g_gyro;
static int g_rotation = 1;
/* 0.3.22, BATTERY: the sensors ran at 200 readings a second each (5 ms) whenever the window had the focus, motion controls
 * on or off, and every reading woke this thread. The game keeps only the latest one (android_motion), read once a frame:
 * now 100 a second (SMO_SENSOR_US to change), and none while controls.ini says motion = off (nnpad_android.c calls
 * android_sensors_wanted when it reads the file). */
static volatile int g_sensors_wanted = 1, g_sensors_on, g_focus_sensors;
static void sensors_start(void) {
    g_focus_sensors = 1;
    if (!g_sensors_wanted || g_sensors_on) return;
    ASensorManager *sm = ASensorManager_getInstanceForPackage("com.mobileodyssey.game");
    if (!sm) return;
    g_accel = ASensorManager_getDefaultSensor(sm, ASENSOR_TYPE_ACCELEROMETER);
    g_gyro = ASensorManager_getDefaultSensor(sm, ASENSOR_TYPE_GYROSCOPE);
    if (!g_sq) g_sq = ASensorManager_createEventQueue(sm, g_app->looper, LOOPER_ID_USER, NULL, NULL);
    if (!g_sq) return;
    const char *e = getenv("SMO_SENSOR_US");
    int us = e && atoi(e) > 0 ? atoi(e) : 10000;
    if (g_accel) { ASensorEventQueue_enableSensor(g_sq, g_accel); ASensorEventQueue_setEventRate(g_sq, g_accel, us); }
    if (g_gyro) { ASensorEventQueue_enableSensor(g_sq, g_gyro); ASensorEventQueue_setEventRate(g_sq, g_gyro, us); }
    g_sensors_on = 1;
    g_rotation = display_rotation();
    static int said;
    if (!said++) printf("android: motion sensors on, a reading every %d us%c", us, 10);
}
static void sensors_off(void) {
    if (!g_sq || !g_sensors_on) return;
    g_sensors_on = 0;
    if (g_accel) ASensorEventQueue_disableSensor(g_sq, g_accel);
    if (g_gyro) ASensorEventQueue_disableSensor(g_sq, g_gyro);
}
static void sensors_stop(void) { g_focus_sensors = 0; sensors_off(); }
void android_sensors_wanted(int on) {          /* from the game thread: controls.ini read */
    g_sensors_wanted = on;
    if (!on && g_sensors_on) { sensors_off(); printf("android: motion sensors off (motion = off)%c", 10); }
    else if (on && g_focus_sensors && !g_sensors_on) sensors_start();
}
/* device axes (x right, y up, z out of the screen in the natural orientation) -> the screen's axes as the player holds
 * it: the axes a game controller reports (SDL / Android: x right, y up, z toward the player) */
static void to_screen(const float d[3], float o[3]) {
    switch (g_rotation) {
    case 0: o[0] = d[0]; o[1] = d[1]; break;
    case 1: o[0] = -d[1]; o[1] = d[0]; break;
    case 2: o[0] = -d[0]; o[1] = -d[1]; break;
    default: o[0] = d[1]; o[1] = -d[0]; break;
    }
    o[2] = d[2];
}
static void sensors_read(void) {
    ASensorEvent e[16];
    ssize_t n;
    while (g_sq && (n = ASensorEventQueue_getEvents(g_sq, e, 16)) > 0)
        for (ssize_t i = 0; i < n; i++) {
            float s[3];
            if (e[i].type == ASENSOR_TYPE_ACCELEROMETER) { to_screen(e[i].acceleration.v, s); android_motion(s, NULL); }
            else if (e[i].type == ASENSOR_TYPE_GYROSCOPE) { to_screen(e[i].vector.v, s); android_motion(NULL, s); }
        }
}

/* ---------------------------------------------------------------- sound: AAudio, low latency, the device pulls */
static plat_audio_fill g_fill;
static void *g_fill_ud;
static int g_channels = 2;
static aaudio_data_callback_result_t audio_cb(AAudioStream *s, void *ud, void *data, int32_t frames) {
    (void)s; (void)ud;
    g_fill((int16_t *)data, frames, g_fill_ud);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static AAudioStream *g_stream;
static void audio_error(AAudioStream *s, void *ud, aaudio_result_t err);
static int audio_start(int rate) {
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return 0;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, g_channels);
    AAudioStreamBuilder_setSampleRate(b, rate);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    AAudioStreamBuilder_setErrorCallback(b, audio_error, NULL);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { printf("audio: AAudio stream not opened (%s)\n", AAudio_convertResultToText(r)); return 0; }
    AAudioStream_setBufferSizeInFrames(g_stream, AAudioStream_getFramesPerBurst(g_stream) * 2);
    r = AAudioStream_requestStart(g_stream);
    printf("audio: AAudio %d Hz %d channels, burst %d frames, buffer %d frames, %s\n", AAudioStream_getSampleRate(g_stream), g_channels,
           AAudioStream_getFramesPerBurst(g_stream), AAudioStream_getBufferSizeInFrames(g_stream), r == AAUDIO_OK ? "started" : "NOT started");
    return r == AAUDIO_OK;
}
static void *audio_restart(void *arg) {               /* the device changed (headphones, Bluetooth): a new stream */
    int rate = (int)(intptr_t)arg;
    if (g_stream) { AAudioStream_close(g_stream); g_stream = NULL; }
    audio_start(rate);
    return NULL;
}
static void audio_error(AAudioStream *s, void *ud, aaudio_result_t err) {
    (void)ud;
    if (err != AAUDIO_ERROR_DISCONNECTED) return;
    pthread_t t;                                      /* the stream may not be closed on its own callback thread */
    pthread_create(&t, NULL, audio_restart, (void *)(intptr_t)AAudioStream_getSampleRate(s));
    pthread_detach(t);
}
int plat_audio_open(int rate, int channels, plat_audio_fill fill, void *ud) {
    g_fill = fill; g_fill_ud = ud; g_channels = channels;
    return audio_start(rate);
}

/* ---------------------------------------------------------------- the game */
int smo_main(int argc, char **argv);                  /* boot.c */
static void *game_thread(void *arg) {
    (void)arg;
    static char *argv[] = {"smo", "../dump/base/elf/main.elf", NULL};
    int code = smo_main(2, argv);
    printf("smo_main returned %d\n", code);
    fflush(stdout);
    _exit(code);
    return NULL;
}
static int need_file(const char *rel) {
    char p[PLAT_MAX_PATH];
    snprintf(p, sizeof p, "%s/%s", g_root, rel);
    if (plat_exists(p)) return 1;
    printf("missing: %s\n", p);
    return 0;
}
static void set_language(struct android_app *app) {   /* SMO_LOCALE ("en-US") and SMO_COUNTRY for nnmisc.c / nnmii.c */
    char lang[3] = {0}, country[3] = {0}, tag[8];
    AConfiguration_getLanguage(app->config, lang);
    AConfiguration_getCountry(app->config, country);
    if (!lang[0]) return;
    snprintf(tag, sizeof tag, country[0] ? "%s-%s" : "%s", lang, country);
    setenv("SMO_LOCALE", tag, 0);
    if (country[0]) setenv("SMO_COUNTRY", country, 0);
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        pthread_mutex_lock(&g_win_lock);
        g_window = app->window;
        g_window_gen++;
        pthread_cond_broadcast(&g_win_cv);
        pthread_mutex_unlock(&g_win_lock);
        {   /* The game's frame rate, so the display runs at a rate that shows 60 frames a second evenly (a 120 Hz panel left
             * at its own choice showed frames paced by display.c's clock for one, two or three refreshes).
             * SMO_FRAME_RATE=<fps> (0: not set). Heat (2026-11-03): COMPATIBILITY_DEFAULT, not FIXED_SOURCE. The compositor
             * still raised a FIXED_SOURCE (video) vote to 120 Hz for every touch, and the touch controls kept the panel at
             * 120 Hz for whole sessions. With DEFAULT (and GameActivity's touch boost off) 10 of 13 runs stayed at 60
             * Hz after 8 swipes on a Galaxy S24 Ultra; three still went to 120. */
            const char *e = getenv("SMO_FRAME_RATE");
            float fps = e ? (float)atof(e) : g_win_rate;      /* 0.3.6: the rate fps_cap asks for (android_window_rate) */
            if (fps > 0 && app->window) {
                int r = ANativeWindow_setFrameRate(app->window, fps, getenv("SMO_FRAME_RATE_VIDEO") ? ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE : ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
                printf("android: window frame rate %.0f fps asked for%s\n", fps, r ? " (refused)" : "");
            }
        }
        android_window_changed(app->window);          /* nvn_vk: a new surface when the renderer runs already */
        break;
    case APP_CMD_TERM_WINDOW:
        android_window_changed(NULL);                 /* the renderer lets go of the surface before the window goes */
        pthread_mutex_lock(&g_win_lock);
        g_window = NULL;
        pthread_mutex_unlock(&g_win_lock);
        break;
    case APP_CMD_GAINED_FOCUS: g_focused = 1; sensors_start(); break;
    case APP_CMD_LOST_FOCUS: {
        g_focused = 0; sensors_stop();
        static int n;                                 /* 0.3.1: a notification, the shade, a floating window: the game keeps running (nnmisc.c oe_window_state) */
        if (++n <= 20) printf("android: the window lost the focus (%d)%s\n", n, g_visible ? " while still shown: the game keeps running" : "");
        break;
    }
    case APP_CMD_RESUME: g_visible = 1; break;
    case APP_CMD_PAUSE: case APP_CMD_STOP: g_visible = 0; break;
    case APP_CMD_CONFIG_CHANGED: g_rotation = display_rotation(); break;
    case APP_CMD_LOW_MEMORY: { long t, a; android_meminfo(&t, &a); printf("android: the system is low on memory (the phone has %ld MB, %ld MB available now)\n", t >> 10, a >> 10); fflush(stdout); break; }
    }
}

void android_main(struct android_app *app) {
    g_app = app;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    const char *files = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
    snprintf(g_root, sizeof g_root, "%s", files);
    plat_set_data_root(g_root);
    {   char p[PLAT_MAX_PATH];
        snprintf(p, sizeof p, "%s/bin", g_root); plat_mkdirs(p);
        if (chdir(p) != 0) LOGI("cannot enter %s", p);
        snprintf(p, sizeof p, "%s/log.txt", g_root);
        char prev[PLAT_MAX_PATH];                 /* the run before (a crash's log survives starting again) */
        snprintf(prev, sizeof prev, "%s/log.prev.txt", g_root);
        rename(p, prev);
        plat_log_init(p);
    }
    set_language(app);
    register_natives();
    display_modes_log();
    {   /* <files>/config/env.txt: KEY=VALUE lines (# comments) set before the game starts, the Android stand-in for the
         * environment variables of the Windows build (SMO_SHOT, SMO_VK_DEBUG, ...: see the runtime's sources) */
        char p[PLAT_MAX_PATH]; static char line[16384];      /* 0.4.2: 512 before cut a long SMO_SEQ (tests/cap_kingdom.seq is 2 KB) */
        snprintf(p, sizeof p, "%s/config/env.txt", g_root);
        FILE *f = fopen(p, "r");
        while (f && fgets(line, sizeof line, f)) {
            char *e = line + strlen(line);
            while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
            char *eq = strchr(line, '=');
            if (line[0] == '#' || !eq || eq == line) continue;
            *eq = 0;
            setenv(line, eq + 1, 1);
            printf("smo: env.txt: %s=%s\n", line, eq + 1);
        }
        if (f) fclose(f);
    }
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    printf("smo: Android host, data folder %s\n", g_root);
    { long t, a; android_meminfo(&t, &a); printf("android: the phone has %ld MB of RAM, %ld MB available at the start\n", t >> 10, a >> 10); }
    int ok = need_file("dump/base/elf/main.elf") & need_file("dump/base/elf/sdk.elf") & need_file("dump/base/exefs/main.npdm") & need_file("romfs");
    if (ok) {
        pthread_attr_t a;
        pthread_t t;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 64 << 20);
        pthread_create(&t, &a, game_thread, NULL);
        pthread_attr_destroy(&a);
    } else printf("smo: the game's files are not in %s: see BUILDING.md (tools/android/push_data.py)\n", g_root);
    for (;;) {
        int events, id;
        struct android_poll_source *src;
        while ((id = ALooper_pollOnce(g_rate_check_at ? 500 : -1, NULL, &events, (void **)&src)) >= 0) {
            if (src) src->process(app, src);
            if (id == LOOPER_ID_USER) sensors_read();
            if (app->destroyRequested) { printf("smo: the activity is destroyed\n"); fflush(stdout); _exit(0); }
            if (g_rate_req != g_rate_done || g_rate_check_at) rate_poll();
        }
        rate_poll();                                  /* woken (android_window_rate) or the check's time */
    }
}
