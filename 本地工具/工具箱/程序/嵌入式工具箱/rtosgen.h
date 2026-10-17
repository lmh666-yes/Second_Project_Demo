#ifndef RTOSGEN_H
#define RTOSGEN_H

/* FreeRTOS 骨架生成逻辑层：
   读移植好的工程的 FreeRTOSConfig.h -> 按任务/资源清单生成 main.c 骨架。
   纯逻辑，不依赖界面，可被命令行自检直接调用。 */

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace RtosGen {

/* FreeRTOSConfig.h 里用到的关键配置 */
struct FwConfig {
    QString                path;      /* 配置文件路径，空 = 未解析 */
    QHash<QString, int>    ints;      /* 关键宏的整数值 */
    bool                   hasAssert = false;
    bool                   ok = false;      /* 是否成功解析到关键项 */
    QStringList            notes;     /* 解析提示（缺项、找不到等） */
};

/* 一个任务 */
struct TaskSpec {
    QString name;                 /* 任务名，如 Sensor（不含 vTask 前缀） */
    QString kind;                 /* 周期 / 事件 / 一次性 */
    int     periodMs   = 1000;    /* 周期任务的周期 */
    int     prio       = 1;       /* 优先级，0 ~ configMAX_PRIORITIES-1 */
    int     stackWords = 256;     /* 栈深度（字） */
    QString duty;                 /* 职责描述，写进 TODO 注释 */
    QStringList links;            /* 关联资源名（对应资源表里的变量名），生成时直接引用 */
    QString apiHint;              /* 补全提示词（函数前缀 / 关键词），生成时从速查取原型注释 */
    int     wcetUs     = 0;       /* 最坏执行时间（微秒）；0 = 未填，可调度性分析用 */
};

/* 一个资源：队列 / 互斥量 / 递归互斥量 / 计数信号量 / 二值信号量 / 软定时器 */
struct ResSpec {
    QString kind;                 /* queue / mutex / recursive / counting / binary / timer */
    QString name;                 /* 变量名，如 s_q_frame */
    QString itemType;             /* 队列项类型 / 定时器回调名；其余可空 */
    int     param = 4;            /* 队列深度 / 计数上限 / 定时器周期(ms) */
    QString note;
};

struct GenOptions {
    QString funcName = QStringLiteral("main");
    bool    useDelayUntil = true; /* 周期任务用 vTaskDelayUntil 固定周期 */
    bool    feedWdg = false;      /* 生成喂狗占位 */
    QString includes;             /* 板级额外 include，一行一个 */
    bool    splitHeader = true;   /* 生成 .h + .c 两件套 */
    QString baseName = QStringLiteral("app_tasks");  /* 文件名基名，用 ASCII */
    bool    crlf = false;         /* 用 CRLF 换行（默认 LF） */
    /* 勾选的流程卡（名称，取自「单片机手册」引导教程板块）。
       生成时按卡片自带圈码序号排序后，把整段可复用代码拼进 .c 的参考区。 */
    QStringList flowPicks;
};

/* 解析 FreeRTOSConfig.h，取关键宏。 */
FwConfig parseConfig(const QString &configHeaderPath);

/* 生成前校验（真正的生成前拦截），返回逐条提示（空 = 无问题）。
   除任务/资源清单外，还要接收"勾选了哪些流程卡"，才能查出跨卡冲突；
   flowPicks 为空时不做卡片相关检查（也不去碰那 4MB 速查数据）。 */
QStringList validate(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
                     const QVector<ResSpec> &res,
                     const QStringList &flowPicks = QStringList());

/* 从一段（流程卡）代码里提取的硬件引用：中断向量、抢占优先级、GPIO 脚位。
   提取前会剥掉注释与字符串，避免注释里提到的名字被当成占用。 */
struct CardRefs {
    QStringList         irqs;          /* 形如 USART1_IRQn / DMA2_Stream0_IRQn */
    QHash<QString, int> irqPreempt;    /* IRQn -> 抢占值；-1 = 写了但不是字面整数 */
    QStringList         pins;          /* 形如 PA9 / PB10 */
    bool                preemptNoChannel = false;  /* 写了抢占值但判不出对应 IRQn */
};
CardRefs extractCardRefs(const QString &code);

/* 跨卡冲突（可测内核）：names 与 refs 一一对应，返回逐条提示（空 = 无冲突）。 */
QStringList flowConflictsFromRefs(const QStringList &names, const QVector<CardRefs> &refs);

/* 跨流程卡冲突检测：按卡片名从速查取可复用代码，提取引用后两两比对。 */
QStringList flowConflicts(const QStringList &flowPicks);

/* 生成前拦截：卡片自己的中断服务函数里调了任务态接口（xQueueSend 等）或内核临界区
   （taskENTER_CRITICAL 等）。规则表与检查器共读同一份：
   工具箱/速查数据/rules/freertos_isr_rules.json；表读不到时本检查静默关闭。 */
QStringList cardIsrApiIssues(const QString &cardName, const QString &code);

/* 周期任务可调度性分析（纯函数）：利用率 U=ΣCi/Ti 对比 RMS 上界 n(2^(1/n)-1)，
   并做最坏响应时间迭代 Ri = Ci + Σ_{j∈hp(i)} ceil(Ri/Tj)·Cj。只给带解释的建议，
   不改任何任务；没有"填了 WCET 的周期任务"时返回空。 */
QStringList scheduleAnalysis(const QVector<TaskSpec> &tasks);

/* 渲染 main.c / .c 骨架（splitHeader 时含 .h，并含 USER CODE 保护区）。 */
QString render(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
               const QVector<ResSpec> &res, const GenOptions &opt,
               const QString &projectName);

/* 渲染配套头文件（句柄 extern、任务参数宏、任务原型、FreeRTOS 包含）。 */
QString renderHeader(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
                     const QVector<ResSpec> &res, const GenOptions &opt,
                     const QString &projectName);

/* 按当前清单给出 FreeRTOSConfig.h 的建议改动片段。无建议时返回空串。 */
QString configSnippet(const FwConfig &cfg, const QVector<TaskSpec> &tasks,
                      const QVector<ResSpec> &res);

/* 把旧文件里 USER CODE 保护区的内容搬进新生成的文本（重生成不覆盖用户代码）。 */
QString mergeUserCode(const QString &generated, const QString &existing);

/* 按需把 LF 转成 CRLF。 */
QString withLineEndings(const QString &text, bool crlf);

/* 一组示例任务（采集 / 处理 / 通信），便于一键填入试用。 */
QVector<TaskSpec> sampleTasks();
QVector<ResSpec>  sampleResources();

/* 一套场景模板：名称 + 说明 + 任务与资源清单。 */
struct ScenarioSpec {
    QString            name;
    QString            desc;
    QVector<TaskSpec>  tasks;
    QVector<ResSpec>   res;
};

/* 内置场景模板（采集端 / 网关端 / 最小入门）。 */
QVector<ScenarioSpec> scenarios();

/* 解析"任务行"文本，便于从起手助手导入。
   约定：一行一个任务，字段用 / | ｜ , ， 或制表符分隔，顺序为
   任务名 / 周期ms（事件填 0） / 优先级 / 栈(字) / 职责。 */
QVector<TaskSpec> parseTaskLines(const QString &text);

/* 按任务顺序串联资源：为相邻任务之间建一条队列，并双向写入两任务的关联资源。
   返回新建的资源（调用方把它们并入资源表）。 */
QVector<ResSpec> chainTasks(QVector<TaskSpec> &tasks);

/* 内置自检：解析真实配置、校验能命中、渲染关键片段齐全。 */
int selftest(QStringList &log, const QString &envMonitorRoot);

} // namespace RtosGen

#endif // RTOSGEN_H
