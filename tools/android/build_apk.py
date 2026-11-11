"""Package the Android app: build/android/smo-<abis>.apk.

    python tools/android/build_apk.py [--abi arm64-v8a] [--abi x86_64] [--release] [--install]

aapt2 (manifest android/AndroidManifest.xml, the icon from tools/android/make_icon.py; SMO_ICON=<png>: your own picture on it) -> lib/<abi>/libsmo.so added (stripped) -> zipalign (16 KB pages for the
library) -> apksigner with a debug key made once in build/android/debug.keystore. No game data goes into the APK.
--release: not debuggable, signed with the release key in build/android/release.keystore (its password in
release.keystore.txt beside it; a copy and the key's SHA-256 in ../keys, beside smo-android; an update must be signed with the same
key, so the build stops rather than make a new one or sign with another: SMO_NEW_RELEASE_KEY=1 starts a new key) -> build/android/odyssey-<version>.apk.
--install: adb install -r on the one device or emulator attached.
"""
import os
import re
import secrets
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import android_env as A  # noqa: E402

BUILD_TOOLS = '35.0.0'
PLATFORM = 'android-35'
RELEASE_KEY_BACKUP = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'keys')   # beside smo-android, outside the source export


def tool(name):
    exe = {'aapt2': 'aapt2.exe', 'zipalign': 'zipalign.exe', 'apksigner': 'apksigner.bat', 'd8': 'd8.bat'}[name] if os.name == 'nt' else name
    return os.path.join(A.sdk(), 'build-tools', BUILD_TOOLS, exe)


def java_env():
    env = dict(os.environ)
    jdk = os.environ.get('JAVA_HOME') or next((os.path.join(os.path.dirname(A.sdk()), d) for d in os.listdir(os.path.dirname(A.sdk())) if d.startswith('jdk')), None)
    if jdk:
        env['JAVA_HOME'] = jdk
        env['PATH'] = os.path.join(jdk, 'bin') + os.pathsep + env.get('PATH', '')
    return env


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        raise SystemExit('%s failed:\n%s' % (os.path.basename(cmd[0]), (r.stdout + r.stderr)[-4000:]))
    return r.stdout


def java_dex(env):
    """android/java/**/*.java -> classes.dex (javac against android.jar, then d8). None when there is no Java."""
    src = [os.path.join(d, n) for d, _, ns in os.walk(os.path.join(A.REPO, 'android', 'java')) for n in ns if n.endswith('.java')]
    if not src:
        return None
    out = A.T.build('android', 'java')
    classes, dexdir = os.path.join(out, 'classes'), os.path.join(out, 'dex')
    for d in (classes, dexdir):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    jar = os.path.join(A.sdk(), 'platforms', PLATFORM, 'android.jar')
    run([os.path.join(env['JAVA_HOME'], 'bin', 'javac'), '-source', '11', '-target', '11', '-encoding', 'UTF-8', '-nowarn',
         '-classpath', jar, '-d', classes] + src, env=env)
    cls = [os.path.join(d, n) for d, _, ns in os.walk(classes) for n in ns if n.endswith('.class')]
    run([tool('d8'), '--release', '--min-api', str(A.API), '--lib', jar, '--output', dexdir] + cls, env=env)
    return os.path.join(dexdir, 'classes.dex')


