# Qt 上位机设计文档

> 工程 : S_N_sys/QT_project/SerialMonitor（Qt 6.11.2 · MinGW 64-bit · qmake · C++17）
> 状态 : 已实现并编译通过（协议、帧解析、6 项界面、CSV、告警、自检全部落地；见第 11 节）
> 更新 : 2026-10-10（实现完成）／2026-10-09（设计评审版）
> 依据 : 《项目架构思路设计参考/方案13.md》《数据传输端设计.md》第 5 节

---

## 0. 文档修订记录（2026-10-09）

| # | 项 | 原 → 新 | 说明 |
|---|---|---|---|
| 1 | 解析换算 | 温度/湿度"÷10" → ÷100；气压 ÷10 不变 | 与板1 协议精度升级同步（见《检测数据端设计.md》5.2）。三端换算必须一致，改错时 25.30 ℃ 会显示成 2.53 ℃ |
| 2 | 数据记录/导出 | "规划中" → 列入本期必做（第 6 节） | CSV 导出是数据可追溯的直接证据 |
| 3 | 阈值告警 | "规划中" → 列入本期必做（第 7 节） | 告警是监护系统的组成部分，只有显示没有告警 |
| 4 | 主机端自检 | 无 → 补第 8 节一致性测试模式 | 用软件证明三端协议一致 |
| 5 | 联动脚本 | 无 → 第 9 节无硬件演示流程 | 流程固定，避免现场漏步 |
| 6 | 多节点预留 | 无 → 第 4.4 节 | 为将来多实验室组网留位置，避免界面结构推翻重做 |

---

## 1. 设计定位

电脑端监控软件：通过串口接收边缘网关（板2）转发的环境数据，
实时显示数值与历史曲线，供实验室管理人员查看精密环境状态。

- 职责一 : 串口通信（打开 / 关闭 / 收发 / 日志显示）
- 职责二 : 环境数据实时显示（数值标签 / 曲线）
- 职责三 : 数据记录与导出（CSV）
- 职责四 : 阈值告警提示 + 告警记录
- 职责五 : 协议一致性自检（三端协议的参考实现）

## 2. 现有基础（SerialMonitor 工程现状）

> 以下为工程中已实现的代码，可直接继承；代码内注释按【基础部分】【格外功能】【出现问题的解决部分】标记。

| 功能 | 状态 | 位置 |
|---|---|---|
| 串口扫描 / 打开 / 关闭 | 已有 | mainwindow.cpp on_btnOpenSerial_clicked |
| 文本 / HEX 发送与接收显示 | 已有 | on_btnSend_clicked / onSerialReadyRead |
| 接收时间戳、清空 | 已有 | onSerialReadyRead / on_btnClearRecv |
| 粘包缓冲（按 \r\n 切行） | 已有 | m_serialBuffer 循环切割 |
| TEMP / HUMI 文本解析 | 已有 | onSerialReadyRead 内解析 |
| 双曲线实时绘制（温度 / 湿度） | 已有 | QCustomPlot，滑动窗口 100 点 |
| 拖拽 / 滚轮缩放 | 已有 | setInteractions |

> "按 `\r\n` 切行"的粘包缓冲与 `TEMP:` 文本解析在二进制帧协议下作废，改为第 4 节的环形缓冲区加状态机。
> 旧代码保留在工程里作对照，不再维护。

## 3. 界面规划

> 现状：左侧串口配置 + 发送区，右侧接收区 + 曲线（splitter 布局 150 / 850）。
> 目标：左侧配置 / 中间数值仪表盘 + 多曲线 / 右侧日志与告警。

| 区域 | 内容 | 状态 |
|---|---|---|
| 左侧面板 | 串口选择 / 波特率 / 打开关闭 / 发送区 | 已有 |
| 中间上区 - 数值 | 温度 / 湿度 / 气压 / 光照 / TVOC / MQ-135（6 项卡片） | 待扩建（现仅 2 项） |
| 中间下区 - 曲线 | 多参数曲线（可勾选显示项） | 待扩建（现 2 条） |
| 右侧上区 - 日志 | 原始数据 / 解析日志（含 CRC 错误统计） | 已有（接收区） |
| 右侧下区 - 告警 | 超标条目 + 时间 + 恢复时间 | 待新增（第 7 节） |
| 底部状态栏 | 连接状态 / 接收计数 / 已收帧数 / CRC 错误数 | 部分已有 |
| 菜单栏 | 导出 CSV / 开始停止记录 / 一致性自检 | 待新增 |

### 3.1 窗口标题 / 图标

- 标题 : `精密仪器实验室环境监护系统 — 上位机 v1.0`
- 图标 : 用 `QIcon` + `QPainter` 自绘仪表盘或叶形图标，不依赖外部素材文件

## 4. 通信协议对接（已定案：二进制帧）

> 数据源：板2 透传板1 的完整二进制帧（见《数据传输端设计.md》第 5 节）；Qt 端按同一帧格式解析，原文本行方案（`TEMP:...`）作废。

### 4.1 帧解析状态机

串口数据是字节流，可能一次收到半个包，也可能一次收到三个包，因此用环形缓冲区（Ring Buffer）加状态机处理：

| 步 | 动作 | 说明 |
|---|---|---|
| 1 | 找帧头 | 扫描 `0xAA 0x55` |
| 2 | 读长度 | 1 字节（环境帧固定 `0x0C` = 12）；不等于 0x0C 则丢一个字节重新找帧头 |
| 3 | 收数据 | 继续收满 `长度 + 2(CRC) + 2(帧尾)` 字节 |
| 4 | 校验 | CRC16-MODBUS（范围 = 命令 + 长度 + 数据）；失败则丢一个字节重新找帧头，不丢整包 |
| 5 | 验帧尾 | 必须为 `0x55 0xAA` |
| 6 | 提取 | 按 4.2 节偏移解析各字段 |

> 第 4 步只丢一个字节：如果校验失败就跳过整帧长度，遇到 `...55 AA AA 55...` 这种帧尾与帧头粘连的情况会连续丢帧。

