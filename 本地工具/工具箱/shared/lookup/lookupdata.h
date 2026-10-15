#ifndef LOOKUPDATA_H
#define LOOKUPDATA_H

/* 给非界面代码（眼下是 RTOS 骨架生成器）用的只读查询层：
   把「STM32 速查」的 dataset.json 当成一本函数手册来查，只回普通类型，不外泄 Dataset/Card。
 *
   数据只读一份：路径走 AppPaths::datasetPath()，与速查页读的是同一个文件；
   本层**惰性装载**，没人调用就不会碰那 4MB（工具箱启动要快）。
   口径与速查页一致：搜索走同一个 SearchEngine，所以查得到的东西两边一样。 */

#include <QString>
#include <QStringList>
#include <QVector>

namespace LookupData {

/* 命中的普通函数条目（资料查询板块） */
struct FuncHit {
    QString name;      /* 名称，如 NVIC_Init */
    QString proto;     /* 原型（p；含括号才会有值） */
    QString desc;      /* 用途（d 的第一句） */
    QString module;    /* 模块分类 m */
};

/* 命中的流程卡（引导教程板块） */
struct FlowHit {
    QString name;      /* 卡片名，如 ① 串口 USART + DMA + 空闲中断接收（不定长帧） */
    QString desc;      /* 用途 */
    QString code;      /* 整段示例代码 e */
};

/* 幂等装载。失败时 *err 给出原因（含路径）；已装载过直接成功。 */
bool    load(QString *err = nullptr);
bool    loaded();
QString path();
int     size();

/* 符号级补全：按关键词 / 前缀查函数条目（与速查页同一套引擎，资料查询板块）。
   只回"带原型（含括号）"的条目，最多 limit 条，按引擎排序。 */
QVector<FuncHit> findFuncs(const QString &query, int limit = 8);

/* 代码段级补全：按关键词查流程卡（引导教程板块），最多 limit 条。
   query 为空 → 按 _ord 给出全部流程卡（界面列清单用）。 */
QVector<FlowHit> findFlows(const QString &query, int limit = 64);

/* 按卡片名精确取一张流程卡；找不到返回 name 为空的结构。 */
FlowHit flowByName(const QString &name);

/* 流程卡的排序键：取名字开头的圈码序号（①~⑳㉑~㉟㊱~㊿），无序号返回 99。
   卡片自带序号，按它拼块就是数据作者排好的"时钟→引脚→外设→NVIC"顺序。 */
int flowRank(const QString &name);

/* 从整段示例代码里抽出**可搬用的部分**：
     - 去掉 #include 行（收集进 includesOut），拼块时不需要它（生成物自己管头文件）；
     - 去掉 main() 整块 —— 39 张流程卡里有 16 张含 main()，直接拼会出现第二个 main；
     - 收集函数定义名（funcsOut），供"记得调用"提示。
   返回剩余代码（已规整空行）。 */
QString extractReusable(const QString &code, QStringList *funcsOut = nullptr,
                        QStringList *includesOut = nullptr);

} // namespace LookupData

#endif // LOOKUPDATA_H
