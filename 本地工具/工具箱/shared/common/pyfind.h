#ifndef PYFIND_H
#define PYFIND_H

/* Python 解释器探测（工具箱各页面共用一份，避免重复实现）。
   顺序：python（PATH 通用名）→ py -3（Windows 官方启动器）→ python3。
   原先这份逻辑只写在 toolrunpage.cpp 里，体检面板也要用，故上移到 common。 */

#include <QStringList>

namespace PyFind {

/* 返回 [程序名, 前置参数...]；三种都不行时返回空列表。 */
QStringList detect();

} // namespace PyFind

#endif // PYFIND_H
