# 面试刷题 · 工具

> 刷题项目的全部构建 / 维护工具。位置：`面试刷题/工具/`

## 顶层工具

| 工具 | 说明 |
|---|---|
| `build_all_apks.bat` | 三端（客观 / 主观 / 大题）APK 一键 **clean** 重建 → 自动复制到各手机版目录。按脚本位置相对定位；JDK / SDK 路径在最上方两行（换电脑需改） |
| `check_apk.py` | 抽查三个 APK 内嵌 `assets/index.html`：题量（kinds）/ 热点数 / 三段答案标记。按脚本位置相对定位 |

## 素材识别/

| 文件 | 说明 |
|---|---|
| `素材识别.py` | 识别素材：图片 → Windows OCR、PDF → pymupdf、docx → python-docx，输出 `_识别结果.md` |
| `ocr_win.ps1` | Windows 自带 OCR 的 PowerShell 封装 |
| `README.md` | 用法示例 |

## 题库批处理/（历史批次 + 常用工具；脚本内路径已统一为本机新结构）

| 类别 | 脚本 |
|---|---|
| 改造规范 | `改造规范-主观题.md`、`改造规范-大题.md`（执行前必读） |
| 批次数据 | `batch_A~K_data.py`（主观 40 题/批）、`batch_big_1~4_data.py`（大题） |
| 应用脚本 | `apply_batch.py`、`apply_big_batch.py`、`migrate_batch1.py`、`migrate_lib.py` |
| 新题数据 | `new_big_506_515.py`、`new_big_516_526.py`、`new_2026_2p0_q3.py`（附加面试题2.0 批次） |
| 插入脚本 | `insert_big_506_515.py`、`insert_big_516_526.py`、`append_obj_295_314.py` |
| 校验脚本 | `verify_*.py`（代码 gcc 编译 + 运行验证）、`check_*.py`、`_*.py`（批次自检） |
| 热点 | `hot_pick.py`（前 50% 打分）、`mark_hot.py`（写入 `**热点：** 是` 行，幂等） |
| 统计 / 导出 | `analyze_banks.py`、`count_fb.py`、`dump_more.py`、`dump_samples.py`、`dump_subjective.py`、`peek_tails.py` |
| 生成 | `gen_c_506_515.py` |

> 注：这些脚本内含本机绝对路径（已统一为 `D:\github\面试刷题\...`）；换机器需全局替换。