### 4.2 字段解析对照（帧内偏移 · 整帧 20 字节）

| 字段 | 帧内偏移 | 类型 | 换算（与板1 一致） |
|---|---|---|---|
| 帧头 | 0 | 2B | `0xAA 0x55` |
| 命令 | 2 | 1B | `0x01` |
| 长度 | 3 | 1B | `0x0C` |
| 温度 | 4 | int16（大端） | 有符号，÷100 → ℃ |
| 湿度 | 6 | uint16（大端） | ÷100 → %RH |
| 气压 | 8 | uint16（大端） | ÷10 → hPa（气压不能用 ×100，原因见板1 文档 5.2） |
| 光照 | 10 | uint16（大端） | 原值 → lx |
| TVOC | 12 | uint16（大端） | 原值 → ppb |
| MQ-135 | 14 | uint16（大端） | ADC 原始值 |
| CRC16 | 16 | 2B | CRC16-MODBUS，低字节在前 |
| 帧尾 | 18 | 2B | `0x55 0xAA` |

```cpp
// C++ 解析示例
int16_t  t  = (int16_t)((data[4] << 8) | data[5]);   double temp = t / 100.0;  // ℃
uint16_t h  = (uint16_t)((data[6] << 8) | data[7]);   double humi = h / 100.0;  // %RH
uint16_t p  = (uint16_t)((data[8] << 8) | data[9]);   double pres = p / 10.0;   // hPa
uint16_t lx = (uint16_t)((data[10] << 8) | data[11]);                            // lx
uint16_t tv = (uint16_t)((data[12] << 8) | data[13]);                            // ppb
uint16_t mq = (uint16_t)((data[14] << 8) | data[15]);                            // raw
```

> 有符号验证 : -5.00 ℃ → -500 → `0xFE0C`。测试用例必须包含一个负温度。

### 4.3 数据项显示规划

| 数据项 | 单位 | 显示方式 | 小数位 |
|---|---|---|---|
| 温度 | ℃ | 数值 + 曲线 | 2 |
| 湿度 | %RH | 数值 + 曲线 | 2 |
| 气压 | hPa | 数值 + 曲线 | 1 |
| 光照 | lx | 数值 + 曲线 | 0 |
| TVOC | ppb | 数值 + 曲线 | 0 |
| MQ-135 | 原始值 | 数值（曲线可选） | 0 |

### 4.4 多节点预留

将来做多实验室组网时，帧里会多一个节点 ID。为此预留：

- 数据结构从一开始就带 `quint8 nodeId`（当前固定填 `0x01`）
- 图表按 `nodeId` 分组，当前只画 `0x01`
- 状态栏留一个"节点数"位

## 5. 代码结构规划

> 现状：单窗口双文件（mainwindow.cpp / .h + qcustomplot 绘图库）。功能增加后按模块拆分，避免 mainwindow.cpp 膨胀。

| 模块 | 职责 | 状态 |
|---|---|---|
| mainwindow | 界面组织 / 事件分发 | 已有 |
| FrameParser | 环形缓冲区 + 帧状态机 + CRC16 校验 | 必做（协议已定案） |
| DataModel | 最新值 + 历史环形数组 + CSV 记录（`QVector<Sample>`） | 必做（第 6 节） |
| AlarmManager | 阈值比较 / 迟滞 / 告警记录（第 7 节） | 必做 |
| SerialManager（可选） | 串口开关 / 收发缓冲 | 可选（现有代码够用） |
| ConsistencyTest | 一致性自检模式（第 8 节） | 建议 |

### 5.1 绘图性能要点

| 要点 | 做法 |
|---|---|
| 关闭抗锯齿 | 初始化时 `ui->widgetPlot->setNotAntialiasedElements(QCP::aeAll);`（曲线与点数多时提升刷新速度） |
| 批量刷新 | 每来一个点不立即 `replot()`；`QTimer` 每 50 ms 批量刷新一次，新点先入数据数组 |
| 滑动窗口 | 沿用 100 点窗口（或按需扩大），限制数据量与绘制量 |
| 6 条曲线 | 温度/湿度用左轴，其余用右轴或独立子图，避免量纲差异（气压 1013 与光照 500）把曲线压平 |

## 6. 数据记录 / 导出 CSV

| 项 | 方案 |
|---|---|
| 记录时机 | 每解析出一帧且 CRC 通过，追加一条 |
| 字段顺序 | `时间,温度,湿度,气压,光照,TVOC,MQ-135,节点ID,CRC状态,数据来源`（表头由 `datamodel.cpp` 的 `CsvRecorder::header()` 单点生成，解析脚本请按列名取值，不要按列号） |
| 数据来源取值 | `实时`（CMD 0x01 实时帧）/ `补传`（CMD 0x15 历史帧，按原始时间戳插入曲线，不参与实时告警与链路统计） |
| 状态列取值 | `OK` / `ERR`（CRC 不过）；若本帧有超出物理合理范围的项，追加 `+RANGE`（如 `OK+RANGE`），数据照记，只标记该值可疑（见第 12 节） |
| 时间格式 | `yyyy-MM-dd HH:mm:ss.zzz`（毫秒便于算延迟） |
| 写入方式 | `QFile` + `QTextStream`，UTF-8；每帧 flush 一次或按 1 s 定时 flush，减少崩溃时的数据损失 |
| 导出 | 菜单"导出 CSV"→ `QFileDialog::getSaveFileName`，把内存记录或当前文件另存 |
| 可选 | 定时自动分卷（每小时一个文件）、超行数滚动 |
| Excel 兼容 | 写 UTF-8 BOM，否则 Excel 打开中文表头乱码 |

## 7. 阈值告警

### 7.1 双阈值迟滞

单阈值会在临界点反复触发：数据在阈值上下抖动时告警状态来回切换。因此每侧设进入与退出两个阈值：

| 参数 | 含义 | 示例（温度） |
|---|---|---|
| 上限进入 | 超过它才算超标 | 27.0 ℃ |
| 上限退出 | 低于它才算恢复（留 0.5 回差） | 26.5 ℃ |
| 下限进入 | 低于它才算超标 | 18.0 ℃ |
| 下限退出 | 高于它才算恢复 | 18.5 ℃ |

