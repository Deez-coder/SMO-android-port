# Fit k: real frame f rebuilt from real frame f-1 as B(q) = A(q + k * mv(q)) (mv: old position - new position).
# k = 1 means the motion vectors match how far the picture really moved between the two real frames.
# usage: python -I mvfit.py <shots dir> <frame> [<frame> ...]
import sys, glob, os
import numpy as np
from PIL import Image

d = sys.argv[1]

def gray(path):
    return np.asarray(Image.open(path).convert('L'), dtype=np.float32)

def sample(img, x, y):                      # bilinear, clamped
    h, w = img.shape
    x = np.clip(x, 0, w - 1.001); y = np.clip(y, 0, h - 1.001)
    x0 = np.floor(x).astype(np.int32); y0 = np.floor(y).astype(np.int32)
    fx = x - x0; fy = y - y0
    a = img[y0, x0]; b = img[y0, x0 + 1]; c = img[y0 + 1, x0]; e = img[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + e * fx) * fy

for f in [int(a) for a in sys.argv[2:]]:
    raw = glob.glob(os.path.join(d, 'motion_%05d_*.rg16f' % f))
    if not raw:
        print(f, 'no raw vectors'); continue
    mw, mh = [int(v) for v in raw[0].rsplit('_', 1)[1].split('.')[0].split('x')]
    mv = np.fromfile(raw[0], dtype=np.float16).astype(np.float32).reshape(mh, mw, 2)
    A = gray(os.path.join(d, 'fg_%05d_real.bmp' % (f - 1))); B = gray(os.path.join(d, 'fg_%05d_real.bmp' % f))
    H, W = A.shape
    qy, qx = np.mgrid[0:H, 0:W].astype(np.float32)
    mi = np.clip(((qx + 0.5) * mw / W).astype(np.int32), 0, mw - 1); mj = np.clip(((qy + 0.5) * mh / H).astype(np.int32), 0, mh - 1)
    vx = mv[mj, mi, 0] * (W / mw); vy = mv[mj, mi, 1] * (H / mh)
    # leave out the HUD corners, Mario (the centre) and the screen edges
    keep = np.ones((H, W), bool)
    keep[:int(H * 0.22), :] = False; keep[int(H * 0.85):, :] = False
    keep[:, :int(W * 0.08)] = False; keep[:, int(W * 0.92):] = False
    keep[int(H * 0.35):int(H * 0.85), int(W * 0.38):int(W * 0.62)] = False
    regions = {}
    for name, (y0, y1) in {'rows22-35': (0.22, 0.35), 'rows35-50': (0.35, 0.5), 'rows50-65': (0.5, 0.65), 'rows65-85': (0.65, 0.85)}.items():
        m = keep.copy(); m[:int(H * y0), :] = False; m[int(H * y1):, :] = False; regions[name] = m
    print(f, 'vectors: mean x %.1f window px, mean |v| %.1f' % (vx[keep].mean(), np.hypot(vx, vy)[keep].mean()))
    for name, m in regions.items():
        best = None; errs = []
        for k in np.arange(0.0, 2.01, 0.05):
            P = sample(A, qx + k * vx, qy + k * vy)
            err = np.abs(P - B)[m].mean(); errs.append((k, err))
            if best is None or err < best[1]: best = (k, err)
        print('   %-5s best k = %.2f (error %.1f; k=0: %.1f, k=1: %.1f)' % (name, best[0], best[1], errs[0][1], errs[20][1]))
