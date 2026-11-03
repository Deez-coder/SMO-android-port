"""Build shaderc (the GLSL -> SPIR-V compiler the Vulkan renderer needs at run time) from the NDK's own sources.

    python tools/android/build_shaderc.py [--out <folder without spaces>]

ndk-build with the NDK's sources/third_party/shaderc/Android.mk, both ABIs, C++ STL static. The folder must not contain
spaces (the SPIRV-Tools grammar generators of the NDK break on them); the default is ~/smo-shaderc. The libraries land in
<out>/obj/local/<abi>/*.a; tools/android/build_runtime.py links them (SHADERC_LIBS overrides where it looks).
The NDK's 'libshaderc_combined' target fails on Windows (its llvm-ar script), so the component libraries are built.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import android_env as A  # noqa: E402

TARGETS = ['shaderc', 'shaderc_util', 'glslang', 'SPIRV-Tools', 'SPIRV-Tools-opt']         # ndk-build module names


def main():
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else os.path.join(os.path.expanduser('~'), 'smo-shaderc')
    if ' ' in out:
        raise SystemExit('%s has a space: choose another --out' % out)
    os.makedirs(out, exist_ok=True)
    ndk = A.ndk()
    if ' ' in ndk:
        raise SystemExit('the NDK path %s has a space: set ANDROID_NDK to the same folder through a path without one (a junction)' % ndk)
    build = os.path.join(ndk, 'ndk-build.cmd' if os.name == 'nt' else 'ndk-build')
    mk = os.path.join(ndk, 'sources', 'third_party', 'shaderc', 'Android.mk')
    cmd = [build, 'NDK_PROJECT_PATH=.', 'APP_BUILD_SCRIPT=' + mk, 'APP_STL=c++_static', 'APP_ABI=arm64-v8a,x86_64',
           'APP_PLATFORM=android-%d' % A.API, 'NDK_LIBS_OUT=libs', '-j%d' % (os.cpu_count() or 8),
           'APP_CFLAGS=' + ' '.join(A.prefix_maps()), 'APP_CPPFLAGS=' + ' '.join(A.prefix_maps())] + \
        (['-B'] if '--force' in sys.argv else []) + TARGETS          # --force: everything again (after a flag change)
    r = subprocess.run(cmd, cwd=out)
    for abi in A.ABIS:
        d = os.path.join(out, 'obj', 'local', abi)
        print(abi, sorted(n for n in os.listdir(d) if n.endswith('.a')) if os.path.isdir(d) else 'NOT BUILT')
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