状态机：`正常 ←→ 告警中`，只在跨越进入/退出阈值时切换状态，阈值区间内的抖动不改变状态。

### 7.2 告警记录

| 项 | 说明 |
|---|---|
| 告警表 | 参数名 / 触发值 / 进入时间 / 恢复时间 / 持续时长 |
| 视觉 | 对应数值卡片变红 + 状态栏提示；恢复后变回绿色并在表里补上恢复时间 |
| 可选 | 声音提示（`QApplication::beep()`）、桌面通知 |
| 导出 | 与 CSV 一起导出，或单独导出告警表 |

> 与下位机分工：板1 蜂鸣器做本地告警，Qt 做远程告警与记录，两级告警。

## 8. 一致性自检模式

目的：用软件证明"板1 / 板2 / Qt 三端协议一致"。

| 模式 | 做法 |
|---|---|
| 回放模式 | 读入一个 `.hex` / `.txt` 的原始帧文件（板1 抓包导出），喂给 `FrameParser::feed()` |
| 输出 | 解析结果表（每帧的 6 个物理量 + CRC 状态），可导出 CSV |
| 比对 | 与预期值逐字段比对，输出差异表；差异为 0 才算通过 |
| 边界用例 | 必须包含：负温度、CRC 故意改错、帧尾与帧头粘连（`...55 AA AA 55...`）、半帧、连续三帧 |
| 用途 | 联调时定位是哪一端算错 |

## 9. 无硬件演示流程

没有实物模块时，按以下步骤用"演示帧"菜单验证界面与解析链路：

| 步 | 动作 | 验证点 |
|---|---|---|
| 1 | 上电，Qt 自动连接串口 | 串口自动扫描 + 断线重连提示 |
| 2 | 观察 6 项数值与曲线实时刷新 | 实时性 |
| 3 | 对着 SHT30 哈气 / 用热风枪远吹 | 温度曲线响应 |
| 4 | 用手遮住 BH1750 | 光照曲线骤降 |
| 5 | 触发阈值（遮挡/加热） | 数值卡片变红 + 板1 蜂鸣器响（两级告警） |
| 6 | 拔掉板1 的 LoRa 天线 → 插回 | 板2 统计 CRC/丢帧，恢复后自动继续 |
| 7 | 拔掉路由器电源 → 恢复 | 板2 断网缓存 + 补传（第 6 节） |
| 8 | 导出 CSV 并打开 | 数据可追溯 |
| 9 | 运行一致性自检 | 协议一致性可验证 |

## 10. 待办清单

