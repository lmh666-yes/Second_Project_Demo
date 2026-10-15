# -*- coding: utf-8 -*-
r"""增强题目配图（提高手机/高分屏可读性）：
 1. 自动裁掉四周空白（按相对背景阈值 + 行/列内容统计，能去掉照片的灰白阴影）
 2. 灰度化 + 自动对比度拉伸
 3. 2 倍 LANCZOS 平滑放大 + USM 锐化（线条更利落）
用法：
  python tools\prepare_imgs.py            # 增强（原图自动备份到 imgs\_raw，只备份一次）
  python tools\prepare_imgs.py --restore  # 从 imgs\_raw 还原
  python tools\prepare_imgs.py --dry      # 只打印裁切结果，不写文件
"""
import glob
import os
import shutil
import sys

from PIL import Image, ImageFilter, ImageOps, ImageStat

APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(APP, 'imgs')
RAW = os.path.join(IMG, '_raw')
DRY = '--dry' in sys.argv
RESTORE = '--restore' in sys.argv

# 逐图微调（在"裁边+2 倍放大"之后执行，坐标按放大后的图算）
#  crop_top   ：从顶部再裁掉多少像素（去掉原卷裁剪时被切断的题干行）
#  blur_boxes ：要打码糊掉的区域（原卷上的手写作答痕迹，避免提前泄露答案）
TWEAK = {
    'fig-15-logic.png': {'crop_top': 50},
    'fig-6-dcdc.png': {'blur_boxes': [(540, 20, 680, 120)]},
}


def content_box(im, drop=38, pad=10, min_run=3, margin=6):
    """按"比背景暗 drop 个灰阶"找内容，再用行/列像素统计收紧边界
    margin：忽略最外圈像素（照片裁剪常带 1~4px 黑边框）"""
    bg = ImageStat.Stat(im).median[0]                  # 背景灰度（中位数）
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
    return box, bg, thr


def enhance(path, scale=2):
    name = os.path.basename(path)
    im = Image.open(path).convert('L')
    before = im.size
    r = content_box(im)
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
    files = [p for p in sorted(glob.glob(os.path.join(IMG, '*.png')))]
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
            shutil.copy2(p, bak)                       # 原图只备份一次
        before, after, info = enhance(p)
        print(f"{name}: {before[0]}x{before[1]} → {after[0]}x{after[1]}"
              f"  {os.path.getsize(p)//1024} KB   {info}" + ("  (dry)" if DRY else ""))
    print('原图备份:', RAW)


if __name__ == '__main__':
    main()
