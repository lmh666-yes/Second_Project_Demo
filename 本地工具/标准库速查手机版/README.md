# STM32 标准库速查 · 手机版（APK 工程）

把「标准库速查」（`本地工具\标准库速查`）打包成 Android APK 的工程。
与 `d:\GitHub_Project\面试题手机版-客观题` 同一套方案（WebView 套壳 + Gradle 命令行构建，无需 Android Studio）。

## 组成

| 文件 | 说明 |
|---|---|
| `android/` | WebView 套壳工程（包名 `com.stmlookup.app`，应用名「STM32速查」，minSdk 24 = Android 7.0+） |
| `build_data.py` | 用最新数据重新打包手机版网页，并同步进 Android 资源（会先调用速查目录的 `build_mobile.py`） |
| `构建APK.bat` | **一键构建**：打包网页 → Gradle 构建 → 复制 APK 到本目录 |
| `STM32-Lookup.apk` | 构建产物（构建后出现在本目录） |

## 更新数据后重新构建

1. 在 `本地工具\标准库速查` 里更新数据（或重跑 `gen_full_data.py`）；
2. 回到本目录，**双击 `构建APK.bat`**（自动完成三步，约 1-3 分钟）；
3. 把新的 `STM32-Lookup.apk` 发到手机覆盖安装即可。

## 安装（手机）

APK 传到手机（微信 / QQ / 数据线 / 网盘）→ 点击文件 → 按提示允许「安装未知来源应用」→
桌面出现「**STM32速查**」图标，离线可用（数据已全部打包进应用）。

> 与「面试刷题」App 互不冲突（不同包名，可同时安装）。

## 手机端专属改造（与网页版的区别）

手机页由 `标准库速查-手机版.html` 打包而来。构建时 `build_mobile.py` 会给 `<html>` 打上
`data-platform="mobile"` 并注入 `window.__MOBILE__`，页面据此启用手机专属形态；
**网页版不带这个标记，完全不受影响。**

- **去掉桌面专属**：手机没有键盘，所有键盘/快捷键提醒一律不出现——
  ① 快捷键帮助面板与 `?` 按钮直接隐藏；
  ② 图标按钮的 tooltip（`收藏夹（Ctrl+B）`、`最近查看（Ctrl+H）`、`切换主题（Ctrl+J）`、`清空（Esc）`）
     由一段脚本统一把括号里的按键提示抹掉（通用正则，不是逐个按钮打补丁）；
  ③ 收藏空态面板原本写着"或按 Ctrl+D"、详情页收藏按钮的 tooltip 也带 `Ctrl+D`、
     主题按钮 tooltip 带 `Ctrl+J` —— 这三处按 `window.__MOBILE__` 条件化，手机版不出这几个字；
  ④ CSS 再加一条通用规则：手机版下所有 `<kbd>` 都不显示。
  （踩过的坑：一开始只隐藏了帮助面板两个元素，别处的提示还在，用户一眼就看出来了。）
- **底部固定导航**：三个板块（资料查询 / 引导教程 / 项目引导）移到屏幕底部，拇指可达，53px 高。
- **热区**：图标按钮 44×44，列表项加高，快捷动作按钮 ≥44px。
- **字号三档**：右上角新增「小 / 中 / 大」按钮，选择记在本机；默认中档。
- **安全区**：底部导航与内容留出 `env(safe-area-inset-bottom)`，全屏手势机不顶到边缘。
- **手感**：去掉点击高亮、滚动惯性、禁止整页 overscroll。

**必须保留**（删了 APK 返回键就失效）：`body.show-detail` 与 `window.goBackToList` ——
`MainActivity.onBackPressed` 靠这两个钩子判断"返回列表"还是"退出应用"。

## 验证

- 打包后可从 APK 内部确认页面已带手机标记（解压看 `assets/index.html` 是否含
  `<html ... data-platform="mobile">` 与 `window.__MOBILE__ = true`）。
- 真机要看的四件事：返回键先回列表再退出、底部导航可点、字号三档、代码复制按钮。

## 环境（本机已就绪，与面试题工程相同）

- JDK 17：`D:\dev-tools\jdk-17.0.20.1+1`
- Android SDK：`%LOCALAPPDATA%\Android\Sdk`（含 API 35）
- Gradle 9.1.0（wrapper 已内置，首次构建用缓存）