- [x] 与网关端定案传输协议（二进制帧 + 精度升级版 · 2026-10-09，见第 4 节）
- [x] 实现环形缓冲区 + 帧状态机 + CRC16 校验（4.1 / 4.2 节）—— `frameparser.cpp`；校验失败只丢 1 个字节；无 GUI 测试 8/8 通过
- [x] 界面扩建 : 6 项数值卡片 + 多曲线勾选（第 3 节）—— `mainwindow.ui/.cpp`，6 张卡片 + 6 条曲线勾选
- [x] 绘图性能优化 : 关闭抗锯齿 + 50 ms 批量刷新 + 量纲分组（5.1 节）—— 6 根独立 Y 轴
- [x] 窗口标题 / 图标 —— 标题已改；图标仍是 Qt 默认（没有 .ico 资源）
- [x] 数据记录 / 导出 CSV（第 6 节）—— `datamodel.cpp` 的 `CsvRecorder`（每行 flush）
- [x] 阈值告警：双阈值迟滞 + 告警记录表（第 7 节）—— `alarmmanager.cpp`
- [x] 一致性自检模式 + 边界用例（第 8 节）—— `consistencytest.cpp`，8 个内置用例 + 回放模式
- [x] 数据上下限可调 + 落盘 + 边界（量程）校验（第 12 节）—— `thresholddialog.cpp` + `physrange.h`；6 项阈值全部可在界面改（含"启用哪一侧"），写 `上位机配置.ini`，启动读回；超物理范围的值标黄 + 计数 + CSV 记 `+RANGE`，不参与告警判定
- [x] 上位机双向控制：往下发命令（第 13 节）—— 串口改 `ReadWrite`、`devctl.cpp`（单条在途 + 600 ms×3 重发）+ 左栏"3·设备控制"面板（转发/缓存开关、查询、清 Flash、软复位）；`--cmdselftest` 16/16
- [ ] 与网关联调 → 全链路联调（板1 → 板2 → 上位机）—— 板2 转发代码已补（见第 11 节），等实物到货
- [ ] 按第 9 节流程走通一遍
- [x] 发布打包（`windeployqt`）—— `windeployqt --release --no-translations`，`build_verify\release\` 共 65.4 MB（含 `platforms/qwindows.dll` 等），目标机不装 Qt 也能运行；实测把 Qt 从 PATH 剔除后启动正常

---

## 11. 实现记录（2026-10-10 · 已实现并编译通过）

> 本节记录实际实现与设计文档的差异，以及实现中遇到的问题。与第 3~9 节冲突时，以本节和源码为准。

### 11.1 代码结构（全部在 `S_N_sys/QT_project/SerialMonitor/`）

| 文件 | 职责 | 关键内容 |
|---|---|---|
| `frameparser.h/.cpp` | 协议层（不依赖 GUI） | `namespace FrameProto`：帧常量、`crc16Modbus()`、`verify()`、`buildEnv()`、`build(cmd, payload)`（组任意帧，含控制帧）、`cmdName()/statusName()/isAck()`、控制帧常量（`CMD_SET_FWD 0x10`…`CMD_REBOOT 0x14`、`ACK_FLAG 0x80`、`QA_*` 偏移）；`struct EnvFrame`（字段 + `tempC()/humiRh()/pressHpa()/humiValid()/unpack()`）；`struct AckFrame`（应答帧 + `status()/u8()/u16()/u32()/fwdOn()/cacheOn()/rxOk()…`）；`class FrameParser`：`feed()/takeFrame()/parseAll()/stats()` + `takeAck()/pendingAcks()`，失败只丢 1 个字节后重同步 |
| `datamodel.h/.cpp` | 数据层 | `class DataModel`：历史环形（默认 1800 帧）+ 每项 min/max/avg；`class CsvRecorder`：逐行 flush 的 CSV 记录器 |
| `alarmmanager.h/.cpp` | 告警层 | 双阈值 + 回差迟滞、告警记录表（上限 5000 条）、`evaluate(f, ms, skipMask)` 只返回本次新产生的事件；`skipMask` 里的量本帧不判定（超量程时用） |
| `physrange.h` | 数据边界（header-only） | `namespace PhysRange`：6 个物理量的合理范围 `lo()/hi()/suspect()/text()`——温度 -40~80℃、湿度 0~100%RH、气压 300~1100hPa、光照 0~20000lx、TVOC 0~5000ppb、MQ-135 0~4095 |
| `devctl.h/.cpp` | 下行控制通道（不依赖 GUI） | `class DevCtl`：`attach(serial)/setForward()/setCache()/query()/clearCache()/reboot()/onAck()/tick()`；一条在途命令队列 + 600 ms 超时重发（最多 3 次）；信号 `logText/statusChanged/ackReceived/commandFailed`；`struct DevStatus{valid,fwd,cache,rxOk,rxBad,cacheCount,lost,uptimeS}` |
| `thresholddialog.h/.cpp` | 阈值设置对话框 | 6 项 × 下限/启用/上限/启用/回差 + "合理范围"提示列；"恢复默认值"；校验"下限 ≥ 上限"直接拒绝；单独开对话框是为了不把主界面左栏撑宽（QSplitter 会按 minimumSizeHint 把中间曲线栏挤没） |
| `consistencytest.h/.cpp` | 自检层 | 8 个内置用例（期望字节是独立 Python 算出的向量，不是用 `buildEnv()` 自证）+ 任意数据回放 |
| `mainwindow.ui/.h/.cpp` | 界面层 | 三栏 `QSplitter`：串口/记录与自检/设备控制/解析统计/阈值 **|** 6 卡片 + 6 曲线 + 勾选（含"Y 轴自动量程""量程校验"）**|** 告警记录表；`loadConfig()/saveConfig()` 读写 `上位机配置.ini`；串口 `open(ReadWrite)` 后 `m_dev->attach()`，`readyRead` 里 `takeAck()` → `m_dev->onAck()` |
| `main.cpp` | 入口 | `--selftest` / `--rangecheck` / `--cmdselftest` / `--dumpmin` / `--shot` / `--shotth` |
| `tests/parsertest/` | 无 GUI 测试工程 | 改完解析器立刻能验，不用起窗口；可选参数 = 回放一个抓包文件 |
| `tools/gen_ctrl_vectors.py` | 控制帧向量的独立实现 | 只按协议文字描述的 CRC16-MODBUS 重算控制帧/应答字节，喂给 `--cmdselftest` 当期望值，避免用被测代码自证 |

### 11.2 关键实现决定

1. 每帧给每条曲线都 append 一个点（湿度无效时 append `NaN`）。这样 6 条曲线的下标严格对齐，导出 CSV 时按下标取值不会错位；`NaN` 让 QCustomPlot 断线，而不是画到 0 造成湿度掉到 0 的假象。
2. 一个物理量一根 Y 轴：温度用左轴，其余 5 项各自 `axisRect()->addAxis(QCPAxis::atRight)`，轴颜色 = 曲线颜色，量程默认固定、可切"Y 轴自动量程"（默认固定是为了曲线形状能跨时间对比；勾上自动后按当前可见曲线的数据 min/max ±10% 自适应。板1 气压还没接、恒 0，固定轴是 950~1050 时那条线会落在画面外）。6 个量纲（℃/%/hPa/lx/ppb/ADC）合用一根轴会全被压成直线。
3. 界面刷新靠定时器，不靠数据到达：`readyRead` 只喂解析器，50 ms 定时器统一刷卡片 + 曲线，1 s 定时器刷统计/告警/静默检测。来一帧刷一次，串口突发时会卡界面。
4. 校验失败只丢 1 个字节（不是丢整帧）：`55 AA AA 55` 粘连时丢整帧会连着丢两帧。测试用例 5 专门验这个。
5. 丢帧只能估算：协议数据段里没有序号字段，只能按到达间隔（>1.5×期望间隔）估算丢了几帧。要精确丢包率，需给协议加 2 字节序号（`LEN 0x0C → 0x0E`，三端同步改）。
6. CSV 每行 flush + UTF-8 BOM：1 s 一行，断电最多丢最后一行；带 BOM 是因为 Excel 打开不带 BOM 的 UTF-8 会乱码。
7. 告警用回差迟滞：`35.0℃` 上下抖 `0.1℃` 会刷出几百条告警；必须进阈值才告警、退出阈值±回差才恢复。
8. 静默检测：串口开着 3 s 没收到帧，就在日志里提示检查板2 是否打开了 UART2 转发、波特率与接线。实机联调第一步常见原因就是这里没配好。
9. 超范围的值不丢、也不判（第 12 节）：板1 现在气压/光照/TVOC/MQ-135 是硬编码 0，`0.0 hPa` 会命中"低于下限 980"，一插串口就有两条假告警。做法是照常显示、照记 CSV（记 `+RANGE`），只跳过告警判定；丢掉数据就分不清"传感器没接"和"真读数"。

### 11.3 编译与验证

```powershell
$env:PATH = 'E:\Qt\6.11.2\mingw_64\bin;E:\Qt\Tools\mingw1310_64\bin;' + $env:PATH

# 1) 只验解析器（最快，改完 frameparser.cpp 就跑这个）
cd S_N_sys\QT_project\SerialMonitor\tests\parsertest
E:\Qt\6.11.2\mingw_64\bin\qmake.exe parsertest.pro -o Makefile ; E:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -j4
.\release\parsertest.exe            # 期望：通过 8 / 8，退出码 0