def main():
    abis = [sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == '--abi'] or ['arm64-v8a']
    out_dir = A.T.build('android')
    os.makedirs(out_dir, exist_ok=True)
    env = java_env()
    release = '--release' in sys.argv
    base = os.path.join(out_dir, 'base.apk')
    manifest = os.path.join(A.REPO, 'android', 'AndroidManifest.xml')
    text = open(manifest, encoding='utf-8').read()
    version = re.search(r'android:versionName="([^"]+)"', text).group(1)
    if release:                                      # the same manifest without android:debuggable
        manifest = os.path.join(out_dir, 'AndroidManifest.release.xml')
        open(manifest, 'w', encoding='utf-8').write(re.sub(r'\s*android:debuggable="true"', '', text))
    res = os.path.join(out_dir, 'res')                # the launcher icon (tools/android/make_icon.py), compiled by aapt2
    run([sys.executable, os.path.join(A.REPO, 'tools', 'android', 'make_icon.py'), res])
    flat = os.path.join(out_dir, 'res.zip')
    run([tool('aapt2'), 'compile', '--dir', res, '-o', flat])
    run([tool('aapt2'), 'link', '-o', base, '-I', os.path.join(A.sdk(), 'platforms', PLATFORM, 'android.jar'), '-R', flat,
         '--manifest', manifest, '--min-sdk-version', str(A.API), '--target-sdk-version', '35'])
    dex = java_dex(env)                              # the launcher (android/java): javac, then d8
    with zipfile.ZipFile(base, 'a', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        if dex:
            z.write(dex, 'classes.dex')
            print('classes.dex: the launcher (%d KB)' % (os.path.getsize(dex) // 1024))
        lic = os.path.join(A.REPO, 'docs', 'licenses')                # the notices the licences of included code ask a binary to carry
        for n in sorted(os.listdir(lic)) if os.path.isdir(lic) else []:
            z.write(os.path.join(lic, n), 'assets/licenses/' + n)
        z.writestr('assets/arch.txt', 'armv8.0' if A.arch_v80() else 'armv8.2')      # the launcher compares it with the processor (issue 9)
        for abi in abis:
            lib = A.build_dir(abi, 'lib', 'libsmo.so')
            if not os.path.exists(lib):
                raise SystemExit('%s missing: run tools/android/build_runtime.py --abi %s' % (lib, abi))
            stripped = lib + '.stripped'
            run([A.llvm_bin('llvm-strip'), '--strip-unneeded', '-o', stripped, lib])
            z.write(stripped, 'lib/%s/libsmo.so' % abi)
            hooks = os.path.join(os.environ.get('ADRENOTOOLS') or os.path.join(os.path.dirname(A.REPO), 'external', 'libadrenotools'), 'build-android', 'src', 'hook')
            if abi == 'arm64-v8a' and os.path.isdir(hooks):          # libadrenotools' hooks (a custom Vulkan driver, android_driver.c)
                for n in sorted(os.listdir(hooks)):
                    if n.endswith('.so'):
                        z.write(os.path.join(hooks, n), 'lib/%s/%s' % (abi, n))
                        print('%s: %s (adrenotools hook)' % (abi, n))
            print('%s: libsmo.so %.0f MB (stripped from %.0f MB)' % (abi, os.path.getsize(stripped) / 2 ** 20, os.path.getsize(lib) / 2 ** 20))
    if release:                                      # nothing in a release names the builder: no home folder in any file
        user = os.path.basename(os.path.expanduser('~')).lower().encode()
        with zipfile.ZipFile(base) as z:
            bad = [n for n in z.namelist() if any(p + user in z.read(n).lower() for p in (b'users/', b'users' + bytes([92]), b'home/'))]
        if bad:
            raise SystemExit('the home folder of whoever built it is named in %s: rebuild them with android_env.prefix_maps()' % ', '.join(bad))
        print('release: no file names the home folder of whoever built it')
    aligned = os.path.join(out_dir, 'aligned.apk')
    run([tool('zipalign'), '-f', '-P', '16', '4', base, aligned])
    if release:
        ks = os.path.join(out_dir, 'release.keystore')
        # 0.2.15: every release so far (GitHub B0.1.0 and B0.2, test builds 0.1.0-0.2.15) is signed with one key, so each installs over the
        # one before. A build signed with any other key cannot: the player would have to uninstall, which deletes the imported game files.
        # The key is copied once to a folder beside the source tree (RELEASE_KEY_BACKUP, not in the source export) and comes back from there
        # when build/ was cleaned; with neither copy a new key is made only if SMO_NEW_RELEASE_KEY=1. Each signed APK is checked against
        # the key's SHA-256 recorded beside the backup (release_cert.txt, written at the first signed release) before it is written as
        # odyssey-<version>.apk. The project's own releases: 2b0720869f193194db73ab1269d72e3582fd16812655e31d35b99f4a58fc35be.
        bak = RELEASE_KEY_BACKUP
        if not os.path.exists(ks) and os.path.exists(os.path.join(bak, 'release.keystore')):
            for n in ('release.keystore', 'release.keystore.txt'): shutil.copy2(os.path.join(bak, n), os.path.join(out_dir, n))
            print('release key: brought back from %s' % bak)
        if not os.path.exists(ks) and os.environ.get('SMO_NEW_RELEASE_KEY') != '1':
            raise SystemExit('release key missing (%s, backup %s): a build signed with a new key cannot update the releases already handed out.\n'
                             'Put the old release.keystore and release.keystore.txt back, or set SMO_NEW_RELEASE_KEY=1 to start a new line of releases.' % (ks, bak))
        if not os.path.exists(ks):
            pw = secrets.token_urlsafe(18)
            run([os.path.join(env['JAVA_HOME'], 'bin', 'keytool'), '-genkeypair', '-keystore', ks, '-storepass', pw, '-keypass', pw,
                 '-alias', 'release', '-keyalg', 'RSA', '-keysize', '4096', '-validity', '10000', '-dname', 'CN=Odyssey Android port'], env=env)
            open(ks + '.txt', 'w').write(pw + '\n')
        pw = open(ks + '.txt').read().strip()
        if not os.path.exists(os.path.join(bak, 'release.keystore')):
            os.makedirs(bak, exist_ok=True)
            for n in ('release.keystore', 'release.keystore.txt'): shutil.copy2(os.path.join(out_dir, n), os.path.join(bak, n))
            print('release key: copied to %s (keep it: updates need it)' % bak)
        apk = os.path.join(out_dir, 'odyssey-%s%s.apk' % (version, '-armv8.0' if A.arch_v80() else ''))
    else:
        ks, pw = os.path.join(out_dir, 'debug.keystore'), 'android'
        if not os.path.exists(ks):
            run([os.path.join(env['JAVA_HOME'], 'bin', 'keytool'), '-genkeypair', '-keystore', ks, '-storepass', 'android', '-keypass', 'android',
                 '-alias', 'androiddebugkey', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=Android Debug,O=Android,C=US'], env=env)
        apk = os.path.join(out_dir, 'smo-%s.apk' % '-'.join(abis))
    out_apk = apk
    if release: apk = os.path.join(out_dir, 'signed.apk')     # written as odyssey-<version>.apk only once its key is checked
    run([tool('apksigner'), 'sign', '--ks', ks, '--ks-pass', 'pass:' + pw, '--key-pass', 'pass:' + pw, '--out', apk, aligned], env=env)
    run([tool('apksigner'), 'verify', apk], env=env)
    if release:
        certs = run([tool('apksigner'), 'verify', '--print-certs', apk], env=env)
        got = re.findall(r'certificate SHA-256 digest: ([0-9a-f]+)', certs)
        rec = os.path.join(RELEASE_KEY_BACKUP, 'release_cert.txt')
        want = open(rec).read().strip() if os.path.exists(rec) else None
        if want is None and len(got) == 1:
            open(rec, 'w').write(got[0] + '\n'); want = got[0]
            print('release key: its SHA-256 recorded in %s' % rec)
        if got != [want]:
            os.remove(apk)
            raise SystemExit('release key: the APK is signed with %s, not the key of the releases so far (%s, %s): it could not update them' % (got, want, rec))
        os.replace(apk, out_apk); apk = out_apk
        print('release key: the same as every release so far (installs over them)')
    print('APK: %s (%.0f MB)' % (apk, os.path.getsize(apk) / 2 ** 20))
    if '--install' in sys.argv:
        print(run([os.path.join(A.sdk(), 'platform-tools', 'adb.exe' if os.name == 'nt' else 'adb'), 'install', '-r', apk]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
