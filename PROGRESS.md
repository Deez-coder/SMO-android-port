# SMO Android port: progress log

Build steps: [smo-android/BUILDING.md](smo-android/BUILDING.md).

## Approach

The base is static recompilation with [smo-recomp](https://github.com/Coz-0/smo-recomp) (GPL-3.0), at commit
`1ca3c00`, the last commit that supports game version 1.0.0:

- The game's AArch64 code is translated ahead of time to C and compiled with the Android NDK. Nothing is emulated or
  interpreted at run time, and no Windows compatibility layer is involved.
- Console services, NVN→Vulkan graphics, Maxwell→GLSL→SPIR-V shaders, audio and input come from smo-recomp's native
  runtime, ported here to Android.
- [OdysseyDecomp](https://github.com/MonsterDruide1/OdysseyDecomp) is about 15.8% matched and serves as reference only.
- [SuperMarioWonderRecomp](https://github.com/hesanaltdoesgithub/SuperMarioWonderRecomp) was the reference for the
  Android host: NativeActivity, fixed guest mapping, Gradle-free APK.

The dump is base 1.0.0. Its `main` NSO hash matches the known retail hash, so the keys and the dump are good.

## Status (2026-10-28)

**Tested on the x86_64 emulator** (Android 35, gfxstream Vulkan 1.3 on an RTX 4060):

- The APK installs. The app finds the pushed game files and starts the recompiled game.
- The game boots through `nninitStartup`, 183 static constructors and `nnMain`.
- Fonts load; files load from the RomFS.
- AAudio starts (48 kHz stereo, 120 ms buffer).
- The Vulkan device comes up; shaders are translated and compiled through shaderc on the device.
- Seen on screen: the first-start menu ("Start / Start in Assist Mode") and the boot screen "Try playing with detached
  Joy-Con controllers". Orientation and 16:9 shape are correct, at 59–60 fps once the shader cache is built.
- The crash and missing-file paths print useful reports.

**Not tested yet:**

- arm64 on a real phone. The arm64 APK builds and its library loads under the emulator's ARM translation, but the
  game has not been run on arm64.
- Input of any kind (touch, controller, keyboard). The adb key press did not move the boot screen on, which is also
  what a loading screen would do.
- Gameplay past the boot screen; audio output (the emulator ran with `-no-audio`); performance on a phone.

## Done

- Toolchain (JDK, SDK, NDK 28, emulator), all checksum-verified.
- Dump decrypted, RomFS extracted.
- Full lift: 3.13M instructions, 0 untranslated; 1,336 SDK functions.
- `SMO_LAYOUT=android` memory layout: guest at 0x40_0000_0000 instead of 0x71_0000_0000, which Android already uses.
- Platform layer `runtime/plat.h` plus `plat_posix.c`; the whole runtime ported off Win32:
  - threads and locks, memory (memfd heap), files, time zones, sockets, crash reports.
- Android host (`runtime/android/`):
  - NativeActivity, Vulkan surface (handles losing and regaining the window), AAudio.
  - Gamepad, keyboard, touch, accelerometer and gyro input.
  - `controls.ini`; `env.txt` for debug settings.
- Renderer changes for GPUs without desktop-only extensions:
  - The vertex layout is part of the pipeline key when `VK_EXT_vertex_input_dynamic_state` is missing.
  - z is remapped in vertex shaders when `VK_EXT_depth_clip_control` is missing.
  - RGBA swapchain; identity pre-transform; picture fitted to its aspect ratio with bars.
- Build scripts in `tools/android/`:
  - `build_game`, `build_shaderc`, `build_runtime`: build the library.
  - `build_apk`: aapt2, zipalign, apksigner.
  - `push_data`: copies your game files to the device.

## On a real phone (Galaxy S24 Ultra, Snapdragon 8 Gen 3 / Adreno 750, Android 16), 2026-10-28

- **Runs.** Boot → Joy-Con notice → first-start menu → "Start" pressed through the touch controls → the opening
  cutscene (Bowser's airship) in full 3D.
- **Menus run at 60 fps.**
- **Shaders:**
  - First start, empty cache: the menu after about 2 min.
  - Second start: the menu after 30–45 s; 1.2 GB of shader cache.
  - The 30 s holds are gone.
- **The phone uses the faster renderer paths.** Adreno has dynamic vertex input, depth-clip control and BC textures.
- **Cutscene: 20 fps.** Frame = 50 ms, all on the render thread, one step after another:
  - replaying the game's ~580 draws: 26–33 ms;
  - waiting for the GPU: 12 ms;
  - present: 9–12 ms.
  - The GPU itself needs only about 12 ms per frame.
  - The render thread is about 50% busy and the game's main thread 14%: they wait on each other, not on the hardware.
  - Not the cause (tested): occlusion results waited for at present, pinning the game threads to three cores.
- **Cutscene speed-up, same day: 20 → 25–26 fps.**
  - The swapchain was being rebuilt every frame by a window-size check that didn't allow for rotation; present went
    from 12 ms to 0.8 ms.
  - The render thread now gets the Cortex-X4 core by default.
  - Repeated "set the same render targets" commands no longer end the render pass.
  - Remaining cost per frame (about 40 ms):
    - draw replay, about 28 ms: mostly Adreno driver calls for about 580 draws and 70 render passes;
    - GPU waits, about 12 ms: `OP_REPORT_VALUE` forced compute results back before writing the value.
  - **Fixed the same day: 25–26 → 33–35 fps.** Report values now wait in a queue until their compute results land,
    or until the render thread runs out of work.
    - Draw replay is about 14 ms per frame.
    - About 7 ms is the game itself waiting for its GPU work, at the render thread's idle point.
    - Audio silence dropped from about 1.4 s to 0.1–0.55 s per 2 s.
    - The picture is unchanged.
  - Diagnostics: `SMO_VK_WAITWHO=1`.
- **Gameplay reached, then crashes, 2026-10-28.**
  - Cap Kingdom loads and runs at 40–58 fps (50–275 draws per frame).
  - About 30 s in, the game opens its camera-tutorial video (`content:/MovieData/PlayGuideMovieRStick.mp4`). The
    game assumes videos always open: `MovieDecoderPlayer::OperateThreadFunction` calls the decoder without a check,
    so with no player it crashed.
  - **Fixed: video player on Android** (`runtime/android/android_movie.c`, AMediaExtractor + AMediaCodec, generated
    from `nnmovie_mf.c`'s protocol code). The tutorial clip (H.264 480×272) plays and loops in game.
  - **Open:** in that first gameplay scene, input reaches the game but Mario doesn't react. Confirmed in the game's
    own pad reads: A, B and both sticks. Menus react to the same path. It's unknown whether this moment allows
    movement at all; needs a check by hand on the phone.
  - Adreno 750 refuses two of Cap Kingdom's pipelines with VK_ERROR_UNKNOWN. Their draws are now skipped. The
    attachment formats are supported and the SPIR-V is valid; the cause is still open.
  - Earlier stops left no log line; `plat_exit` now drains the log first.
- **Gameplay works by touch.** Confirmed by the user on the phone: Mario moves and jumps.
- **On-screen controls and an fps counter** are drawn by the present pass.
- **Turnip driver (Mesa), 2026-10-28.** K11MCH1's v26.0.0 R8 is loaded through libadrenotools (built from source).
  - Same Cap Kingdom scene, about 1,000 draws per frame:
    - Qualcomm driver: 24–26 fps, replay 23.4 ms;
    - Turnip: 33–35 fps, replay 15.6 ms.
  - The stage-library path (graphics pipeline libraries) is unusable on a phone: it ran out of memory and stalled on
    precompiling. Android defaults to whole pipelines.
  - After an hour of tests the phone reported thermal status 2, and replay doubled from CPU throttling. Measurements
    must be taken on a cool phone.
  - **For 60 fps (16.7 ms):**
    - draw replay must roughly halve (spread over threads, or fewer driver calls per draw);
    - the ~6 ms of GPU waiting must overlap with other work.
  - **Findings from the 60 fps work (end of 2026-10-28):**
    - The background shader workers were predicting pipelines for every shader variant: 44,845 on Turnip, 77% of all
      CPU, the phone throttling. Fixed: `SMO_VK_CPREDICT`, off on Android.
    - **The GPU needs about 19.5 ms per frame** in Cap Kingdom with Turnip, so it alone exceeds 16.7 ms. The figure is
      the same at resolution scale 0.75, so the cost is per pass or per barrier, not per pixel. That points at:
      - render-pass handling on the tiled GPU (each of about 70 passes may go through binning);
      - full-pipeline barriers (`image_barrier` uses ALL_COMMANDS → ALL_COMMANDS for every layout change).
    - Load-clears (`LOAD_OP_CLEAR` for passes that start with a clear) are in, but gave no measurable change.
    - **Next:**
      1. Test `TU_DEBUG=sysmem` and `TU_DEBUG=gmem` (in `config/env.txt`) to see whether binning is the cost. A run
         was started but interrupted.
      2. Narrow the barriers to the stages that are actually involved.
      3. Multithreaded draw replay for the CPU side (replay about 15–19 ms).
    - **The phone heats fast:** status 2–3 after one or two runs. Measure only at status ≤ 1 (the scripts in
      `tools/android/` take a few minutes per run).
  - **Why fps sticks at 30 (later the same day):** with vsync, a frame that misses 16.7 ms waits for the next refresh,
    so the rate drops straight to 30. The game also runs per frame, so below 60 it plays in slow motion.
    - Per-pass GPU timing (`SMO_VK_PASSTIME=<frame>`), Turnip, Cap Kingdom: 131 passes, 16.4 ms of GPU work, and 8 ms
      of gaps where the GPU waits for the render thread to record. **The render thread (CPU) is the limit.**
    - Render-thread profile: copying the game's buffers into the stream ring is about 20% (about 15 MB per frame,
      re-copied every batch); then the command-list scan, pipeline lookup and sampler setup.
    - **Tried and removed:** keeping ring copies across batches for pools the game must flush (CPU_CACHED,
      CPU_NO_ACCESS). It saved only 0.4 of 14.8 MB per frame, because the per-frame data lives in CPU_UNCACHED pools.
      It also blacked out the 3D scene (stale copies). Not worth pursuing.
    - **Next candidates:** multithreaded replay or cheaper per-draw work on the render thread; real GPU timestamps for
      the game's dynamic resolution (the 3D image is 800x450, which causes the aliasing).
  - **Hidden GPU waits removed (later the same day, commit 100a264): Cap Kingdom 23-25 -> 28-32 fps.**
    - Turnip's `vkGetFenceStatus` is a kernel wait. Polls that must not wait now read a VkEvent each command buffer
      sets at its end.
    - The idle step no longer waits for compute results.
    - No wait for occlusion counts at the present. The counts are written to game memory one whole presented frame
      at a time (`SMO_VK_OCC_HOLD`); without that, light glows flickered (confirmed fixed by the user).
    - Conditional renders whose counts are not in yet use the previous frame's decision.
    - The look-ahead scan runs on its own thread.
    - Result: the render thread waits ~0.1 ms per frame for the GPU (was 7-12). Replay is 16-19 ms per frame; the GPU
      needs ~16.4 ms.
  - **Tried without an fps change (commit 0022e14):** ARMv8.2 target, `Ctx *restrict`, a lock-free indirect-call
    cache, NVN lookup locks, `ANativeWindow_setFrameRate(60)`. All kept; all correct.
  - **Tried and dropped:** keeping ring copies across batches (only 0.4 of 15 MB per frame was eligible; it broke
    the picture); async present (`SMO_PRESENT_ASYNC=1`, no clear gain).
  - **The limit now: the game's own threads.** No thread is near full load (render 55%, main 45%, workers 7-21%).
    The main thread spends ~42% of its time in the game's own nn::os timed-event waits (for its worker threads).
    Frames take ~35 ms as a chain of threads waiting on each other.
    - **Next:** record which guest thread waits for which per frame (nn::os events, mutexes, wake-up latency) and
      fix what lengthens the chain: worker priorities, core placement, slow wake-ups.
    - Also: frames below 60 make the game run in slow motion, which is why 50-60 fps still feels laggy; frame pacing
      uses display.c's own 60 Hz clock, not the display's refresh.
  - **2026-10-29:**
    - The phone's own Qualcomm driver is now nearly as fast as Turnip in the same spot (25-30 vs 28-31 fps), so
      phones without Turnip (MediaTek, Exynos: Mali) are on a usable path. Turnip stays optional (only loaded when
      its driver is in `files/driver`).
    - Android performance hints (`android_perf.c`, ADPF, Android 13+) for the render thread and the game's threads:
      no measured change in Cap Kingdom in standard game mode (off 32-35 fps, on 31.5-33). Kept: harmless, and
      other phones' governors may respond to it.
    - Samsung's performance game mode gave no measured change in that spot either; the user saw 40-60 fps in it
      while looking away from the town (far fewer draws).
    - Controllers: tested with injected gamepad keys at the title menu (selection moves, touch controls hide). Device
      id -1 had been taken for the back gesture; fixed. Sticks and triggers need a real controller to test.
    - Touch: a right camera stick (`camera_touch = stick`, the default; `drag` for the old behaviour).
    - `phone_play.sh` presses A twice: a single press was sometimes taken before the title menu accepted input.
    - Touch controls on/off: a ring at the top centre (saved as `touch = off | auto` in controls.ini). Tested.
    - **Wait map (`SMO_WAITMAP=1`, os_threads.c):** per frame at ~32 fps the game's main thread (shown as "host")
      waits ~5 ms for the view-update worker and ~3-4 ms for the presentation thread; wake-ups are fast (50-350 us);
      the workers are idle most of the time. The main thread's own work (~20 ms a frame) is the critical path.
    - Main thread on the Cortex-X4 and the render thread on A720s (`SMO_OS_HOST_CPUS=98 SMO_RENDER_CPUS=64`): worse
      (replay 23.5 ms, 27.6-28.7 fps). The default stays.
    - The sampler list sort (`smo_fast.c`) now runs in host code on Android too: libsmo.so is linked with
      `--wrap=f_400072e61c`. `SMO_FAST_CHECK=1` in gameplay: every sort equal to the game's. Frame rate unchanged in
      Cap Kingdom (29.4-30.6 fps, within run-to-run spread).
    - Then: the sort as a stable insertion sort (the same order; `SMO_FAST_CHECK=1` in gameplay: all equal), the
      NVN object and label tables at 65,536 chains (they were 5% and 3% of the main thread). Still ~30 fps.
    - **Why main-thread savings do not show:** the median frame is ~32-33 ms, two refreshes. Free pacing
      (`SMO_PACE=free`, by time instead of refreshes; opt-in) changed nothing (29.4-29.8 fps), so the quantising wait
      is in the game's own frame loop: its presentation thread waits for the vsync event, and the main thread waits
      for that thread (wait map: ~3-4 ms a frame).
    - Correction: the ~32 ms frames are real work, not refresh rounding (free pacing gives a smooth 25-45 ms spread).
      The game's own translated code is 64% of the main thread: next big lever is the translator keeping registers
      in locals (several sessions).
- **2026-11-02: 30 fps full-speed mode, launcher, mods.**
  - `frame_rate = 30` (graphics.ini, launcher Graphics page): presents every second refresh, and
    sead::GameFrameworkNx::procCalc_ runs twice per frame (`smo_framerate.c`, `--wrap=f_4000731fdc`). Tested:
    steady 30.0 fps, two logic steps per frame; audio silence 20-140 ms per 2 s against 135-535 ms in normal mode
    at ~30 fps, so the game runs near full speed. Picture correct.
  - Launcher (`android/java/io/github/smoport/LauncherActivity.java`, javac + d8 in build_apk.py): Game page (status,
    Play, import game files from a folder), Graphics, Controls, Advanced (driver .zip install/remove, phone's own
    driver), Mods (install .zip, on/off). Tested on the phone: shows the installed game and Turnip driver; Play
    starts the game. Settings write graphics.ini / controls.ini (new keys: fps_counter, driver).
  - Mods: `mods/<name>/romfs/...` (or `mods/<name>/...`) replace the game's files, alphabetical order, `mods.ini`
    switches them off (nnfs.c view_path). Files only a mod adds are not in directory listings. Not tested with a
    real mod yet.
  - **Registers in locals (`tools/lifter/regcache.py`), tried and not kept:** 69,470 of 74,183 functions changed,
    compiled and played correctly, but no measurable fps change in matched views (old 32.3-34.1, new 33.3-33.7 fps at
    1,195 draws; 42 vs 42 at ~834). The reloads it removes hit the L1 cache. The game code was reverted; the pass is
    kept for later.
  - **Test spread:** the camera after loading differs from run to run (834 vs 1,195 draws in Cascade Kingdom,
    noticed by the user), so only runs with the same draws per frame are compared.
  - In the ~834-draw view: GPU 10.5 ms, render thread replay 10.8 ms, frame 23.6 ms (42 fps): the game's own
    threads set the pace.
  - `anti_aliasing = fxaa` (graphics.ini, launcher): FXAA on the final picture in the present pass. Tested, works.
  - The 3D at 800x450 is not caused by our report timestamps: with timestamps that made every frame look quick, the
    passes stayed 800x450 (that option was removed again).
  - **Why resolution_scale never did anything in SMO:** the scale applied only to textures of 1920x1080 >> k (the
    PC project's first game); SMO's are 1600x900 / 800x450 (docked) and 1280x720 (handheld). Fixed (rs_screen_class)
    and the limit raised to 2.0 (an emulator-style resolution multiplier). At 1.5 the game started and the scaled
    targets were made, no errors; not yet seen in gameplay (the phone locked while unattended).
  - The game's own cap (al::GraphicsQualityInfo::calcRenderResolution [0xa58394], wrapped: smo_quality.c,
    `dynamic_resolution = on | off`) gives the full 1600x900 asked for in Cascade Kingdom: the 800x450 3D is the
    game's own choice there, not a quality drop. Kept as an option, not in the launcher.
  - Tested at 1.5 in gameplay: part of the picture is drawn unscaled over the scaled rest (two overlapping
    copies of the scene; seen by the user too). SMO's sizes are now behind a development switch
    (`SMO_VK_SCALE_SMO=1`) and the launcher's slider is back at 25-100% (it does nothing in SMO yet) until the
    scaled path is right for SMO: next, find which pass or copy keeps the game's coordinates.
  - **Fixed (2026-11-02):** the doubled picture came from SMO's 1600x900 HDR colour target, a 2D array texture
    (NVN target 4) of one layer: the scale took only targets 1 and 5, so it stayed at the game's size while its
    partners in the same passes were scaled (a diagnostic logs such mixed passes). Arrays of one layer are scaled
    now; compute images (NVN_TEXTURE_FLAGS_IMAGE) keep the game's size. No mixed passes left; picture correct.
  - **`resolution_scale = auto` (the default):** docked 1920x1080 (1.2 x the game's 1600x900, the console's output),
    handheld the game's 1280x720. Measured at 1,195 draws: 31.1-31.7 fps at 1.2, 31.0 at 1.5, against 32.3-34.1 at
    1.0. Launcher: presets Console / 1.5x / 2x and a 25-200% custom slider.
  - **Supersampling:** when the picture is larger than its place on the screen, the present pass averages an n x n
    grid (n up to 4) over each screen pixel's footprint instead of one bilinear sample. 2x: clearly smoother
    edges; 26.5-26.7 fps in 60 fps mode at ~1,145 draws (vs ~31 at 1.2); in the 30 fps mode 2x and 1.5x hold 30
    (1% lows 25.6 / 24.4) at ~838 draws.
  - **Sharpening** (`sharpening = 0..100`, launcher slider): contrast-adaptive, in the present pass after
    supersampling / FXAA. Tested at 50% with 2x in the 30 fps mode: 29.9 fps, crisp detail, no halos.
  - yuzu's source could not be read for this (GitHub answers 451 for the yuzu mirror); the approach follows the
    general emulator design (resolution multiplier, downscale filter, FXAA/SMAA, sharpening).
  - Not yet: importing straight from a dump (NSP/XCI + the player's keys) on the phone. The APK holds code
    translated from the player's own game: for that player only, not for sharing.
  - **Build note:** the game and runtime must be built with `SMO_LAYOUT=android`. Without it, the PC lift is
    compiled and the game stops at its first indirect call.
- **Audio choppy in the cutscene.** About 1.5 s of every 2 s is silent. The output never ran dry, so the game itself
  produces sound in step with its frame rate: no gaps at 60 fps in the menu, about 75% gaps at 20 fps. This should
  follow the frame rate fix.
- **The red/blue swap in some UI colours** (teal panel shown olive) happens on the phone too: a real bug.
- **Samsung's navigation bar stays visible.** Needs immersive mode (a small Java activity).

## Frame rate investigation (2026-11-03, S24 Ultra, release build)

Goal: find what keeps gameplay below 60 fps before changing anything. Test settings: 60 fps mode, scale 1.0, no
AA, no sharpening. Each run starts cool (thermal status 0-1), enters gameplay, resets the camera (L) so every run has
the same view (831-841 draws per frame, screenshots match), then measures 60 s.

| Run | What it changes | fps (last 60 s) | render-thread replay per frame |
| --- | --- | --- | --- |
| base | nothing | 40.0 (steady) | 11.3 ms |
| free | `SMO_PACE=free`: paced by time, not by display refresh | 42.5 rising to 51.7 | 10.5 ms |
| spin | `SMO_SPIN=2,5`: two busy threads from gameplay on, so the governor clocks up | 44.5, then 60.0 | 10 ms, then 5.2 ms |
| spinfree | both | 60.0 (58.3 at the end) | 3.2-4.1 ms |

**Finding: low CPU clocks are the main cause.** With the same work and view, the render thread's replay takes 11.3
ms at the clocks the governor picks and 3.2-5.2 ms at high clocks. When clocks are high, the frame holds 60 fps.
- Every thread of the game waits on another for part of each frame (main thread about 56% running, render thread
  about 55%), so none looks busy to the WALT governor. It runs the cores at about 55% of their maximum (render thread
  on cpu7 at ~1.5 of 3.34 GHz, main thread ~1.75 of 3.15 GHz). The ADPF hint session exists but doesn't raise them
  enough.
- In the spin run the jump to 60 came about 35 s after the spinners started: replay halved at that moment, so the
  render thread had reached a fast clock (probably moved onto a busy cluster, or cpu7 ramped up). spinfree reached
  60 within 5 s.
- Emulators like Eden keep their CPU threads busy (JIT, spin-waits), which incidentally keeps the clocks high. Here
  the threads sleep while they wait, so the governor drops them.

**Second factor: refresh pacing.** Free pacing alone gave +2.5 fps at first and +12 fps once clocks drifted up. The
main thread's sync waits on display refresh (`display_wait_refresh`, ~9.5% of its time) and on a synchronous round
trip to the render thread (`gl_run` in sync_wait, ~6%), so the render thread's replay sits on the critical path.

**Not the cause:** the GPU (waits 0.1-0.2 ms per frame; passes ~14 ms at scale 1.0 run in parallel), shader
compiling (none during the runs), heat (all runs started cool and the fast runs got faster, not slower).

**Caveat:** this view has ~835 draws. Busier views reach ~1,200 draws (31-34 fps base earlier); full clocks have not
been tested there yet.

**Fix plan, in order:**
1. Keep the clocks up during gameplay without spinners: lower the ADPF target or report work time instead of frame
   interval, `APerformanceHint_setPreferPowerEfficiency(false)`, a short spin before sleeping on the render-thread
   and sync waits (so the threads look busy and hand off faster), and keep the render thread on the big cores.
2. Take the render-thread replay off the main thread's critical path: make sync_wait / present asynchronous where
   the game doesn't need the result, and signal fences early.
3. Pace by time in gameplay (free pacing) with a frame-time cap instead of waiting for display refresh.
4. Faster replay itself: draw preparation is 6.4 ms of the 11.3 at low clocks (descriptor set work per draw).
5. Later: lifted-code overhead (context stores, lane-by-lane vector loads, NaN fixups).

**Clock fix, first round (2026-11-03, in progress).** All numbers on the phone's own driver (`SMO_DRIVER=system`)
from now on: players without a custom driver are the target (Mali included). Same view as above (~835 draws).
- Samsung's Adreno driver is much slower on the CPU than Turnip: 32.5 fps vs 40 (replay 15.5 vs 11.3 ms, draw
  preparation 9.8 vs 6.4 ms).
- Spinning before blocking (plat.h `PLAT_SPIN_WHILE`, off by default): `SMO_SPINWAIT_US` on every thread did nothing
  at 0.5 ms, little at 2 ms, and at 20 ms crowded out the work (menus at 30 fps). `SMO_SPINHOT_US=16000` only on the
  threads that set the frame rate (render thread, presenting thread, the thread that waits for the window sync):
  32.5 → 41.9 fps on the system driver (Turnip: 40 → 53.7). With free pacing as well: 43 rising to 49.6.
- `SMO_PERF_BOOST=1` (no power-efficiency preference, a workload rise every frame) with an 8 ms target: 41.2 vs 40.1.
  No real effect.
- The clusters still average 1.5-1.7 GHz even with three threads 100% busy, although the caps are ~2.9 GHz and the
  threads have no uclamp limit. WALT's `hispeed_freq` on the prime core is 1.59 GHz. Every spinning run sped up
  30-40 s into play, so the 60 s windows caught a ramp: longer runs are next.
- Render thread on the system driver: `vkResetCommandBuffer` cost ~1.2 ms a frame (the driver frees and reallocates
  its memory). Each slot now has its own command pool, reset whole (`vkResetCommandPool`). Built and installed, NOT
  yet run: the phone's battery ran out at the start of the test.
- Testing note: stopping a background test from the agent left the `sh` script running, so twice two scripts drove
  the phone at once. Some Turnip numbers above (53.7) may be affected; the system-driver numbers were rerun cleanly.

`SMO_SPIN` (runtime/android/android_perf.c) stays as a measuring aid: it does nothing unless set in env.txt, and
starts only when `<files>/config/spin_now` exists.

## Launcher: touch layout, controller buttons, save data (2026-11-03)

- **Touch layout editor** (LayoutEditorActivity.java): landscape, in the same full-screen window as the game. Drag any
  control (buttons, d-pad arrows, SHAKE, the two sticks' resting places), Smaller / Bigger (50-250%), Put back,
  Reset all, Save. Only moved controls get a line: `pos_<control> = x y size` in controls.ini. The runtime reads them
  (nnpad_android.c, also while the game runs). A touch away from the buttons goes to the stick on its side of a line
  0.10 to the camera's side of the middle between the two sticks (0.45 of the width for the layout as made, as before).
- **Controller buttons**: Controls page, one row per console button. Tap, press the controller button. The key moves
  from whichever button had it to the old key of this one (no button is left without a key). Written as
  `bind_<button> = <keycode>`; a button bound elsewhere no longer answers its own key. Sticks and analog triggers keep
  their axes.
- **Save data**: Game page. Export zips `userdata/save` (the committed save, files under `save/`). Import takes a zip
  with the files at the top or in one folder (an export, or a save folder from elsewhere), keeps the current save as
  `userdata/save-backup-<time>`, removes a pending `save.commit` and any `save.journal.*` / `save.work.*` of the old
  save, then puts the new one in place (the old one is put back if that fails). Refused while the game runs (libsmo.so
  is in the app's process): the dialog offers to close the app.
- **Checked on the emulator** (Android 15 x86_64, own AVD `odyssey_test`): editor drag / resize / save wrote the
  expected lines; returning to the launcher keeps the Controls page; rebinding with injected gamepad keys (swap
  included); export then import of test files, an emulator-style zip, backup and clean-up as above. The runtime side
  by a test program built from nnpad_android.c, run on the emulator: 13 of 13 checks (moved and resized controls,
  touches at the new places, bound keys, unbound keys keep their defaults).
- **Not yet checked on a phone**: the editor's places against the game's own drawing, a real controller, a real save.
- **prod.keys on its own button** (Game page, step 1): the file is checked for `header_key`, then kept as
  `prod.keys` in the app's internal files folder (only this app can read it), shown as "imported", with a Remove
  button. Step 2, Import from NSP / XCI, reads the keys from there and drops the parsed copy when the import ends; it
  asks for step 1 first if there are no keys. `android:allowBackup="false"` keeps the keys (and the game files) out
  of Android's cloud backup. Checked on the emulator with a made-up keys file: a file without `header_key` is refused;
  the keys survive an app restart and a reinstall; the NSP button uses them (a non-game file fails in the importer);
  Remove deletes them. Not yet run with real keys and a real NSP on a phone.
- **Logs for reports**: the runtime keeps the run before as `log.prev.txt` (a crash's log survives starting again),
  and every 10 s of play writes a line like `perf: 55.3 fps, 1% low 19.7, worst frame 51.5 ms over 553 frames (60 fps
  mode, scale 0.75); CPU MHz cpu0 .., cpu4 ..; thermal none` (android_perf.c: frame gaps of 1 s or more are pauses
  and are left out; the 1% low is the average of the slowest 1% of frames; one clock per cpufreq policy; the thermal
  state from AThermal). With the GPU and driver lines already printed at start, a log from another phone says how
  the game ran there. Advanced page, **Export logs**: a .zip with info.txt (app and Android version, phone, chip, ABIs,
  custom driver), log.txt, log.prev.txt, the settings files and the app's own logcat (main, system, crash; Android
  gives an app only its own lines). No save, keys or game files. Checked: the perf line by a test program built from
  android_perf.c on the emulator (made-up frames: 1% low 19.7 for 1% of frames at 50 ms; a 1.5 s pause left out; the
  emulator has no CPU clocks, so they read 0), the export on the emulator (contents as listed, logcat only this app's).
  Not yet on a phone: the perf line in a real game, readable CPU clocks.

## Beta check on the phone (2026-11-03, S24 Ultra, release build 0.1.0)

Everything added since bf3d443 run on the phone, with the player's own settings (30 fps mode, resolution auto, FXAA,
sharpening 75):

- **Play**: boots, reaches gameplay, 30.0 fps. Turnip: 1% lows 22-27 in play. The phone's own driver: 30 fps, 1% lows
  about 25 (one window 16.9). The per-slot command pools (vkResetCommandPool) work on both drivers.
- **perf lines** read real cluster clocks (cpu0 / cpu2 / cpu5 / cpu7 policies) and the thermal state ("moderate":
  the phone was warm from charging). `scale` now prints "auto" for resolution_scale = auto.
- **Export logs**: info.txt (SM-S928B, QTI SM8650, Android 16, Turnip v26.0.0 R8), both logs with perf lines,
  settings, logcat of the app only. With Turnip, logcat holds two "Access denied finding property
  vendor.mesa.spirv.*" warnings per shader compile (the driver's own, harmless).
- **System bars (fixed)**: the theme hid only the status bar. With button navigation, the navigation bar sat over
  the right edge in landscape, over A, ZR and R, and the layout editor's window ended at it. New GameActivity.java
  (a NativeActivity subclass) and the editor hide the system bars (immersive, swipe to show) and lay out under the
  cutout, so both use the whole display. First version crashed the editor: Window.getInsetsController() is null
  before setContentView on this phone; getDecorView().getWindowInsetsController() works.
- **Layout editor**: A dragged to 0.750 0.358 was saved as such, drawn there in the game, and taps there took the game
  from the menu into play (taps at the old place did nothing).
- **Controller buttons**: ZR rebound with an injected gamepad R1: `bind_ZR = 103`, `bind_R = 105` (the swap).
- **Save**: export of the real save (2 MB) byte-identical to the save; import of that zip: identical files, the old
  save kept as `save-backup-<time>`, a stale `save.work.<pid>` removed; the game loads it into play.
- **prod.keys + NSP import** with the player's own keys and 5.6 GB NSP: about 15 s on the phone. Every file name and
  size equals the PC extraction (the PC also has dump/base/control.nacp and dump/version.json, not used); all program
  files and 199 sampled game files have the same MD5. The game plays on the imported files. The earlier pushed copy
  had 4 files fewer.

## Leaving the game, button letters (2026-11-03)

- **Exit**: a house button at the top of the touch controls (movable in the layout editor) and a controller key
  (Controls tab, "Exit the game (asks first)", Home / Guide by default, `bind_EXIT`) open a popup in the app's own style:
  Keep playing or Exit to the menu. The game is held while it is open (display_menu_hold). Exit waits for a save
  commit in progress (`fs_quiesce`, nnfs.c: the fs lock fs_Commit holds, then the commit lock the launcher's save
  import uses), then the activity finishes and android_main ends the process.
- The game now runs in a process of its own (`:game`), so leaving it ends only the game and the app's menu stays.
  The launcher finds the game by that process ("Close the game" in the save-import warning ends it).
- Button icons in the style of the console's own button glyphs, drawn by our own code (signed distances in the
  present-pass shader, no image): circled A B X Y, L R ZL ZR tabs, hollow + and -, arrow d-pad, a stick-press R3, L and R
  in the sticks' knobs, the house for Exit. A dark fill with a white outline and icon; white with a dark icon while
  pressed. On the S24 Ultra the title screen holds 30.0 fps with the controls drawn and with them off.
- Checked on the S24 Ultra: the house opens the popup over the title screen, the picture stands still while it is open
  and moves again after Keep playing, Exit to the menu brings the app back with only the `:game` process gone and the
  log shows the save wait; the controller Home key (`input gamepad keyevent BUTTON_MODE`) opens it too. Rebinding
  Exit on the emulator writes `bind_EXIT`. Touches during the black boot screen do nothing: the touch controls start
  when the game sets up its controllers.

## Start-up time: the pipeline cache was never written on Android (2026-11-03)

- Every phone log of the beta check showed the same thing: the ~4,400 pipelines listed in `pipes.bin` made again at
  every start in 85-128 s ("pipelines made ahead"), and the game's first frame held in `program_set` for that long, so
  the title screen came after 1.5-2 minutes. No log had "pipeline cache saved": the cache file (8 MB) was never
  written back. The exit path that writes it (vkd_shutdown) never runs on Android (the process ends with `_exit`), and
  the held loading screen that writes it belongs to the stage-library path (SMO_VK_GPL=2), which phones do not take.
- Now `nvn_vk_pcache_save` (nvn_vk_m4.c) writes it: when the game goes to the background (GameActivity.onStop, a
  thread of its own), when it is left with Exit (after the save wait), and once after a start whose pipelines made
  ahead took 10 s or more (some were not in the cache). Skipped when the driver's data is the size of the file.
- Measured on the S24 Ultra (the phone's own driver): the start that built the cache wrote it after its pipelines
  made ahead (32 MB, read out in 13 ms, written in 29 ms, no frame lost); Exit then found nothing new. The next start:
  pipelines made ahead in 2.6 s instead of 113 s, the game's frames from about 8 s after Play, the title screen at 18 s
  (was about 2 minutes).
- **Loading screen** (GameActivity.showLoading): the game's window stays empty until its renderer shows a frame, and
  Samsung's opening animation stopped half way on it. A window of its own (not focusable, not touchable) now covers it
  at once in the app's colours: the icon, "Preparing graphics... n%" while the pipelines are made ahead (polled from
  `nvn_vk_load_progress`), "Starting the game..." after, and a note on a slow first start. It fades out once the game's
  frames have reached the window for 0.6 s (`nvn_vk_window_presents`).
- The game's window keeps the screen on (FLAG_KEEP_SCREEN_ON): the phone locked itself during a 2-minute first start.

## Open problems, most important first

1. **BC textures on phones without BC (Mali, PowerVR): decoded on the CPU since 2026-10-28.**
   - Output formats: BC1–3/7 → RGBA8, BC4 → R8, BC5 → RG8, BC6H → RGBA16F.
   - Tested on the emulator with `SMO_VK_NO_BC=1` against its GPU's own BC decoding (`tools/android/run_shots.sh`):
     the boot and menu frames match within ±5 levels; the only other difference was an animation phase.
   - Formats covered by that test: BC1/BC3/BC7 sRGB and BC4/BC5 unorm and snorm. BC6H is not tested yet.
   - Costs:
     - Texture memory about doubles (Vulkan memory 464 → 912 MB on the menus).
     - Decoding runs on the render thread when a texture is first used, which can cause hitches.
   - Possible improvements: decode on worker threads during loading screens, and transcode to ASTC/ETC2 to save memory.
   - Adreno GPUs usually read BC themselves.
2. **Some UI colours red/blue swapped on the emulator:** the teal panel, the orange step numbers. Neither BGRA
   textures nor vertex formats are the cause. The suspect is component swizzles on image views in gfxstream. Check on
   a real phone first.
3. **Frame rate on the emulator is set by its display path, not the game.**
   - When the picture is shown the right way up, each present costs about 110 ms (7–9 fps). This holds whether the
     compositor rotates the picture or the new pre-rotated present pass does.
   - The only fast configuration (present about 1.3 ms, 55–60 fps) shows the picture rotated wrongly
     (`SMO_VK_ROTATE_COMPOSITOR=1` with the old blit).
   - Rendering itself takes about 5 ms per frame. Next step: measure on a real phone (S24 Ultra).
4. **First-start shader work.** About 10,000 variants are translated and compiled; on the emulator this takes several
   minutes, at about 7 fps. Options:
   - prepare the shaders on the PC (`--prepare-shaders`) and push them with the game files;
   - give the container scan a lower priority on phones;
   - offer `shader_wait = skip`.
5. Touch overlay drawing: input mapping exists (`nnpad_android.c`) but nothing is drawn yet (`android_touch_shapes` is
   ready for a blended pass).
6. A launcher / settings screen for `graphics.ini` and `controls.ini`. Rumble, movies (MediaCodec), the Help web view.
7. The firmware's shared fonts. The device's fonts stand in, so the Nintendo button-symbol glyphs are missing.
8. The Windows build has not been rebuilt since the shared runtime changes.

## Legal and data handling

- No game data, keys, or anything derived from the dump is committed (`build/` is gitignored).
- The APK holds no game data or keys, but its `libsmo.so` is the game's own code lifted from the dump, so an APK
  built this way is the builder's own copy, like their game files.
- Key values were never printed.
- The game files go from the user's PC only to the user's own device.
