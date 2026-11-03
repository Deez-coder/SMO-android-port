# Building the Android port

Super Mario Odyssey 1.0.0, statically recompiled (smo-recomp's lifter and runtime) and built as a native Android app.
The APK holds no game data. You supply your own dump and keys, and the game files are copied from your PC to your
own device.

Everything below was run on Windows 11 with Git Bash. Paths with spaces break `sdkmanager.bat` and `ndk-build`, so the
project is reached through a junction without spaces (here `C:\smo`, holding `smo-android` and `toolchain`).

## Toolchain (`../toolchain`, not in git)

| Tool | Version |
|---|---|
| JDK | Temurin 17.0.20.1 |
| Android SDK | cmdline-tools 15859902, build-tools 35.0.0, platform android-35, platform-tools, emulator |
| NDK | 28.2.13676358 (clang 19) |
| Python | venv `.venv` with pycryptodome, lz4, zstandard, capstone, unicorn, numpy, Pillow, PyYAML |

`tools/android/android_env.py` finds the SDK in `ANDROID_HOME`, `ANDROID_SDK_ROOT` or `../toolchain/sdk`, and the NDK
in `ANDROID_NDK` or `<sdk>/ndk/*`.

## 1. Dump → C (once, about 1 hour)

```bash
cd /c/smo/smo-android
K="path/to/your/prod.keys"; D="path/to/your/dump"     # your own console's keys and your own dump of the game
.venv/Scripts/python scripts/setup.py --dump "$D" --keys "$K" --only extract     # build/dump/base/{elf,exefs}
.venv/Scripts/python scripts/setup.py --dump "$D" --keys "$K" --only romfs       # build/romfs (5.1 GB)
.venv/Scripts/python scripts/setup.py --dump "$D" --keys "$K" --only manual      # build/manual (optional)
.venv/Scripts/python scripts/setup.py --only functions
export SMO_LAYOUT=android        # guest at 0x40_0000_0000: Android's own mappings sit at 0x55.. - 0x7f..
.venv/Scripts/python tools/lifter/recomp.py full                                  # build/android/lift/main
.venv/Scripts/python tools/lifter/gen_tables.py && .venv/Scripts/python tools/lifter/gen_sdktest.py
.venv/Scripts/python tools/lifter/sdklift.py gen                                  # build/android/gen
```

Optional, from your own console firmware (the shared system fonts):
`python tools/extract/fw_fonts.py --fw <folder of .nca> --keys "$K"` → `build/sysdata/fonts`. If they're missing,
Android uses the device's Roboto / Noto CJK instead, and the log says `SUBSTITUTE`.

## 2. C → libsmo.so → APK

```bash
export ANDROID_NDK='C:\smo\toolchain\sdk\ndk\28.2.13676358'      # a path without spaces
.venv/Scripts/python tools/android/build_shaderc.py                  # once: shaderc into ~/smo-shaderc
.venv/Scripts/python tools/android/build_game.py 6 --abi arm64-v8a   # 257 lifted objects, ~40 min
.venv/Scripts/python tools/android/build_runtime.py --abi arm64-v8a  # runtime + link: build/android/arm64-v8a/lib/libsmo.so
.venv/Scripts/python tools/android/build_apk.py --abi arm64-v8a      # build/android/smo-arm64-v8a.apk
```

Use `--abi x86_64` for the emulator. `build_apk.py` takes several `--abi` flags to build one APK for both, and
`--install` installs it on the attached device. The debug key is created once in `build/android/debug.keystore`.
`--release` builds `build/android/odyssey-<version>.apk` instead: not debuggable, signed with a key made once in
`build/android/release.keystore` (password in `release.keystore.txt`; keep both, since updates need the same key).
`SMO_ICON=<png>` puts your own picture on the launcher icon instead of the default one (`tools/android/make_icon.py`).

## 3. Game files to the device

```bash
.venv/Scripts/python tools/android/build_apk.py --abi arm64-v8a --install
.venv/Scripts/python tools/android/push_data.py      # build/romfs, manual, sysdata, dump/base/{elf,exefs}/...
```

The files go to `/sdcard/Android/data/com.mobileodyssey.game/files/`. `push_data.py` starts the app once first, so Android
creates that folder and gives it to the app. Later runs only send files that changed.

## Optional: a custom Vulkan driver (Mesa Turnip on Adreno)

On Snapdragon phones, Mesa's Turnip driver runs the game noticeably faster than Qualcomm's: on a Galaxy S24 Ultra in
Cap Kingdom, 33–35 fps against 24–26 fps. It is loaded through libadrenotools, the loader Eden uses.

1. Build libadrenotools once, from source (BSD-2-Clause, github.com/bylaws/libadrenotools), in
   `../external/libadrenotools`:
   ```bash
   C=toolchain/sdk/cmake/3.31.6/bin; N=toolchain/sdk/ndk/28.2.13676358
   $C/cmake -S external/libadrenotools -B external/libadrenotools/build-android -G Ninja -DCMAKE_MAKE_PROGRAM=$C/ninja \
     -DCMAKE_TOOLCHAIN_FILE=$N/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 \
     -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
   $C/cmake --build external/libadrenotools/build-android
   ```
   `build_runtime.py` then links it, and `build_apk.py` packages its hook libraries.
2. Put an AdrenoTools driver package, unzipped (`meta.json` plus the `.so`), in `files/driver/` on the phone. The one
   tested is K11MCH1's `Turnip_v26.0.0_R8.zip` from github.com/K11MCH1/AdrenoToolsDrivers, SHA-256
   `e634db0f929e2205e95511c769071817d0390180ec72c8e690bc76375e813715`. A driver is code that runs inside the game, so
   use only a build you trust.
   ```bash
   adb push meta.json /sdcard/Android/data/com.mobileodyssey.game/files/driver/meta.json
   adb push vulkan.ad07xx.so /sdcard/Android/data/com.mobileodyssey.game/files/driver/vulkan.ad07xx.so
   ```
3. The log says `custom Vulkan driver ... loaded` and names the device "Turnip Adreno (TM) 750". `SMO_DRIVER=system`
   in `config/env.txt` switches back to Qualcomm's driver. Removing `files/driver` does the same.

## Running and debugging

- The log goes to `files/log.txt` and to logcat (tag `smo`).
- Pull the log: `adb pull /sdcard/Android/data/com.mobileodyssey.game/files/log.txt`.
- `files/config/env.txt` holds `KEY=VALUE` lines that become environment variables before the game starts. They are
  the runtime's debug switches, the same ones the Windows build reads. Examples: `SMO_SHOT=300` with
  `SMO_SHOT_DIR=../shots` (frame pictures), `SMO_VK_PASSSHOT=<frame>` (every pass of a frame), `SMO_FSLOG=1` (every
  file open), `SMO_VK_GLSLDUMP=../glsl` (translated shaders).
- `SMO_VK_NO_BC=1` makes the renderer decode BC textures on the CPU, as it does on GPUs without BC. That lets you test
  this path on a GPU that has BC.
- `tools/android/run_shots.sh <out> <seconds> KEY=VALUE...` runs the app with those settings and pulls the frame
  pictures and the log, for A/B comparisons.
- `files/config/graphics.ini` and `files/config/controls.ini` are created with their defaults on the first start and
  are re-read when they change.
- The first start compiles about 10,000 shader variants on the worker threads, which takes several minutes on the
  emulator. The results are cached in `files/cache/vk_dz` (or `cache/vk` on GPUs with `VK_EXT_depth_clip_control`).

## Emulator used for testing

AVD `smo`: `system-images;android-35;google_apis;x86_64`, 8 GB RAM, 24 GB data, `-gpu host`. The host GPU here is an
RTX 4060, and the emulator offers Vulkan 1.3 through gfxstream. It lacks `VK_EXT_vertex_input_dynamic_state`,
`VK_EXT_extended_dynamic_state3`, `VK_EXT_depth_clip_control` and `VK_EXT_graphics_pipeline_library`, which makes it
a good stand-in for phone GPUs.

```bash
export ANDROID_SDK_ROOT='C:\smo\toolchain\sdk' ANDROID_AVD_HOME='C:\smo\toolchain\avd'
toolchain/sdk/emulator/emulator.exe -avd smo -gpu host -no-snapshot -no-audio -no-boot-anim
```

In Git Bash, set `MSYS_NO_PATHCONV=1` before passing `/sdcard/...` paths to adb; otherwise Git Bash rewrites them.

## The Windows build

This branch changes code shared with the Windows build: the platform layer (`runtime/plat.h`) and the pipeline key.
The Windows build has **not** been rebuilt or tested since these changes.
