"""Where the Android tools are and how the Android builds are laid out (shared by tools/android/*.py)."""
import glob
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import target as T  # noqa: E402

API = 30                                  # minSdk: Vulkan 1.1+ loader, AAudio, MAP_FIXED_NOREPLACE in practice
ABIS = {'arm64-v8a': 'aarch64-linux-android', 'x86_64': 'x86_64-linux-android'}


def sdk():
    for p in (os.environ.get('ANDROID_HOME'), os.environ.get('ANDROID_SDK_ROOT'),
              os.path.join(os.path.dirname(REPO), 'toolchain', 'sdk')):
        if p and os.path.isdir(p):
            return p
    raise SystemExit('Android SDK not found: set ANDROID_HOME')


def ndk():
    p = os.environ.get('ANDROID_NDK')
    if p and os.path.isdir(p):
        return p
    found = sorted(glob.glob(os.path.join(sdk(), 'ndk', '*')))
    if not found:
        raise SystemExit('Android NDK not found under %s/ndk: set ANDROID_NDK' % sdk())
    return found[-1]


def llvm_bin(tool):
    host = 'windows-x86_64' if os.name == 'nt' else ('darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64')
    exe = '.exe' if os.name == 'nt' else ''
    return os.path.join(ndk(), 'toolchains', 'llvm', 'prebuilt', host, 'bin', tool + exe)


def clang():
    return llvm_bin('clang')


def abi_from_args(argv):
    abi = argv[argv.index('--abi') + 1] if '--abi' in argv else 'arm64-v8a'
    if abi not in ABIS:
        raise SystemExit('unknown ABI %s (one of %s)' % (abi, ', '.join(ABIS)))
    return abi


def target_flags(abi):
    # arm64: ARMv8.2-A (every arm64 phone core since about 2018: Cortex-A55/A75 on) - atomics as LSE instructions instead
    # of calls to the outline helpers (__aarch64_cas4_acq ..., 3-10% of the game's threads on a Galaxy S24 Ultra), tuned
    # for the Cortex-A720 cores the game's threads run on
    # SMO_ARCH=armv8.0: the build for phones whose cores are ARMv8.0 (Cortex-A53 / A73: Snapdragon 680 and older, issue 9), whose
    # atomics are the outline helpers (LSE instructions where the processor has them); its folders and APK have their own names
    if abi == 'arm64-v8a' and arch_v80():
        extra = ['-march=armv8-a', '-moutline-atomics', '-mtune=cortex-a53']
    else:
        extra = ['-march=armv8.2-a', '-mtune=cortex-a720'] if abi == 'arm64-v8a' else []
    return ['--target=%s%d' % (ABIS[abi], API)] + extra + prefix_maps()


def prefix_maps():
    """Source paths written into the binaries (__FILE__ in asserts, debug info) start at '~' instead of the builder's
    home folder, so an APK does not carry its builder's user name."""
    home = os.path.expanduser('~')
    forms = {home, home.replace(chr(92), '/'), home.replace('/', chr(92))}
    forms |= {f[0].lower() + f[1:] for f in list(forms)}
    return ['-ffile-prefix-map=%s=~' % f for f in sorted(forms)]


def arch_v80():
    return os.environ.get('SMO_ARCH', '').lower() in ('armv8.0', 'armv8-a', 'v80')


def build_dir(abi, *parts):
    return T.build('android', abi + ('-v80' if abi == 'arm64-v8a' and arch_v80() else ''), *parts)
