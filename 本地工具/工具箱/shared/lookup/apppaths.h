#ifndef APPPATHS_H
#define APPPATHS_H

/* 速查页的数据来自「速查数据」目录（网页版是唯一数据源，桌面端只读它导出的
   data/dataset.json）。本页是工具箱里的一个页面，不自带 exe，按工具箱结构定位：

     exe 在   本地工具/工具箱/程序/嵌入式工具箱/
     数据在   本地工具/工具箱/速查数据/data/dataset.json

   2026-10-15：数据管线（dataset.json / 基线 / 配置 / 导出脚本 / 重建链）
   从 `本地工具/STM32速查/` 搬进 `工具箱/速查数据/`，那个同级目录已删除。

   定位顺序：
     1) 从 exe 向上找工具箱根（含「程序」「检查」「shared」）→ 工具箱根/速查数据/
     2) 兜底：工具箱根的上一级里的同级 STM32速查/（搬家前的旧位置，留着以防回退）
     3) 兜底：向上找含 data/dataset.json 的那一层（源码树里直接跑）
   找不到一律返回空，由页面显式报错（不要静默回落到空数据）。 */

#include <QHash>
#include <QString>

namespace AppPaths {

/* 数据根：含 data/dataset.json 的目录；找不到返回空 */
QString toolRoot();
/* 数据文件：数据根/data/dataset.json */
QString datasetPath();
/* D2 代码索引：数据根/data/code_usage.json（code_index.py --usage 生成；
   缺文件时手册页照旧用 dataset.json 自带的 u，不报错） */
QString usagePath();
/* 速查自己的配置（收藏 / 最近 / 板块）：数据根/_stm32lookup.ini */
QString iniPath();

/* D2：手册「本项目使用位置」要把索引里的文件名（basename）拼回绝对路径，需要"标签 → 目录"：
       板1 / 板2        → 该板工程目录（board_envmonitor.json 的 dir）
       板1·库 / 板2·库   → <该板工程目录>/FWLIB/src（驱动源码那一组）
   口径与手册页的进度文档定位一致（仓库根 = 数据根的上三级）；读不到返回空表。
   界面（MainWindow）与 Qt 的 `--selftest`（lookupcli）**共用这一份** ——
   两处各拼一套路径，迟早会有一套悄悄错掉。 */
QHash<QString, QString> boardDirs();

} // namespace AppPaths

#endif // APPPATHS_H
