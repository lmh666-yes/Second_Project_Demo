#ifndef DOCBUILDER_H
#define DOCBUILDER_H

/* 起手式的步骤定义与设计文档渲染。

   步骤（PageSpec）既当界面表单，也当输出章节：一处定义，两处使用。 */

#include <QHash>
#include <QString>
#include <QVector>

struct FieldSpec {
    QString id;      /* 字段唯一键，答案按它存取 */
    QString label;   /* 表单标签与章节小标题 */
    bool    multi;   /* true 用多行输入，false 用单行 */
    QString hint;    /* 输入框内的占位提示 */
    QString demo;    /* 示例答案：照着改比重头想快得多，可一键填入 */
    QString why;     /* 为什么这么写：说明这一格背后的判断标准 */
};

struct PageSpec {
    QString             key;    /* 步骤键，与字段 id 共同组成答案键 */
    QString             title;  /* 步骤名与章节标题 */
    QString             teach;  /* 本步的"为什么这样做"提示 */
    QVector<FieldSpec>  fields;
};

/* 默认的起手式步骤（8 步）。 */
QVector<PageSpec> defaultSpecs();

/* 由项目名与答案渲染一份 Markdown 设计文档骨架。 */
QString renderMarkdown(const QString &name, const QHash<QString, QString> &answers);

/* C2：把起手助手的答案喂给「工程模板」生成器。
   peripheralsText = 「外设清单」答案、rtosText = 「是否用 RTOS」答案
   → 返回生成器的 --periph 参数（逗号分隔的外设名）。认不出的不硬塞，一个都没认出时
     回退到最小可跑的一组（tick,led,delay），避免生成空工程。
   tasksText = 「任务表」答案 → 返回 --tasks 参数（Name:prio:stack,...）。
     缺「优先级」或「栈」的行解析不出来，跳过并把原文记进 skipped（让人知道漏了什么）。 */
QString scaffoldPeriphArg(const QString &peripheralsText, const QString &rtosText);
QString scaffoldTasksArg(const QString &tasksText, QStringList *skipped = nullptr);

#endif // DOCBUILDER_H
