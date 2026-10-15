# -*- coding: utf-8 -*-
r"""增强主观题/大题配图（提高手机/高分屏可读性）：
 1. 自动裁掉四周空白（按相对背景阈值 + 行/列内容统计）
 2. 灰度化 + 自动对比度拉伸
 3. 2 倍 LANCZOS 平滑放大 + USM 锐化
用法：
  python tools_prepare_subj_imgs.py            # 增强（原图备份到 imgs\_raw，只备份一次）
  python tools_prepare_subj_imgs.py --restore  # 从 imgs\_raw 还原
  python tools_prepare_subj_imgs.py --dry      # 只打印，不写文件
  python tools_prepare_subj_imgs.py 图1.png 图2.png   # 只处理指定图片
"""
import glob
import os
import shutil
import sys

from PIL import Image, ImageFilter, ImageOps, ImageStat

IMG = r'd:\github\面试题刷题软件-主观题\imgs'
RAW = os.path.join(IMG, '_raw')
DRY = '--dry' in sys.argv
RESTORE = '--restore' in sys.argv
DO_CROP = '--crop' in sys.argv      # 默认不裁切（防误裁图纸内容），需要时显式开启
ONLY = [a for a in sys.argv[1:] if not a.startswith('--')]

# 逐图微调（坐标按"裁边+2 倍放大"之后的图算）
TWEAK = {}


def content_box(im, drop=38, pad=10, min_run=3, margin=6, min_keep=0.55):
    """按"比背景暗 drop 个灰阶"找内容，再用行/列像素统计收紧边界。
    min_keep：裁切后宽/高至少要保留原图的这个比例，否则视为误判、不裁。"""
    bg = ImageStat.Stat(im).median[0]
    thr = max(0, bg - drop)
    mask = im.point(lambda p: 255 if p < thr else 0)
    px = mask.load()
    W, H = mask.size
    cols = [0] * W
    rows = [0] * H
    for y in range(margin, H - margin):
        for x in range(margin, W - margin):
            if px[x, y]:
                cols[x] += 1
                rows[y] += 1
    xs = [x for x in range(W) if cols[x] >= min_run]
    ys = [y for y in range(H) if rows[y] >= min_run]
    if not xs or not ys:
        return None
    box = (max(0, xs[0] - pad), max(0, ys[0] - pad),
           min(W, xs[-1] + 1 + pad), min(H, ys[-1] + 1 + pad))
    if (box[2] - box[0]) < W * min_keep or (box[3] - box[1]) < H * min_keep:
        return None                                  # 内容过少 → 疑似误判，放弃裁切
    return box, bg, thr


def enhance(path, scale=2):
    name = os.path.basename(path)
    im = Image.open(path).convert('L')
    before = im.size
    r = content_box(im) if DO_CROP else None
    info = ''
    if r:
        box, bg, thr = r
        if (box[2] - box[0]) < im.width or (box[3] - box[1]) < im.height:
            im = im.crop(box)
        info = f"bg={bg} thr={thr} crop={box}"
    im = ImageOps.autocontrast(im, cutoff=(0, 10))
    im = im.resize((im.width * scale, im.height * scale), Image.LANCZOS)
    im = im.filter(ImageFilter.UnsharpMask(radius=2, percent=110, threshold=3))
    tk = TWEAK.get(name, {})
    if tk.get('crop_top'):
        im = im.crop((0, int(tk['crop_top']), im.width, im.height))
        info += f" 顶部再裁 {tk['crop_top']}px"
    for bx in tk.get('blur_boxes', []):
        bx = tuple(int(v) for v in bx)
        pad = 6
        region = im.crop((max(0, bx[0] - pad), max(0, bx[1] - pad),
                          min(im.width, bx[2] + pad), min(im.height, bx[3] + pad)))
        im.paste(region.filter(ImageFilter.GaussianBlur(14)),
                 (max(0, bx[0] - pad), max(0, bx[1] - pad)))
        info += f" 糊掉原卷手写痕迹 {bx}"
    if not DRY:
        im.convert('RGB').save(path, optimize=True)
    return before, im.size, info


def main():
    files = sorted(glob.glob(os.path.join(IMG, '*.png')))
    if ONLY:
        files = [p for p in files if os.path.basename(p) in ONLY]
    if RESTORE:
        n = 0
        for p in files:
            bak = os.path.join(RAW, os.path.basename(p))
            if os.path.exists(bak):
                shutil.copy2(bak, p)
                n += 1
                print('还原', os.path.basename(p))
        print(f'共还原 {n} 张')
        return
    os.makedirs(RAW, exist_ok=True)
    for p in files:
        name = os.path.basename(p)
        bak = os.path.join(RAW, name)
        if not os.path.exists(bak):
            shutil.copy2(p, bak)
        before, after, info = enhance(p)
        print(f"{name}: {before[0]}x{before[1]} -> {after[0]}x{after[1]}"
              f"  {os.path.getsize(p)//1024} KB   {info}" + ("  (dry)" if DRY else ""))
    print('原图备份:', RAW)


if __name__ == '__main__':
    main()
