# 本地工具（索引）

本目录是与 `EnvMonitor` 配套的一整套自建工具：起手设计、进度、协议、引脚、RTOS、编码、
无硬件联调，以及一个能当手册用的「STM32 速查」。

**现在整个目录都入库**（源码 / 脚本 / 文档 / 数据与基线 / 已经打包好的 exe + Qt DLL），
目的是**换台电脑 clone 下来就能用**：桌面程序双击即开（运行时不需要装 Qt），
命令行检查只需要一个 Python，改源码重新编译才需要 Qt 与 MinGW。
（中间产物、IDE 状态、本机专属配置不入库，见文末。）

## 目录索引

| 目录 | 是什么 | 入口 |
|---|---|---|
| `工具箱/` | **主力**：一个 Qt 窗口集成下面全部工具（页面：速查 / 起手助手 / RTOS 生成 / 协议工作台 / 五个检查 / 汇总） | 双击 `工具箱\打开工具箱.bat` |
| `工具箱/速查数据/` | 速查页的数据管线（不是工具，是数据）：`dataset.json` + 基线 + 导出/重建脚本 | `工具箱\速查数据\tools\rebuild.bat` |
| `Git上传/` | 一键上传到 GitHub 的独立 Qt 程序（多配置档、仓库状态、.gitignore 助手） | 双击 `Git上传\运行.bat` |
| `标准库速查/` | **网页版**速查（唯一数据源：`标准库函数速查.html` + 17 个 `data_*.js`） | 浏览器打开 `标准库函数速查.html` |
| `标准库速查手机版/` | 手机链：单文件 HTML + Android 套壳工程，产物两个 APK | 装 `STM32-Lookup.apk` |
| `仓库巡检/` | 原有：Qt 复编验证、引脚审计、文风检查 | `最终验证.ps1` / `文风检查.ps1` |
| `一键上传/` | 原有：PowerShell 版一键上传（命令行后备） | `push_to_github.ps1` |
| `面试刷题/` | 原有：题库与刷题软件工程 | 见目录内说明 |
| `归档/` | 已删除入口的备份 zip（可回退，一般不装） | — |
| `keilkilll.bat` · `_sync_flash.py` · `_tft_sch.txt` | 与本工具集无关的散件（Keil 清理脚本 / 板间同步脚本 / 触摸屏原理图 dump），保留原位 | — |

> 更细的说明在各目录自己的 `说明.md` 里，本文件只做索引与依赖说明。

## 依赖矩阵：什么用途需要装什么

| 你只想做这件事 | 需要 |
|---|---|
| 打开工具箱 / Git 上传（双击即用） | **什么都不用装**（Qt 运行库已随 exe 打包在 `build\release\`） |
| 用工具箱里的检查、跑 `--selftest` | **Python 3**（`python` / `py -3` / `python3` 任一在 PATH 即可） |
| 更新速查数据、导 dataset、跑网页引擎回归、`rebuild.bat` | 再要 **Node.js** |
| 改源码后重新编译 Qt 程序 | 再要 **Qt 6.x + MinGW**（`构建.py` 自动探测，见下） |
| 打手机 APK | 再要 **JDK 17 + Android SDK**（工程内已配好，`gradlew` 离线可编） |
| 跑 `仓库巡检\最终验证.ps1` 的 Keil 复编 | 再要 **Keil uVision**（`UV4.exe`） |

**工具链自动探测**：`工具箱\构建.py` 与几个 ps1 脚本都不再写死盘符——
按 `QTDIR` / `QT_ROOT` 环境变量 → 各盘 `\Qt`（`\Qt6`）→ PATH 的顺序找，
找不到会直接告诉你**缺什么、装什么、或设哪个变量**，而不是丢一句"不是内部或外部命令"。
Keil 同理，找 `E:\k5_v5`、`C:\Keil_v5` 等常见位置，可用 `-Keil <路径>` 覆盖。

## 常用命令

```
# 打开：双击  工具箱\打开工具箱.bat        （唯一入口，一个窗口用全部）
# 打开：双击  Git上传\运行.bat

# 重新编译两个 Qt 程序（工具箱 + Git上传）
python 本地工具\工具箱\构建.py

# 速查数据：导出 + 基线 + 重编 + 自检 + 手机版/APK + 全链同步校验（一条命令）
本地工具\工具箱\速查数据\tools\rebuild.bat          # 全链
本地工具\工具箱\速查数据\tools\rebuild.bat /nodb    # 跳过打 APK

# 自检
工具箱\程序\嵌入式工具箱\build\release\Toolbox.exe --selftest
工具箱\程序\嵌入式工具箱\build\release\Toolbox.exe --gui-smoke      # GUI 冒烟，建议外面套超时
Git上传\build\release\GitUploader.exe --selftest
```

## 不入库的东西（.gitignore）

- **编译中间产物**：`*.o`、`moc_*`、`Makefile*`、`.qmake.stash`、`__pycache__/`、
  Android 的 `build/`、`.gradle/`、`local.properties`
- **本机专属配置**：`_toolbox.ini`、`_stm32lookup.ini`、`_git_upload.ini`（各自记录窗口几何、
  收藏、账户档等本机状态，不入库）
- **生成产物**：`工具箱/检查/链路仿真/产物/`、IDE 与编辑器状态（`.vscode/`、`.qtcreator/`）
- **备份**：`_参考实现备份/`、`归档/`

> `工具箱/程序/嵌入式工具箱/build/release/` 是**例外**：里面的 exe 与 Qt DLL **要入库**，
> 否则别人 clone 下来还得自己装 Qt 才能打开工具箱。

> **同一个目录里还有 3 个 `lib*.dll`**（`libgcc_s_seh-1.dll` / `libstdc++-6.dll` /
> `libwinpthread-1.dll`，MinGW 运行库）—— 它们和 Qt DLL 同等重要：少一个就会在
> 没装 Qt 的机器上以 `0xC0000135`（找不到 DLL）连窗口都起不来。
> `构建.py` 每次编译成功后会自检 `release\` 的运行库与插件目录、缺的自动补齐，
> 别手工删（2026-10-17 就是因为漏了这 3 个，才只在装了 Qt 的机器上跑得起来）。
