"""One-off source conversion used for the Android port (2026-10-27): rewrites the runtime's common Win32 types, locks,
atomics and clock calls into the neutral names of runtime/plat.h, in place. Everything that is not a plain renaming
(memory, threads, events, files, windows) was ported by hand afterwards. Kept so the same rewrite can be applied to
files merged later from upstream smo-recomp:

    python tools/android/port_win32_names.py runtime/<file>.c ...
"""
import re
import sys

WORDS = [
    # types
    ('LONG64', 'int64_t'), ('LONGLONG', 'int64_t'), ('ULONGLONG', 'uint64_t'), ('DWORD64', 'uint64_t'),
    ('DWORD_PTR', 'uintptr_t'), ('ULONG_PTR', 'uintptr_t'), ('DWORD', 'uint32_t'), ('ULONG', 'uint32_t'),
    ('LONG', 'int32_t'), ('BOOL', 'int'), ('BYTE', 'uint8_t'), ('WORD', 'uint16_t'), ('UINT', 'unsigned'),
    ('SIZE_T', 'size_t'), ('PVOID', 'void *'), ('LPVOID', 'void *'), ('TRUE', '1'), ('FALSE', '0'),
    ('INFINITE', 'PLAT_INFINITE'), ('MAX_PATH', 'PLAT_MAX_PATH'), ('LARGE_INTEGER', 'plat_li'), ('WINAPI', ''),
    # locks
    ('CRITICAL_SECTION', 'plat_mutex'), ('CONDITION_VARIABLE', 'plat_cond'), ('SRWLOCK', 'plat_rwlock'),
    ('SRWLOCK_INIT', 'PLAT_RWLOCK_INIT'), ('CONDITION_VARIABLE_INIT', 'PLAT_COND_INIT'),
    ('InitializeCriticalSectionAndSpinCount', 'plat_mutex_init_spin'), ('InitializeCriticalSection', 'plat_mutex_init'),
    ('DeleteCriticalSection', 'plat_mutex_destroy'), ('EnterCriticalSection', 'plat_mutex_lock'),
    ('LeaveCriticalSection', 'plat_mutex_unlock'), ('TryEnterCriticalSection', 'plat_mutex_trylock'),
    ('InitializeConditionVariable', 'plat_cond_init'), ('SleepConditionVariableCS', 'plat_cond_wait'),
    ('WakeConditionVariable', 'plat_cond_signal'), ('WakeAllConditionVariable', 'plat_cond_broadcast'),
    ('InitializeSRWLock', 'plat_rw_init'), ('AcquireSRWLockShared', 'plat_rw_rdlock'),
    ('ReleaseSRWLockShared', 'plat_rw_rdunlock'), ('AcquireSRWLockExclusive', 'plat_rw_wrlock'),
    ('ReleaseSRWLockExclusive', 'plat_rw_wrunlock'),
    # atomics
    ('InterlockedIncrement64', 'plat_atomic_inc'), ('InterlockedIncrement', 'plat_atomic_inc'),
    ('InterlockedDecrement64', 'plat_atomic_dec'), ('InterlockedDecrement', 'plat_atomic_dec'),
    ('InterlockedAdd64', 'plat_atomic_add'), ('InterlockedExchangeAdd64', 'plat_atomic_fetch_add'),
    ('InterlockedExchangeAdd', 'plat_atomic_fetch_add'), ('InterlockedExchange64', 'plat_atomic_xchg'),
    ('InterlockedExchangePointer', 'plat_atomic_xchg'), ('InterlockedExchange', 'plat_atomic_xchg'),
    ('InterlockedCompareExchange64', 'plat_atomic_cas'), ('InterlockedCompareExchangePointer', 'plat_atomic_cas'),
    ('InterlockedCompareExchange', 'plat_atomic_cas'), ('InterlockedOr8', 'plat_atomic_fetch_or'),
    # clocks, threads, process
    ('GetTickCount64', 'plat_ms64'), ('SwitchToThread', 'plat_yield'), ('GetCurrentThreadId', 'plat_tid'),
    ('GetCurrentProcessId', 'plat_pid'), ('_strlwr', 'plat_strlwr'), ('_stricmp', 'plat_stricmp'),
    ('_strnicmp', 'plat_strnicmp'), ('_snprintf', 'snprintf'),
]
PATTERNS = [
    (re.compile(r'#include\s*<windows\.h>'), '#include "plat.h"'),
    (re.compile(r'QueryPerformanceCounter\(\s*&\s*([A-Za-z_][\w.\[\]>-]*)\s*\)'), r'((\1).QuadPart = plat_qpc())'),
    (re.compile(r'QueryPerformanceFrequency\(\s*&\s*([A-Za-z_][\w.\[\]>-]*)\s*\)'), r'((\1).QuadPart = plat_qpf())'),
    (re.compile(r'__declspec\(\s*thread\s*\)'), '_Thread_local'),
    (re.compile(r'__declspec\(\s*dllexport\s*\)'), 'PLAT_EXPORT'),
    (re.compile(r'__declspec\(\s*dllimport\s*\)'), 'PLAT_IMPORT'),
    (re.compile(r'\bSleep\('), 'plat_sleep_ms('),
    (re.compile(r'\bExitProcess\('), 'plat_exit((int)'),
]
WORD_RE = re.compile(r'\b(%s)\b' % '|'.join(re.escape(a) for a, _ in sorted(WORDS, key=lambda w: -len(w[0]))))
WORD_MAP = dict(WORDS)


def convert(text):
    for rx, rep in PATTERNS:
        text = rx.sub(rep, text)
    return WORD_RE.sub(lambda m: WORD_MAP[m.group(1)], text)


def main():
    for path in sys.argv[1:]:
        with open(path, encoding='utf-8', errors='surrogateescape', newline='') as f:
            src = f.read()
        out = convert(src)
        if out != src:
            with open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
                f.write(out)
            print('converted %s' % path)


if __name__ == '__main__':
    main()