# 2) 完整 GUI
cd S_N_sys\QT_project\SerialMonitor ; mkdir build_verify ; cd build_verify
E:\Qt\6.11.2\mingw_64\bin\qmake.exe ..\SerialMonitor.pro ; E:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -j4
Start-Process .\release\SerialMonitor.exe -ArgumentList '--selftest' -Wait   # 结果写 release\自检报告.txt

# 3) 诊断/留证（都在 exe 里，不开窗就能出证据）
.\release\SerialMonitor.exe --dumpmin           # 窗口最小尺寸 + 三栏实际宽度 → 窗口尺寸诊断.txt
.\release\SerialMonitor.exe --shot 截图.png      # 让界面自己渲染出 PNG（不受屏幕 DPI 缩放影响）
.\release\SerialMonitor.exe --shotth 对话框.png   # 只截"告警阈值设置"对话框（写文档用）
.\release\SerialMonitor.exe --rangecheck        # 数据边界的对照自检（5 组用例）→ 量程校验自检.txt
.\release\SerialMonitor.exe --cmdselftest       # 下行控制帧的字节/解析自检（16 项）→ 下发控制自检.txt
```

> 用 PowerShell 抓图或跑自检一律走 `Start-Process -Wait -PassThru` 再看 `$ExitCode`：
> 直接 `& .\release\SerialMonitor.exe --shot x.png` 实测不生成文件，`$LASTEXITCODE` 还是空的。

- 无硬件也能看界面：菜单 → "演示帧"，用正弦造帧直接喂界面。
- 有硬件联调：板2 把 P6 跳线跳到 SP3232 侧 → USB-TTL 接电脑；波特率 115200 8N1。
- 本次实测记录（2026-10-10）：release 版 `make` exit 0（无 error/warning）；`--selftest` 退出码 0（`通过 8 / 8`、`三端协议一致`）；`--rangecheck` 5 / 5 PASS、退出码 0；`--cmdselftest` `通过 16 / 16`、退出码 0；`--dumpmin` 实测三栏宽度 = `330 / 620 / 350`、窗口最小尺寸 `960x620`；`--shot` 出的整界面截图见 `S_N_sys/QT_project/上位机界面截图.png`（含"3·设备控制"面板，`build_verify\ui_ctrl.png` 为同名副本）；阈值对话框截图见 `S_N_sys/QT_project/阈值设置对话框.png`；配置落盘实测：关窗后生成 `build_verify\release\上位机配置.ini`，手改 `Temp\high=40` + `ui\yAuto=true` + `curves\Press\visible=true` 后重启，界面显示 `40.00`、气压勾上、自动量程勾上（截图 `build_verify\verify_load.png`）；`windeployqt` 后 `release\` 65.4 MB 可独立运行。

### 11.4 常见问题与处理

| 问题 | 现象 | 处理 |
|---|---|---|
| `QCPGraph` 没有 `QVector<QPointF>` 重载 | `no known conversion ... 'QList<QPointF>' to 'double'` | 加 `splitPoints()` 拆成两条 `QVector<double>` 再 `setData/addData` |
| `QCPGraph` 没有 `isVisible()` | `has no member named 'isVisible'` | 用 `QCPLayerable::visible()` |
| 成员函数起名 `emit` | `comma operator has no effect`（Qt 的 `emit` 宏展开成空） | 改名（本项目已改成 `emitText`） |
| 构造顺序错导致双击闪退 | 退出码 `0xC0000005`（访问冲突），没有报错弹窗；gdb 栈指向 `QPlainTextEdit::appendPlainText` | `setupSerialUi()` 末尾会扫串口并 `logLine()`，而当时 `m_hexLog` 还没建（`buildHexLog()` 排在它后面）；把 `buildHexLog()` 提到构造函数第一句，并给 `logLine()` 加空指针判断 |
| GUI 子系统没有控制台 | `--selftest` 的打印看不见 | 结果写 `自检报告.txt`，用退出码判断（0=全通过） |
| 直接双击 exe 报 `0xC0000135` | 找不到 `Qt6Core.dll` | 把 `E:\Qt\6.11.2\mingw_64\bin` 加进 `PATH`；发布前执行 `windeployqt` |
| `qcustomplot.cpp` 自身的弃用警告 | 一批 `QImage::mirrored is deprecated` | 第三方库代码不改（改了不利于后续升级） |
| 外部抓屏"界面少了一块" | 屏幕缩放不是 100% 时，用 PowerShell `CopyFromScreen`/`PrintWindow` 抓出来的图只有左上角一块，看着像右栏被裁掉 | 这是 DPI 虚拟化导致的，不是界面问题；用 `SerialMonitor.exe --shot 截图.png`（`QWidget::grab()`，本进程真实渲染）。三栏外面套了 `QScrollArea`，三栏宽度改到 `showEvent()` 里分配，小屏时出滚动条 |
| `QSplitter::setSizes()` 在构造函数里调 | splitter 还没布局过（宽度是默认值），传进去的比例先被缩放，显示后右栏可能被挤成 0 宽 | 挪到 `showEvent()` 里，第一次真正显示后再分配 |
| `QSettings::setIniCodec()` 在 Qt 6 被移除 | `'class QSettings' has no member named 'setIniCodec'`，编译不过 | 删掉该调用（Qt 6 的 ini 固定 UTF-8，中文键值正常）；只在需要兼容 Qt 5 时才写这个 |
| 阈值 spinbox 用 `setEnabled(false)` 表示"这一侧不判" | 用户以为界面坏了或改不动；12 个勾选框塞进 330 px 左栏会把中间曲线栏挤没 | 改为 spinbox 恒可编辑（改了就自动启用该侧）+ 单独一个设置对话框管启用与回差 |
| 串口 `open(QIODevice::ReadOnly)` | 界面看起来一切正常，`m_serial->write()` 静默失败（Qt 返回 -1，不抛异常），"上位机发不出命令"且没有提示 | 改 `ReadWrite`；`DevCtl` 在下发前先判 `isWritable()`，失败就 emit `logText("串口未打开…")` |
| 设备状态回显与用户操作互相触发 | 收到 QUERY 应答后 `setChecked(dev.fwd)` 会再触发一次 `toggled(bool)` → 又发一帧 SET_FWD，设备回、又 setChecked，如此循环 | 回显时用 `QSignalBlocker` 挡住信号；只有用户点击才下发 |
| 直接 `& exe --shot x.png` 抓图不落文件 | `$LASTEXITCODE` 为空、目标文件不存在，看着像程序崩了 | 用 `Start-Process -FilePath ... -ArgumentList '--shot',$out -Wait -PassThru`，再看 `$p.ExitCode` |
| edit 工具改 GUI 代码时锚点过短 | 一次替换把 `buildHexLog()` 里的 `m_hexLog = new QPlainTextEdit(ui->statsBox);` 连带删掉（编译能过、运行必崩） | 改完立刻 `git diff` 或重读该段；锚点带上足够上下文 |
| 命令行编板子的 Keil 用错入口 | `E:\k5\UV4\UV4.exe -b …` 报 `C9555E: Failed to check out a license` + 86 个 Error，看着像代码全崩了 | 那 86 个错全是假错（一个文件都没编成）。换 `E:\k5_v5\UV4\UV4.exe`（自带 AC5 许可）→ 0 Error / 0 Warning。诊断依据：错误数接近源文件数时先怀疑工具链或许可，而不是代码 |

### 11.5 板2 侧配套改动

`S_N_sys/Project_2_数据通信端/Project/main.c`：

第一轮（转发链路，已复编通过）

- 新增宏：`#define USART_QT SYS_USART_2` / `#define USART_QT_BAUD 115200U`（同一对脚 PA2/PA3，由板上 P6 跳线选 SP3485 或 SP3232，代码不用改）；
- `main()` 里 `SYS_USART_Init(USART_QT, USART_QT_BAUD);`——不初始化上位机就收不到数据，这是联调第一个检查点；
- `vTaskForward()` 里加 `SYS_USART_SendBuf(USART_QT, f, FRAME_TOTAL);`——转发原始 20 字节帧不做任何加工（CRC/帧尾都在里面，上位机用同一套状态机自己解）；
- 监控任务打印加上 `Qt 转发 %u 帧`，用于确认转发是否在工作。

