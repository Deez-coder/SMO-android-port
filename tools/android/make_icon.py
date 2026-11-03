"""The app's launcher icon, drawn here (original art, nothing from the game): an adaptive icon, a night sky behind a
ringed planet and a four-pointed star.

    python tools/android/make_icon.py [out_res_dir]       # default build/android/res

SMO_ICON=<picture> (any PNG with transparency) puts that picture on the night sky instead of the planet: a private
build's own icon. The picture is the builder's own and stays out of this repository.

Writes mipmap-<density>/ic_launcher_background.png and ic_launcher_foreground.png (108 dp each) and
mipmap-anydpi-v26/ic_launcher.xml; tools/android/build_apk.py compiles them with aapt2. Also icon_preview.png (512 px,
the round mask a launcher puts on it) beside the folder.
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

DENSITIES = {'mdpi': 108, 'hdpi': 162, 'xhdpi': 216, 'xxhdpi': 324, 'xxxhdpi': 432}   # 108 dp in pixels
SS = 1728                                                                             # drawn at 4x xxxhdpi, scaled down


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def background():
    """Indigo at the top to violet and a warm glow at the bottom, with small stars."""
    img = Image.new('RGB', (SS, SS))
    top, mid, low = (14, 16, 52), (48, 30, 110), (150, 60, 140)
    px = img.load()
    for y in range(SS):
        t = y / (SS - 1)
        c = lerp(top, mid, t / 0.7) if t < 0.7 else lerp(mid, low, (t - 0.7) / 0.3)
        for x in range(SS):
            px[x, y] = c
    glow = Image.new('L', (SS, SS), 0)                                   # a soft light behind the planet
    ImageDraw.Draw(glow).ellipse([SS * 0.18, SS * 0.18, SS * 0.82, SS * 0.82], fill=110)
    glow = glow.filter(ImageFilter.GaussianBlur(SS * 0.09))
    img = Image.composite(Image.new('RGB', (SS, SS), (120, 90, 220)), img, glow)
    d = ImageDraw.Draw(img)
    rnd = random.Random(7)
    for _ in range(70):
        x, y = rnd.uniform(0, SS), rnd.uniform(0, SS * 0.85)
        r = rnd.choice([3, 4, 5, 6, 8])
        a = rnd.randint(140, 255)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(a, a, min(255, a + 20)))
    return img


def planet_layer():
    """A gold-to-coral sphere, lit from the upper left."""
    r = SS * 0.19
    cx, cy = SS * 0.5, SS * 0.53
    layer = Image.new('RGBA', (SS, SS), (0, 0, 0, 0))
    px = layer.load()
    lx, ly = -0.55, -0.6
    for y in range(int(cy - r) - 2, int(cy + r) + 3):
        for x in range(int(cx - r) - 2, int(cx + r) + 3):
            dx, dy = (x - cx) / r, (y - cy) / r
            q = dx * dx + dy * dy
            if q > 1.0:
                continue
            dz = math.sqrt(1.0 - q)
            light = max(0.0, (dx * lx + dy * ly + dz * 0.58) / 1.0)
            band = 0.5 + 0.5 * math.sin((dy * 0.9 + dx * 0.25) * 9.0)        # soft bands across it
            base = lerp((255, 196, 80), (240, 96, 70), min(1.0, max(0.0, dy * 0.6 + 0.45 + band * 0.18)))
            k = min(1.0, 0.25 + 0.95 * light)
            c = lerp((96, 44, 120), base, k)                                    # the shadow side turns violet, like the sky
            c = lerp(c, (255, 250, 225), max(0.0, light - 0.82) * 2.5)          # a highlight
            edge = min(1.0, (1.0 - math.sqrt(q)) * r / 2.0)                   # anti-aliased rim
            px[x, y] = c + (int(255 * edge),)
    return layer, (cx, cy, r)


def ring_halves(cx, cy, r):
    """The ring, tilted: the half behind the planet and the half in front of it."""
    w, h = r * 1.72, r * 0.5
    ring = Image.new('RGBA', (SS, SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(ring)
    for i, (k, col) in enumerate([(1.0, (255, 232, 190, 255)), (0.78, (0, 0, 0, 0))]):
        d.ellipse([cx - w * k, cy - h * k, cx + w * k, cy + h * k], fill=col)
    gap = Image.new('L', (SS, SS), 0)                                      # a darker line splits it in two bands
    ImageDraw.Draw(gap).ellipse([cx - w * 0.89, cy - h * 0.89, cx + w * 0.89, cy + h * 0.89], outline=255, width=int(SS * 0.008))
    ring.putalpha(ImageChops.subtract(ring.getchannel('A'), gap.point(lambda v: v * 0.55)))
    back = ring.copy()
    front = ring.copy()
    top = Image.new('L', (SS, SS), 0)
    ImageDraw.Draw(top).rectangle([0, 0, SS, cy], fill=255)
    back.putalpha(ImageChops.multiply(back.getchannel('A'), top))
    front.putalpha(ImageChops.multiply(front.getchannel('A'), ImageChops.invert(top)))
    return [h.rotate(-20, resample=Image.BICUBIC, center=(cx, cy)) for h in (back, front)]


def sparkle(img, x, y, s, col):
    """A four-pointed star with a soft glow."""
    glow = Image.new('L', (SS, SS), 0)
    ImageDraw.Draw(glow).ellipse([x - s * 0.7, y - s * 0.7, x + s * 0.7, y + s * 0.7], fill=150)
    glow = glow.filter(ImageFilter.GaussianBlur(s * 0.35))
    img.alpha_composite(Image.merge('RGBA', [Image.new('L', (SS, SS), c) for c in col] + [glow]))
    pts = []
    for i in range(8):
        a = i * math.pi / 4 - math.pi / 2
        rr = s if i % 2 == 0 else s * 0.2
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    ImageDraw.Draw(img).polygon(pts, fill=col + (255,))


def foreground():
    img = Image.new('RGBA', (SS, SS), (0, 0, 0, 0))
    planet, (cx, cy, r) = planet_layer()
    back, front = ring_halves(cx, cy, r)
    shadow = Image.new('L', (SS, SS), 0)                                   # a soft shadow under the planet
    ImageDraw.Draw(shadow).ellipse([cx - r, cy - r + SS * 0.02, cx + r, cy + r + SS * 0.02], fill=120)
    shadow = shadow.filter(ImageFilter.GaussianBlur(SS * 0.02))
    img.alpha_composite(Image.merge('RGBA', [Image.new('L', (SS, SS), 10)] * 3 + [shadow]))
    img.alpha_composite(back)
    img.alpha_composite(planet)
    img.alpha_composite(front)
    sparkle(img, SS * 0.69, SS * 0.29, SS * 0.08, (255, 246, 210))
    sparkle(img, SS * 0.32, SS * 0.31, SS * 0.033, (255, 255, 255))
    return img


def picture_foreground(path):
    """The builder's own picture, fitted into the middle of the icon (the 66 dp a launcher's mask never cuts), with a
    soft shadow."""
    pic = Image.open(path).convert('RGBA')
    if pic.getchannel('A').getextrema()[0] == 255:                         # no transparency: the white around it goes
        rgb = pic.convert('RGB')
        for corner in [(0, 0), (rgb.width - 1, 0), (0, rgb.height - 1), (rgb.width - 1, rgb.height - 1)]:
            if min(rgb.getpixel(corner)) > 235:
                ImageDraw.floodfill(rgb, corner, (255, 0, 255), thresh=40)
        key = Image.eval(ImageChops.difference(rgb, Image.new('RGB', rgb.size, (255, 0, 255))).convert('L'), lambda v: 255 if v > 24 else 0)
        pic.putalpha(key.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.7)))
    box = SS * 0.56
    k = box / max(pic.size)
    pic = pic.resize((max(1, int(pic.width * k)), max(1, int(pic.height * k))), Image.LANCZOS)
    img = Image.new('RGBA', (SS, SS), (0, 0, 0, 0))
    at = ((SS - pic.width) // 2, (SS - pic.height) // 2)
    shadow = Image.new('L', (SS, SS), 0)
    shadow.paste(pic.getchannel('A').point(lambda v: v * 0.45), (at[0], at[1] + int(SS * 0.02)))
    img.alpha_composite(Image.merge('RGBA', [Image.new('L', (SS, SS), 10)] * 3 + [shadow.filter(ImageFilter.GaussianBlur(SS * 0.015))]))
    img.alpha_composite(pic, at)
    return img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'build', 'android', 'res')
    pic = os.environ.get('SMO_ICON')
    bg, fg = background(), picture_foreground(pic) if pic else foreground()
    for name, px in DENSITIES.items():
        d = os.path.join(out, 'mipmap-' + name)
        os.makedirs(d, exist_ok=True)
        bg.resize((px, px), Image.LANCZOS).save(os.path.join(d, 'ic_launcher_background.png'))
        fg.resize((px, px), Image.LANCZOS).save(os.path.join(d, 'ic_launcher_foreground.png'))
    d = os.path.join(out, 'mipmap-anydpi-v26')
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'ic_launcher.xml'), 'w', encoding='utf-8').write(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@mipmap/ic_launcher_background" />\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
        '</adaptive-icon>\n')
    prev = bg.convert('RGBA')                                              # what a round launcher mask shows (72 of 108 dp)
    prev.alpha_composite(fg)
    m = SS * 18 // 108
    prev = prev.crop((m, m, SS - m, SS - m)).resize((512, 512), Image.LANCZOS)
    mask = Image.new('L', (2048, 2048), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, 2047, 2047], fill=255)
    prev.putalpha(mask.resize((512, 512), Image.LANCZOS))
    prev.save(os.path.join(out, '..', 'icon_preview.png'))
    print('icon: %s' % os.path.abspath(out))


if __name__ == '__main__':
    main()
