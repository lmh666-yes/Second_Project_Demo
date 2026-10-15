#ifndef LOOKUPCLI_H
#define LOOKUPCLI_H

#include <QStringList>

class MainWindow;

/* 速查页的命令行出口（不开界面也能跑，便于脚本化与自检）。
   并入工具箱后由 Toolbox 的 main.cpp 调用，所以参数名与原来一致：

     --selftest                 数据装载 + 计数 + 搜索基线逐行回归
     --where                    只打印数据文件位置与条目总数
     --search [--sec ref|guide|build] [词...]
     --explain [--sec ...] 词   打分细则
     --topics                   主题归类逐条输出

   返回 -1 表示"这些参数与速查无关"，交回调用方继续判定；否则返回退出码。 */
int runLookupCli(const QStringList &args, MainWindow *w);

#endif // LOOKUPCLI_H
