# How far along the way from real frame f to real frame f+1 the made frame f sits, per band (shift real f -> made f divided by
# shift real f -> real f+1). Ideal: 1 / (the logic steps of frame f+1), 0.5 for 2 steps, 0.33 for 3.
# usage: python -I fgeven.py <run dir> <frame> [<frame> ...]   (log.txt in the run dir gives the steps)
import sys, os, re
import numpy as np
from PIL import Image

run = sys.argv[1]; d = os.path.join(run, 'shots')
steps = {}
for line in open(os.path.join(run, 'log.txt'), encoding='utf-8', errors='replace'):
    m = re.search(r'nvn_vk_motion: frame (\d+): steps (\d+)', line)
    if m: steps[int(m.group(1))] = int(m.group(2))

def gray(n): return np.asarray(Image.open(os.path.join(d, n + '.bmp')).convert('L'), dtype=np.float32)

def shift(a, b, box):
    x0, y0, x1, y1 = box; ref = a[y0:y1, x0:x1]; best = None
    for dx in range(-100, 101):
        if x0 + dx < 0 or x1 + dx > a.shape[1]: continue
        e = np.abs(b[y0:y1, x0 + dx:x1 + dx] - ref).mean()
        if best is None or e < best[0]: best = (e, dx)
    return best[1]

H, W = 900, 1600
bands = {'left': (120, H // 3, W // 4, H // 3 + 80), 'right': (3 * W // 4, H // 3, W - 120, H // 3 + 80), 'upper': (W // 3, 210, 2 * W // 3, 290)}
for f in [int(a) for a in sys.argv[2:]]:
    r0, m0, r1 = gray('fg_%05d_real' % f), gray('fg_%05d_made' % f), gray('fg_%05d_real' % (f + 1))
    s = steps.get(f + 1)
    out = []
    for name, box in bands.items():
        a, b = shift(r0, m0, box), shift(r0, r1, box)
        out.append('%s %d/%d = %.2f' % (name, a, b, a / b if b else float('nan')))
    print('frame %d -> %d (%s steps, ideal %.2f): %s' % (f, f + 1, s, 1.0 / s if s else float('nan'), ' | '.join(out)))