第二轮（下行控制通道，代码已写完，本轮未编译）

- `main()` 的 `SYS_USART_Init(USART_QT, …)` → `SYS_USART_InitRxIT(USART_QT, …)`：要收上位机的命令帧必须开 `RXNE` 中断（`USART2_IRQHandler` 在 `sys_usart.c` 里是 `__weak`，工程 `stm32f4xx_it.c` 没有同名强定义，直接生效）；
- 新增 `static void vTaskCmdRx(void *pv)`（20 ms 轮询 `SYS_FRAME_Poll(USART_QT)` → `SYS_FRAME_Get()` → 按命令处理 → `SYS_FRAME_Send(USART_QT, cmd|0x80, …)` 回应答）；未知命令回状态 2；`REBOOT` 是先回应答、`SYS_USART_FlushTx()` 等发完、再 `NVIC_SystemReset()`；
- 新增两个运行期开关 `s_fwd_on` / `s_cache_on`（`vTaskForward()` 里据此决定发不发串口、写不写 Flash），初始都为 1（上电行为与改之前一致）；
- 发送互斥：`vTaskForward()`（转发）与 `vTaskCmdRx()`（应答）都会往 USART2 写，用 `s_qt_tx_mtx = xSemaphoreCreateMutex()` 把每次发帧包起来；两个任务的字节交错会拼出半帧，上位机解析器只会把它当噪声丢掉；
- `APP_TASK_COUNT` 2 → 3（看门狗心跳位要跟着任务数走）。

> 已复编通过（2026-10-10）：正确入口是 `E:\k5_v5\UV4\UV4.exe`（MDK 5.24a，自带 ARMCC AC5 许可）：
> `& 'E:\k5_v5\UV4\UV4.exe' -r -j0 -o 日志.txt 'D:\…\Project_2_数据通信端\Project\000标准模板库.uvprojx'`
> → 退出码 0、`0 Error(s), 0 Warning(s)`、`Program Size: Code=37420 RO-data=2340 RW-data=384 ZI-data=36344`
> （第一轮转发版是 `Code=34884 RO=2252 RW=372 ZI=35492`，控制通道约 +2.5 KB 代码）。
> 同一把 UV4 对板1 工程 `Project_1_检测端\Project\标准模板.uvprojx` 也是 0 Error / 0 Warning（`Code=28860`）。
> `E:\k5\UV4\UV4.exe`（MDK 5.43a）的 AC5 许可拉不起来（`C9555E: Failed to check out a license`，86 个 Error 全是假错）；`E:\k5\Backup.001\UV4\UV4.exe` 报"Default Compiler Version 5 不可用"；只有 `E:\k5_v5` 能编。
> `main.c` 是 GBK(936) 编码：用记事本等编辑器改中文注释可能把整文件变成乱码，改完确认中文仍正常显示。

### 11.6 未完成项（按优先级）

