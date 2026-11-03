"""Copy the player's own game files from build/ to the device: /sdcard/Android/data/com.mobileodyssey.game/files.

    python tools/android/push_data.py [--no-romfs] [-s <serial>]

What goes over (runtime/android/android_main.c says where it looks): build/romfs, build/manual (if there),
build/dump/base/elf/*.elf and build/dump/base/exefs/main.npdm. `adb push --sync` sends only files newer than the
device's copy, so a second run is quick. The app is started once first when its folder is missing: Android makes the
app's external files folder for the app (getExternalFilesDir), and a folder made by adb would not belong to it.
Nothing here leaves the user's machine except to their own device.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import android_env as A  # noqa: E402

PKG = 'com.mobileodyssey.game'
DEST = '/sdcard/Android/data/%s/files' % PKG


def adb(*args, check=True):
    cmd = [os.path.join(A.sdk(), 'platform-tools', 'adb.exe' if os.name == 'nt' else 'adb')]
    if '-s' in sys.argv:
        cmd += ['-s', sys.argv[sys.argv.index('-s') + 1]]
    r = subprocess.run(cmd + list(args), capture_output=True, text=True)
    if check and r.returncode:
        raise SystemExit('adb %s failed:\n%s' % (' '.join(args), (r.stdout + r.stderr)[-3000:]))
    return r.stdout.strip()


def exists(path):
    return adb('shell', 'test -e "%s" && echo y' % path, check=False) == 'y'


def push(src, dst):
    t0 = time.time()
    out = adb('push', '--sync', src, dst)
    print('%s -> %s: %s (%.0fs)' % (os.path.relpath(src, A.REPO), dst, out.splitlines()[-1] if out else 'done', time.time() - t0))


def main():
    if PKG not in adb('shell', 'pm', 'list', 'packages', PKG):
        raise SystemExit('%s is not installed: python tools/android/build_apk.py --abi <abi> --install' % PKG)
    if not exists(DEST):
        adb('shell', 'am', 'start', '-n', '%s/.GameActivity' % PKG)
        for _ in range(50):
            if exists(DEST):
                break
            time.sleep(0.2)
        adb('shell', 'am', 'force-stop', PKG)
        if not exists(DEST):
            raise SystemExit('the app did not make %s' % DEST)
    b = A.T.build
    adb('shell', 'mkdir', '-p', DEST + '/dump/base/elf', DEST + '/dump/base/exefs')
    for n in ('main.elf', 'sdk.elf', 'subsdk0.elf', 'rtld.elf'):
        if os.path.exists(b('dump', 'base', 'elf', n)):
            push(b('dump', 'base', 'elf', n), DEST + '/dump/base/elf/' + n)
    push(b('dump', 'base', 'exefs', 'main.npdm'), DEST + '/dump/base/exefs/main.npdm')
    for d in ('manual', 'sysdata'):                     # sysdata/fonts: tools/extract/fw_fonts.py from the user's firmware
        if os.path.isdir(b(d)):
            push(b(d), DEST + '/')
    if '--no-romfs' not in sys.argv:
        push(b('romfs'), DEST + '/')
    print(adb('shell', 'du', '-sh', DEST))
    return 0


if __name__ == '__main__':
    sys.exit(main())
