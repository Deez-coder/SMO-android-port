"""Compile the lifted main (build/lift/main/*.c) for Android with the NDK's clang.

    python tools/android/build_game.py [jobs] [--abi arm64-v8a|x86_64] [--force]

The bounded-time rules (optnone for very long functions, retries, -O0 last resort) are tools/lifter/build_main.py's;
only the compiler, the target and the object folder differ. Objects go to build/android/<abi>/obj/main.
Environment: ANDROID_NDK (default toolchain/sdk/ndk/28.2.13676358 beside the repository), RECOMP_OPT (-O2),
RECOMP_OPTNONE_LINES, RECOMP_TIME_LIMIT as for build_main.py.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'lifter'))
import android_env as A  # noqa: E402
import build_main as B  # noqa: E402


def main():
    abi = A.abi_from_args(sys.argv)
    B.CLANG = A.clang()
    B.OPT = os.environ.get('RECOMP_OPT', '-O2')
    B.OBJ = A.build_dir(abi, 'obj', 'main')
    B.BASE_FLAGS = A.target_flags(abi) + ['-fPIC', '-ffunction-sections'] + B.BASE_FLAGS
    sys.argv = [a for i, a in enumerate(sys.argv) if a != '--abi' and (i == 0 or sys.argv[i - 1] != '--abi')]
    B.main()


if __name__ == '__main__':
    main()