1. 实机联调（等 LoRa 到货）：板1 → 板2 → 上位机整链路；
2. `windeployqt` 打包已做完（见第 10 节最后一条），产物在 `build_verify\release\`；
3. 给协议加 2 字节序号，才能算真丢包率（三端同步改）；
4. 按第 9 节流程走一遍并留下截图/GIF；
5. 窗口图标（需要 `.ico` 资源）；
6. 板2 第二轮改动复编已通过（`E:\k5_v5\UV4\UV4.exe`，0 Error / 0 Warning，见 11.5）；
7. 板2 的断网缓存目前只有存、没有补传（`W25QXX_LogRead()` 只在开机自检里被调用一次）；上位机的"清 Flash 缓存/缓存条数"能看到有多少条，但没有任务把它们发出去。

---

## 12. 数据上下限（阈值）与"边界溢出"的处理（2026-10-10 补）

### 12.1 三端各管一段

| 段 | 现在怎么处理边界 | 证据 |
|---|---|---|
| 板1（检测端） | `frame_build()` 打包时不限幅、不四舍五入（截断）：`put_u16_be(&f[FRAME_TEMP_OFF], (uint16_t)(int16_t)(temp_c * 100.0f));`。传感器读失败时 `vTaskSensor()` 连续 3 次失败 → `m.valid = 0`，`vTaskProcess()` 跳过组帧（不会把 0.0℃ 当真数据发出去）。气压/光照/TVOC/MQ-135 目前硬编码 0 | `Project_1_检测端\Project\main.c:794-815`、`:849-927` |
| 板2（数据通信端） | 只转发原始 20 字节，不改载荷；Flash 缓存的环形指针到顶就覆盖最旧（`if (s_count < total) s_count++;`），整扇区擦除连带丢掉的条数单独记在 `s_lost`（只增，`SYS_..._Lost()` 可读） | `Project_2_数据通信端\Project\main.c` 的 `vTaskForward()`；`w25qxx_log.c:345`/`:382` |
| Qt（上位机） | `PhysRange` 判物理量合理范围；超范围照收、照显示、照记 CSV（状态列 `+RANGE`），只是进 `skipMask` 不参与告警判定。解析层只校验长度/CRC，不校验物理量范围（那是上层的事） | `physrange.h`、`alarmmanager.cpp` 的 `evaluate(f, ms, skipMask)` |

### 12.2 合理范围（`physrange.h`）

| 量 | 合理范围 | 量 | 合理范围 |
|---|---|---|---|
| 温度 | `-40.00 ~ 80.00 ℃` | TVOC | `0 ~ 5000 ppb` |
| 湿度 | `0.00 ~ 100.00 %RH` | MQ-135 | `0 ~ 4095 ADC` |
| 气压 | `300.0 ~ 1100.0 hPa` | 光照 | `0 ~ 20000 lx` |

> `suspect()` 对 `NaN/±Inf` 返回 `false`；湿度无效是另一套逻辑（`humiValid()`，画断线），两者不混用。

### 12.3 出厂默认阈值（可在界面改，改完自动落盘）

| 量 | 下限 | 上限 | 回差 | 出厂策略 |
|---|---|---|---|---|
| 温度 | 5 ℃ | 35 ℃ | 1 | 两侧都判 |
| 湿度 | 20 %RH | 80 %RH | 2 | 两侧都判 |
| 气压 | 980 hPa | 1030 hPa | 1 | 两侧都判 |
| 光照 | 50 lx | 停用 | 20 | 只判"太暗"（灯光下 0 lx 是正常值，判上限会误报） |
| TVOC | 停用 | 1000 ppb | 50 | 只判"超标" |
| MQ-135 | 停用 | 2000 ADC | 100 | 只判"超标" |

三个入口都能改：1) 左栏"5·告警阈值"表格直接改（改哪侧就自动启用该侧）；2) "阈值设置…" → `ThresholdDialog`（多出"启用某一侧"勾选框 + "合理范围"提示列 + "恢复默认值"）；3) 手改 `上位机配置.ini`（exe 同目录，关窗自动存、启动自动读）。

### 12.4 边界溢出分三类

1. 板1 定点打包溢出（`temp_c × 100` 超出 `int16` ±327.67 ℃；湿度超 655.35 %RH；气压超 6553.5 hPa）：目前没有限幅，回绕成负值（400 ℃ → `(uint16_t)(int16_t)40000` = -25536 → 解出 -255.36 ℃）。Qt 侧能靠 `PhysRange` 判出可疑并拦掉假告警，但真值已经丢了。→ 待办：在 `frame_build()` 入口 clamp 到物理范围（本次按只动 Qt 的口径没改单片机）。
2. Qt 统计累加溢出：`m_rangeSuspect[6]` / `m_rangeSuspectTotal` 用 `quint64`（1 帧/秒也要跑几千年才满）；历史数据是固定容量环形缓冲（默认 1800 帧）。
3. 告警记录表满（`kMaxRecords = 5000`）：满了丢最旧、保最新，表格里始终能看到最近 5000 条，不会因为满了就不记。

### 12.5 验证方法

`SerialMonitor.exe --rangecheck` = 5 组对照用例（结果写 `量程校验自检.txt`，退出码 0/1）：

| 用例 | 期望 |
|---|---|
| 1) 板1 现状帧（气压/光照/TVOC/MQ-135 = 0）+ 量程校验关 | 2 条告警（气压低于下限、光照低于下限）——这就是不改就会看到的假告警 |
| 2) 同一帧 + 量程校验开 | 1 条（只剩"光照低于下限"）——0 lx 是物理合理值，量程校验挡不住它，要么板1 接上光照传感器，要么把光照下限设为 0 |
| 3) 温度 85 ℃ + 校验开 | 0 条（超量程不判） |
| 4) 温度 85 ℃ + 校验关 | 1 条（温度超上限） |
| 5) `humiX100 = -1`（无效湿度） | 0 条（无效值走 `humiValid()`，不产生告警） |

---

## 13. 上位机下行控制通道（2026-10-10 补）

### 13.1 改动前的事实

| 事实 | 证据 |
|---|---|
| Qt 串口是只读打开的，全工程没有一处 `m_serial->write()` | `mainwindow.cpp` 的 `onOpenPortClicked()` 里 `m_serial->open(QIODevice::ReadOnly)` |
| 因此上位机只实现了监测方向：数据往上走，命令发不出去 | 菜单里只有 文件(CSV/退出)、工具(自检/演示帧/清空)、帮助(协议/关于) |
| 板2 只是"收 LoRa → 转串口 + 写 Flash"，没有接收上位机命令的通道 | `main()` 里用的是 `SYS_USART_Init(USART_QT, …)`（只开发送），没开 RX 中断 |
| 断网缓存只有存、没有补传 | `W25QXX_LogRead()` 只在开机自检 `test_flash_log()` 里被调用一次 |
| MQTT 运行期没接 | `MQTT_Publish*` 全工程只有开机自检 `test_mqtt()` 里那一处调用 |

> "板1 采集 / 板2 处理转发 / 同时给 MQTT 和 Qt"这句话只有 Qt 这一路是真的；
> MQTT 那一路目前是开机自检时测一次连通性（`broker.emqx.io` 发一条 alive-test），运行期不发数据。

### 13.2 命令表（三端同一套字节）

帧格式不变（`AA 55 CMD LEN DATA… CRC低 CRC高 55 AA`），只是给 `CMD` 分了两个区：
`0x00~0x7F` = 板2 → 上位机的上报（环境帧 `0x01`），`0x80~0xFF` = 板2 → 上位机的应答（`CMD = 请求 | 0x80`）。

| 方向 | 名称 | CMD | 载荷 | 说明 |
|---|---|---|---|---|
| 下发 | `SET_FWD` | `0x10` | 1 字节 `0/1` | 板2 要不要往 USART2 转发环境帧 |
| 下发 | `SET_CACHE` | `0x11` | 1 字节 `0/1` | 板2 要不要把收到的帧写进 W25Q128 |
| 下发 | `QUERY` | `0x12` | 无（LEN=0） | 要一份板2 状态快照 |
| 下发 | `CLR_CACHE` | `0x13` | 无（LEN=0） | 擦掉整个 Flash 缓存区（16 个扇区） |
| 下发 | `REBOOT` | `0x14` | 无（LEN=0） | 软复位板2（`NVIC_SystemReset()`） |
| 应答 | — | `请求\|0x80` | 第 1 字节恒为状态码 | `0=OK` `1=参数错` `2=不支持` `3=忙` |

应答载荷：

| 对应请求 | 应答 CMD | 载荷 |
|---|---|---|
| `SET_FWD` / `SET_CACHE` | `0x90` / `0x91` | `[0]` 状态，`[1]` 新值（1 字节） |
| `QUERY` | `0x92` | 16 字节：`[0]`状态 `[1]`转发开关 `[2]`缓存开关 `[3]`保留 `[4:6]`收好帧数 `[6:8]`收坏帧数 `[8:10]`缓存条数 `[10:12]`丢帧数 `[12:16]`运行秒数（多字节大端） |
| `CLR_CACHE` | `0x93` | `[0]` 状态，`[1:3]` 清完之后的条数（应为 0） |
| `REBOOT` | `0x94` | `[0]` 状态（先回这一帧、`SYS_USART_FlushTx()` 确认发完、100 ms 后才复位） |
| 未知命令 | `cmd\|0x80` | `[0]` = `2`（不支持） |

### 13.3 上位机侧的实现要点

1. 只允许一条在途命令（`devctl.cpp`）：应答里没有事务号，两条命令并发就分不清回的是哪一条。所以 `DevCtl` 是个队列，队首发了才发下一条；600 ms 没等到应答就重发（最多 3 次），3 次都超时就 `commandFailed` 并放行下一条。换串口或关串口一律清队列（应答不可能再回来了）。
2. 状态回显与用户操作要分开：收到 `QUERY` 应答要 `setChecked(dev.fwd)` 回显开关，这会再触发一次 `toggled(bool)` → 又发一帧 `SET_FWD` → 设备再回，形成死循环。回显时用 `QSignalBlocker` 挡住信号，只有用户点击才下发。
3. 破坏性操作二次确认："清空板2 Flash 缓存""重启板2"都先弹 `QMessageBox::question`，默认按钮是 No；避免误点一下丢掉几十小时的缓存。

另外：`m_dev->tick()` 挂在 50 ms 的曲线定时器上（不是 1 s 的统计定时器），否则 600 ms 超时会被拉长到 1 s 量级。

### 13.4 验证方法（`--cmdselftest`，16 项）

期望字节来自独立实现 `SerialMonitor/tools/gen_ctrl_vectors.py`（只按协议文字描述的 CRC16-MODBUS 重算一遍），不是用被测的 `FrameProto::build()` 自己算自己；否则实现错了也会通过。

| 组 | 用例 | 期望 |
|---|---|---|
| 组帧 ×6 | `QUERY` / `SET_FWD(1)` / `SET_FWD(0)` / `SET_CACHE(1)` / `CLR_CACHE` / `REBOOT` | 例如 `QUERY = AA 55 12 00 0D 10 55 AA`、`SET_FWD(1) = AA 55 10 01 01 B0 55 55 AA` |
| 解析 ×5 | `QUERY` 应答 16 字节、`SET_FWD` 应答、状态码 1（参数错）、`CLR_CACHE` 应答、`DevCtl` 状态快照 | `好=291 坏=4 缓存=200 丢=2 运行=4660s` 逐字段对上 |
| 抗错 ×2 | CRC 坏 1 位的应答 | `errCrc=1`、取不到 AckFrame（不能把坏帧当命令成功） |
| 混流 ×1 | 2 条环境帧 + 1 条应答帧 | `framesOk=2 framesAck=1 errCrc=0`（应答不能被当成其他命令帧丢掉，也不能污染环境帧统计） |
| 护栏 ×2 | 串口没 attach 就下发 / `rxCount` 计数 | 返回 `false`、不崩、队列还是 0 |

实测：`通过 16 / 16`、退出码 0（结果写 `build_verify\release\下发控制自检.txt`）。

### 13.5 该自检未覆盖的范围

- 真串口收发：本机只有蓝牙串口，没有第二块板子；板2 是否真收到、RX 中断是否开对、`SysUsartId_t` 有没有配错，要实机联调才知道；
- 超时与重发：600 ms × 3 的逻辑只有在真链路上才有意义（本机没有对端）；
- 板2 固件：已复编 0 Error / 0 Warning（`E:\k5_v5\UV4\UV4.exe`，见 11.5），但没有烧到板子上跑过；
- 拔线与半开连接：上位机发出的命令在串口还开着但板2 没响应时会重发 3 次后报错，这个路径只能实机验。
