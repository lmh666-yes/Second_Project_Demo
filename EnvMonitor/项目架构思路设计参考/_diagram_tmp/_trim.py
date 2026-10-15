# 把渲染出来的长图上下多余的同色背景裁掉，宽度保持不变。
# 用法：python _trim.py <输入 png> <输出 png>
# 背景色取左上角像素；按行扫描非背景像素，求首尾行。
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
w, h = im.size
px = im.load()
bg = px[0, 0]

top, bottom = -1, -1
for y in range(h):
    for x in range(w):
        if px[x, y] != bg:
            if top < 0:
                top = y
            bottom = y
            break

if bottom < 0:
    print("整张图都是背景色，未裁剪")
    sys.exit(1)

im.crop((0, top, w, bottom + 1)).save(dst)
print("裁到 y =", top, "..", bottom + 1, "输出尺寸", w, "x", bottom + 1 - top)
