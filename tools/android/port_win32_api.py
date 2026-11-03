"""Second stage of the Android port's source conversion (2026-10-27): Win32 calls with a direct runtime/plat.h
equivalent, in the forms the runtime writes them. Run after port_win32_names.py; what is left (window code, COM, Media
Foundation, memory queries with conditions of their own) was ported by hand. Never run on plat.h / plat_*.c.

    python tools/android/port_win32_api.py runtime/<file>.c ...
"""
import re
import sys

SUBS = [
    # thread priorities
    (r'\bTHREAD_PRIORITY_TIME_CRITICAL\b', 'PLAT_PRIO_TIME_CRITICAL'), (r'\bTHREAD_PRIORITY_HIGHEST\b', 'PLAT_PRIO_HIGHEST'),
    (r'\bTHREAD_PRIORITY_ABOVE_NORMAL\b', 'PLAT_PRIO_ABOVE'), (r'\bTHREAD_PRIORITY_BELOW_NORMAL\b', 'PLAT_PRIO_BELOW'),
    (r'\bTHREAD_PRIORITY_NORMAL\b', 'PLAT_PRIO_NORMAL'), (r'\bTHREAD_PRIORITY_LOWEST\b', 'PLAT_PRIO_LOWEST'),
    (r'\bTHREAD_PRIORITY_IDLE\b', 'PLAT_PRIO_LOWEST'),
    (r'\bGetThreadPriority\(GetCurrentThread\(\)\)', 'plat_thread_get_priority()'),
    (r'\bSetThreadPriority\(GetCurrentThread\(\),\s*', 'plat_thread_set_priority(NULL, '),
    (r'\bSetThreadPriority\(', 'plat_thread_set_priority('),
    # threads: CreateThread(NULL, stack, fn, arg, 0, NULL) -> a plat_thread running the uint32_t-returning fn
    (r'\bCreateThread\(\s*NULL\s*,\s*([^,]+?)\s*,\s*(\w+)\s*,\s*([^,]+?)\s*,\s*0\s*,\s*NULL\s*\)', r'plat_thread_start32(\2, \3, \1, "\2")'),
    (r'\bWaitForMultipleObjects\(\s*\(uint32_t\)\s*(\w+)\s*,\s*(\w+)\s*,\s*1\s*,\s*([^)]+?)\s*\)', r'plat_threads_wait_all(\2, \1, \3)'),
    # events
    (r'\bCreateEventA\(\s*NULL\s*,\s*([01]|TRUE|FALSE)\s*,\s*([01]|TRUE|FALSE)\s*,\s*NULL\s*\)', r'plat_event_create(\1, \2)'),
    (r'\bSetEvent\(', 'plat_event_set('), (r'\bResetEvent\(', 'plat_event_reset('),
    # processors
    (r'SYSTEM_INFO (\w+);\s*GetSystemInfo\(&\1\);', r'int \1_n = plat_cpu_count();'),
    # memory: the plain forms (fresh read-write memory anywhere, the release of a whole allocation)
    (r'\bVirtualAlloc\(\s*NULL\s*,\s*([^,]+?)\s*,\s*MEM_RESERVE\s*\|\s*MEM_COMMIT\s*,\s*PAGE_READWRITE\s*\)', r'plat_vm_alloc(NULL, \1, PLAT_PROT_RW)'),
    (r'\bVirtualFree\(\s*([^;]+?)\s*,\s*0\s*,\s*MEM_RELEASE\s*\)', r'plat_vm_free(\1, 0)'),
    # files
    (r'\bCreateDirectoryA\(\s*([^,]+?)\s*,\s*NULL\s*\)', r'plat_mkdirs(\1)'),
    (r'\b_fseeki64\(', 'plat_fseek64('), (r'\b_ftelli64\(', 'plat_ftell64('),
    (r'\bDeleteFileA\(', 'plat_remove('), (r'\bRemoveDirectoryA\(', 'plat_remove('),
    (r'\bGetFileAttributesA\(([^()]*(?:\([^()]*\))?[^()]*)\)\s*!=\s*INVALID_FILE_ATTRIBUTES', r'plat_exists(\1)'),
    (r'\bGetFileAttributesA\(([^()]*(?:\([^()]*\))?[^()]*)\)\s*==\s*INVALID_FILE_ATTRIBUTES', r'!plat_exists(\1)'),
    (r'\bMoveFileExA\(\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*MOVEFILE_REPLACE_EXISTING\s*\)\s*!=\s*0', r'plat_rename(\1, \2)'),
    (r'\bMoveFileExA\(\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*MOVEFILE_REPLACE_EXISTING\s*\)', r'plat_rename(\1, \2)'),
]


ONCE_FN = re.compile(r'static (?:int|BOOL) CALLBACK (\w+)\(\s*PINIT_ONCE \w+\s*,\s*void \* ?\w+\s*,\s*void \* ?\*\s*\w+\s*\)\s*\{\n'
                     r'(?:\s*\(void\)\w+; \(void\)\w+; \(void\)\w+;\n)?')


def once_functions(text):
    """InitOnceExecuteOnce callbacks -> plain void functions for plat_once_run (their `return 1;` / `return 0;` too)"""
    out, pos = [], 0
    for m in ONCE_FN.finditer(text):
        end = text.index('\n}\n', m.end())
        body = re.sub(r'\breturn (?:1|0|TRUE|FALSE);', 'return;', text[m.end():end])
        out.append(text[pos:m.start()] + 'static void %s(void) {\n' % m.group(1) + body)
        pos = end
    out.append(text[pos:])
    return ''.join(out)


SUBS += [
    (r'\bINIT_ONCE (\w+) = INIT_ONCE_STATIC_INIT;', r'plat_once \1 = PLAT_ONCE_INIT;'),
    (r'\bInitOnceExecuteOnce\(\s*&(\w+)\s*,\s*(\w+)\s*,\s*NULL\s*,\s*NULL\s*\)', r'plat_once_run(&\1, \2)'),
    (r'\bMemoryBarrier\(\)', '__atomic_thread_fence(__ATOMIC_SEQ_CST)'),
    (r'\bGetTempPathA\(\s*([^,]+?)\s*,\s*(\w+)\s*\)', r'snprintf(\2, \1, "%s" PLAT_SEP_S, plat_temp_dir())'),
]


def convert(text):
    text = once_functions(text)
    for rx, rep in SUBS:
        text = re.sub(rx, rep, text)
    return text


def main():
    for path in sys.argv[1:]:
        if 'plat' in path.replace('\\', '/').split('/')[-1]:
            continue
        with open(path, encoding='utf-8', errors='surrogateescape', newline='') as f:
            src = f.read()
        out = convert(src)
        if out != src:
            with open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
                f.write(out)
            print('converted %s' % path)


if __name__ == '__main__':
    main()
