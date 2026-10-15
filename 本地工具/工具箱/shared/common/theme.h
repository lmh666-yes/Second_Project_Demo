#ifndef THEME_H
#define THEME_H

/* 统一主题：深色/浅色两套，科技简约风。
   配色集中在这里，界面里不再散落硬编码颜色。
   用法：程序启动时 Theme::apply()；切换调用 Theme::toggle()。 */

#include <QString>

namespace Theme {

enum Mode { Dark = 0, Light = 1 };

/* 读设置并应用到整个应用（qApp->setStyleSheet）。 */
void apply();

/* 指定模式并应用，同时写进设置。 */
void setMode(Mode m);

/* 当前模式（读设置，默认深色）。 */
Mode mode();

/* 在深色与浅色之间切换并应用，返回切换后的模式。 */
Mode toggle();

/* 当前主题名（供界面显示，如"深色"）。 */
QString modeName();

} // namespace Theme

#endif // THEME_H
