# Mario's screen position (the centre of his strongly red pixels in the lower middle of the screen) in real frame f, made
# frame f and real frame f+1, and how far along the way the made frame puts him (ideal: 1 / the steps of frame f+1).
# usage: python -I mariochk.py <run dir> <frame> [<frame> ...]
import sys, os, re
import numpy as np
from PIL import Image

run = sys.argv[1]; d = os.path.join(run, 'shots')
steps = {}
for line in open(os.path.join(run, 'log.txt'), encoding='utf-8', errors='replace'):
    m = re.search(r'nvn_vk_motion: frame (\d+): steps (\d+)', line)
    if m: steps[int(m.group(1))] = int(m.group(2))

def mario(name):
    a = np.asarray(Image.open(os.path.join(d, name + '.bmp')).convert('RGB'), dtype=np.int32)
    H, W, _ = a.shape
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    red = (r > 140) & (g < 70) & (b < 80)
    red[:int(H * 0.35), :] = False; red[:, :int(W * 0.2)] = False; red[:, int(W * 0.8):] = False
    ys, xs = np.nonzero(red)
    if len(xs) < 30: return None
    return xs.mean(), ys.mean(), len(xs)

for f in [int(x) for x in sys.argv[2:]]:
    a, m, b = mario('fg_%05d_real' % f), mario('fg_%05d_made' % f), mario('fg_%05d_real' % (f + 1))
    if not (a and m and b): print(f, 'Mario not found', a, m, b); continue
    dx, dy = b[0] - a[0], b[1] - a[1]
    mx, my = m[0] - a[0], m[1] - a[1]
    s = steps.get(f + 1)
    along = (mx * dx + my * dy) / (dx * dx + dy * dy) if dx * dx + dy * dy > 1 else float('nan')
    print('frame %d: Mario real (%.0f,%.0f) made (%+.1f,%+.1f) next real (%+.1f,%+.1f) | made sits %.2f of the way (ideal %.2f), off the line by %.1f px'
          % (f, a[0], a[1], mx, my, dx, dy, along, 1.0 / s if s else float('nan'), abs(mx * dy - my * dx) / max(1e-6, (dx * dx + dy * dy) ** 0.5)))
