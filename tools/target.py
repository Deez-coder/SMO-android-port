"""Which game version a build is for, and where its files go (used by the tools, scripts and tests).

SMO_TARGET=1.0.0 (the default): the base package alone; its modules in build/dump/base, everything else under build/.
SMO_TARGET=<your update's version> (1.4.1): the update package applied; its modules in build/dump/update and a build
folder of its own, build/<version>/, laid out as build/ is (romfs, manual, functions, lift, obj, gen, bin, run, ...), so
the runtime's paths relative to its exe (..\\romfs, ..\\manual, ..\\userdata) work unchanged. build/dump is shared.
scripts/setup.py --target sets SMO_TARGET for every step it runs.

SMO_LAYOUT=console (the default) | android: where the guest's image, the SDK image and the placeholder ranges of the
runtime lie in the host's address space (guest pointers are host pointers, so the lifted code has them as constants).
console = the Switch's own addresses (0x71_0000_0000 ..); android = 0x40_0000_0000 .., below what Android maps on its
own (its libraries and heap above 0x70_0000_0000, the zygote's executable somewhere in 0x55 .. 0x65_0000_0000). The
android layout lifts into build/android/lift and writes its tables into build/android/gen; the rest is shared.
"""
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_VERSION = '1.0.0'
TARGET = os.environ.get('SMO_TARGET') or BASE_VERSION
IS_BASE = TARGET == BASE_VERSION
BUILD = os.path.join(REPO, 'build') if IS_BASE else os.path.join(REPO, 'build', TARGET)
DUMP_KIND = 'base' if IS_BASE else 'update'          # also the key of the package in build/dump/version.json
DUMP = os.path.join(REPO, 'build', 'dump', DUMP_KIND)
LAYOUT = os.environ.get('SMO_LAYOUT') or 'console'
_BASES = {'console': (0x7100000000, 0x7300000000, 0x7400000000, 0x7500000000),
          'android': (0x4000000000, 0x4100000000, 0x4200000000, 0x4300000000)}
if LAYOUT not in _BASES:
    raise SystemExit('SMO_LAYOUT=%s: not a layout (%s)' % (LAYOUT, ', '.join(_BASES)))
GUEST_BASE, IMPORT_BASE, HOSTFN_BASE, SDK_BASE = _BASES[LAYOUT]


def build(*parts):
    """A path in this target's build folder (the lifted code and the tables of a non-default layout: its own)."""
    if LAYOUT != 'console' and parts and parts[0] in ('lift', 'gen'):
        return os.path.join(BUILD, LAYOUT, *parts)
    return os.path.join(BUILD, *parts)


def elf(module):
    """The converted module (main, sdk, subsdk0, rtld) of this target's package."""
    return os.path.join(DUMP, 'elf', module + '.elf')


def update_version():
    """The display version of the extracted update package (from its NACP), or None."""
    p = os.path.join(REPO, 'build', 'dump', 'update', 'control.nacp')
    if not os.path.isfile(p):
        return None
    return open(p, 'rb').read()[0x3060:0x3070].split(b'\0')[0].decode('ascii', 'replace')
