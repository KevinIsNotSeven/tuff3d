# Builds res/tuff3d.ico (16-256 px) from the 64 px pixel-art res/tuff3d.png.
# usage: python3 res/make_icon.py res/tuff3d.png res/tuff3d.ico [preview.png]
import sys
from PIL import Image
src = Image.open(sys.argv[1]).convert('RGBA')
assert src.size == (64, 64)
imgs = {}
for s in (256, 128):          # whole multiples: keep the pixel art crisp
    imgs[s] = src.resize((s, s), Image.NEAREST)
imgs[64] = src
for s in (48, 40, 32, 24, 20, 16):
    imgs[s] = src.resize((s, s), Image.LANCZOS, reducing_gap=None)
order = sorted(imgs, reverse=True)
imgs[256].save(sys.argv[2], format='ICO', sizes=[(s, s) for s in order],
               append_images=[imgs[s] for s in order[1:]])
if len(sys.argv) < 4: sys.exit()
sheet = Image.new('RGBA', (sum(order) + 10 * len(order), 256), (255, 255, 255, 255))
x = 0
for s in order:
    sheet.alpha_composite(imgs[s], (x, 0)); x += s + 10
sheet.save(sys.argv[3])
