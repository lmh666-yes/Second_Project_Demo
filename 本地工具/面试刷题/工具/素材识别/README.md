# 工具 · 素材识别

> 把一堆"图片 / PDF / docx / md / txt / csv"的素材，一键整理成一份可读的 Markdown，
> 用于快速看清素材内容、给题目分类、写"来源"行。

## 1. `素材识别.py`

```powershell
# 把一个素材文件夹整个识别（图片走 OCR）
python 工具\素材识别.py "d:\github\附加面试题\面试题7"

# 只列清单不 OCR（很快，先看有哪些文件）
python 工具\素材识别.py "d:\github\附加面试题\面试题7" --index

# 指定输出文件
python 面试刷题\工具\素材识别\素材识别.py "素材目录" --out "d:\dev-tools\素材.md"
```

**输出**：默认写到素材目录下 `_识别结果.md`（含"素材清单表 + 每份素材的正文"）。

**识别引擎**

| 类型 | 引擎 | 说明 |
|---|---|---|
| 图片 png/jpg/bmp/webp/tif… | **Windows 自带 OCR**（`Windows.Media.Ocr`，中文简体） | 无需安装任何东西；文字会带多余空格（脚本已自动清理 CJK 间空格），但仍会有错字、断行、下标/全角符号错乱 |
| PDF | pymupdf（fitz） | 有文本层的直接抽；扫描版会提示"需导出图片后 OCR" |
| docx | python-docx | 段落 + 表格 |
| md/txt/csv/代码 | 直接读取（utf-8 / utf-8-sig / gbk 自动尝试） | |

## 2. `ocr_win.ps1`

被 `素材识别.py` 调用的 Windows OCR 辅助脚本。

> ⚠️ **这个文件必须保持纯 ASCII**：PowerShell 5.1 默认按 ANSI(GBK) 读 `.ps1`，
> 里面写中文会导致 `The string is missing the terminator` 之类的语法错误。

单独使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 工具\ocr_win.ps1 -Path "某图片.png"
```

## 3. 使用注意（重要）

1. **OCR 只当索引，转写题库必须人工看图核对**——尤其代码、下划线填空题、下标、数学符号。
   实测：`int (*(*f4)())[10]();` 会被 OCR 成 `int （ * (*f4 0 ） [ 10 ] 0 ，`。
2. OCR 的**阅读顺序**可能把"题号列"和"题目正文"拆开（先出 1. 2. 3. … 再出题干），
   所以看图时按"题号 → 题干"人工对齐。
3. 识别结果文件建议不要提交到 git（素材目录本身也不入库），需要留存就移到自己的笔记目录。
