"""Registers in locals: a pass over lifted C (recomp.py output) that keeps a function's general registers in local variables.

    python tools/lifter/regcache.py <dir with c_*.c> [--check]

Lifted code reads and writes the guest's registers as c->x[N]. Guest memory is written through pointers the compiler
cannot tell apart from the Ctx (memcpy to an address made from an integer), so after every guest store it reloads every
register it needs from memory: on a Galaxy S24 Ultra the game's own lifted code is 64% of its main thread. Here a
function copies the registers it uses into locals (gx0..gx31) on entry, works on those (registers of the host CPU), and
puts back the ones it writes wherever code outside the function may look at the Ctx:
  - before a call of another lifted function, call_indirect or host_import: the written ones are stored; after it,
    every register the function uses is loaded again (a callee may change any: outlined code does not keep the ABI);
  - before return (and before guest_trap, so a crash report shows the registers).
The helpers that take c and stay inline (adds32/64, logic32/64, cond, fcmp, set_nzcv, stx32/64, v_tbl and the vector
helpers) only touch the flags, the vector registers or the exclusive monitor, never c->x: no store or load around them.
Flags (c->N..V) and vector registers (c->v) stay in the Ctx.

A function that already has locals (a line `uint64_t gx`) is left alone, so the pass can run twice. --check only reports.
"""
import os
import re
import sys

SIG = re.compile(r'^(?:static )?void f_[0-9a-f]+\(Ctx \*(?:restrict )?c\) \{\s*$')
REG = re.compile(r'c->x\[(\d+)\]')
WRITE = re.compile(r'c->x\[(\d+)\]\s*(?:=(?!=)|\+=|-=|\*=|\|=|&=|\^=|<<=|>>=)')
CALL = re.compile(r'(\bf_[0-9a-f]+\(c\);|\bcall_indirect\(c, [^;]*\);|\bhost_import\(c, [^;]*\);)')
TRAP = re.compile(r'(\bguest_trap\()')
RET = re.compile(r'\breturn;')


def transform(body):
    """body: the lines between the signature and the closing brace. Returns the new lines, or None to keep them."""
    text = '\n'.join(body)
    used = sorted({int(n) for n in REG.findall(text)})
    if not used or 'uint64_t gx' in text:
        return None
    written = sorted({int(n) for n in WRITE.findall(text)})
    flush = ' '.join('c->x[%d] = gx%d;' % (r, r) for r in written)
    reload = ' '.join('gx%d = c->x[%d];' % (r, r) for r in used)
    out = []
    for line in body:
        line = REG.sub(lambda m: 'gx' + m.group(1), line)
        line = CALL.sub(lambda m: '{ %s %s %s }' % (flush, m.group(1), reload), line)
        line = TRAP.sub(lambda m: '%s %s' % (flush, m.group(1)) if flush else m.group(1), line)
        if flush:
            line = RET.sub('{ %s return; }' % flush, line)
        out.append(line)
    decl = '  uint64_t ' + ', '.join('gx%d = c->x[%d]' % (r, r) for r in used) + ';'
    tail = ['  ' + flush] if flush else []          # a function whose last instruction falls off the end
    return [decl] + out + tail


def process(path, check):
    lines = open(path, encoding='latin-1', newline='').read().split('\n')
    out, i, changed, total = [], 0, 0, 0
    while i < len(lines):
        line = lines[i]
        if SIG.match(line):
            j = i + 1
            while j < len(lines) and lines[j] != '}':
                j += 1
            total += 1
            new = transform(lines[i + 1:j])
            if new is None:
                out.extend(lines[i:j + 1])
            else:
                out.append(line)
                out.extend(new)
                out.append('}')
                changed += 1
            i = j + 1
            continue
        out.append(line)
        i += 1
    if changed and not check:
        open(path, 'w', encoding='latin-1', newline='').write('\n'.join(out))
    return changed, total


def main():
    d = sys.argv[1]
    check = '--check' in sys.argv
    files = sorted(n for n in os.listdir(d) if n.startswith('c_') and n.endswith('.c'))
    changed = total = 0
    for n in files:
        c, t = process(os.path.join(d, n), check)
        changed += c; total += t
    print('regcache: %d of %d functions in %d files %s' % (changed, total, len(files), 'would change (--check)' if check else 'changed'))


if __name__ == '__main__':
    main()
