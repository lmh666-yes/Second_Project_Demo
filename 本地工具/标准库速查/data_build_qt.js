/* data_build_qt.js —— Qt 上位机项目引导（手写、非生成物） */

/* ============================================================================
 *  「Qt 上位机（SerialMonitor）」项目引导：8 个阶段，每个动作一条 7 元组。
 *
 *  边界（跟板1/板2 一样的三条硬规矩）：
 *    1. 界面（mainwindow.ui、样式、布局、曲线控件摆放）由 AI 完成，你不写 UI；
 *       你写的是数据链路：串口收发、协议解析、数据模型、告警、记录、下发控制。
 *    2. 只引用 SerialMonitor 里真实存在的类 / 函数 / 命令行开关，不臆造。
 *    3. 每个动作都要能验收：必须写清"看到什么现象才算过"（界面、自检输出、
 *       CSV 文件、串口字节流）。
 *
 *  加载方式：本文件由标准库函数速查.html 在 data_build.js 之后加载。
 *            data_build.js 已定义 STD_BUILD_GUIDE（板1 阶段 0~3），
 *            本文件只 push，不重新定义。
 *
 *  字段：n 名称 / m 徽章 / _b 工程标签（"Qt"）/ d 一句话 / g 概述 /
 *        k 搜索关键词（空格分隔）/ f 动作数组（每个动作恰好 7 个字符串：
 *        标题、为什么、这一步做什么、代码、怎么验证、坑、相关 API）
 *
 *  维护：改了本文件 → node --check data_build_qt.js → node _build_regress.js
 *        → python check_quality.py → python build_mobile.py
 * ========================================================================== */

const STD_BUILD_QT = [
  /* ======================= Qt · 阶段 0 ======================= */
  {
    n: "🧭 Qt · 阶段 0｜先拿地图：Qt 端职责与自检习惯",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "动手前先划清：Qt 端一共要干哪七件事、哪些是 AI 给你的界面、出问题怎么用自检开关定位。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：无（这是起点）。\n本阶段产出：工程结构与自检习惯的认知。\n下一阶段怎么用：阶段 1 接串口；后面所有代码按文件 + 分区落位。\n\n本阶段的产物：① 一张职责表（收帧 / 解析 / 显示 / 告警 / 记录 / 下发控制 / 补传）；② 一遍把工程源码目录按文件读过去（每个文件负责什么）；③ 一个自检习惯：改完代码先跑 --selftest，退出码非 0 就别急着插串口。你要写的永远是数据链路，界面由 AI 完成。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "Qt 上位机 分工 阶段地图 七个职责 自检习惯 SerialMonitor 数据链路 协议解析 告警 记录 补传",
    f: [
      [
        "分工边界：你写数据链路，界面由 AI 完成",
        "边界不清是把时间烧在 .ui 文件和样式表上的主要原因，而界面对协议正确性毫无帮助。",
        "记住三句话：① 你写串口收发、协议解析、数据模型、告警判定、CSV 记录、下发控制与补传解析；② mainwindow.ui、样式表、布局、卡片与曲线控件摆放由 AI 完成，你只管把数据喂进去；③ 阈值只在 Qt 端存在，从不下发给板子。",
        "",
        "你能一句话说清：界面我不写，我写的是「字节进来 → 数值上屏 → 越限告警 → 落盘记录 → 命令下发」。",
        "别因为「调整一下卡片颜色更快」就动手改 .ui：改了以后 AI 给的下一版界面会覆盖掉，白干。",
        ""
      ],
      [
        "Qt 端要干的七件事（本手册 8 个阶段的来源）",
        "先看到全程，才知道今天这一步为什么要这么写。",
        "七件事：① 收帧（QSerialPort 读字节流）② 解析（20 字节帧 + CRC16 校验 + 重同步）③ 显示（卡片数值 + 单条曲线 + 点条）④ 告警（阈值 / 回差 / 记录表）⑤ 记录（CSV 落盘与导出）⑥ 下发控制（0x10~0x14）⑦ 历史补传（0x16 启停 + 0x96 进度）。阶段 1~7 依次对应：打通串口 → 解析与模型 → 界面与曲线 → 告警 → 记录 → 控制与补传 → 自检联调。",
        "",
        "你能指着手册说出：第 2 阶段交的是「解析器 + 数据模型」，第 6 阶段交的是「下发命令 + 补传进度」。",
        "不要跳过阶段 2 直接接界面：解析没验准，后面所有曲线都是在画错数据。",
        "QSerialPort FrameParser DataModel AlarmManager CsvRecorder DevCtl"
      ],
      [
        "先把工程源码读一遍（按文件，别按目录）",
        "知道每个文件负责什么，出问题时能在 10 秒内定位到该开哪个文件，这是后面 7 个阶段的地基。",
        "打开 D:\\GitHub_Project\\EnvMonitor\\QT_project\\SerialMonitor\\，按这个顺序读：SerialMonitor.pro（依赖哪些 Qt 模块）→ main.cpp（命令行开关与启动流程）→ frameparser.h/.cpp（协议常量与解析）→ datamodel.h/.cpp（数据与 CSV）→ statcalc.h/.cpp（窗口统计）→ alarmmanager.h/.cpp（告警）→ devctl.h/.cpp（下发命令）→ mainwindow.h/.cpp/.ui（界面与定时器）。\n\n→ 放在：看 SerialMonitor.pro（QT += 行）与 mainwindow.h 的类声明；不改代码。",
        "/* 只看依赖关系，不改代码：在 .pro 里确认串口模块 */\nQT += widgets serialport printsupport\nCONFIG += c++17\nFORMS += mainwindow.ui",
        "你能不看目录、直接说出「CRC 校验在 frameparser.cpp、阈值判定在 alarmmanager.cpp、下发命令在 devctl.cpp」。",
        "别把 20 字节帧的长度记成 20 就以为 frame 里存了整帧字节——EnvFrame 存的是解析后的字段，原始字节在 HistoryFrame::rawFrame 里。",
        "SerialMonitor.pro main.cpp frameparser datamodel statcalc alarmmanager devctl mainwindow"
      ],
      [
        "进度文档 QT上位机进度文档.md 由你自己写",
        "手册给的是动作，进度只有你自己知道写到哪了；别人替你记的进度一定会失真。",
        "在工程根目录维护 QT上位机进度文档.md，每完成一个动作记三行：① 这一步做了什么、② 怎么验证的（现象是什么）、③ 卡在哪（没卡就写「无」）。它不进版本库的发布包，纯给你自己看。\n\n→ 放在：写进 QT上位机进度文档.md，不涉及源码放置。",
        "# 复制粘贴改成自己的日期，别删格式\n## 2026-__-__ 阶段 2\n- 做：FrameParser::feed() 接上 readyRead\n- 验证：--selftest 18/18，粘包用例通过\n- 卡：无",
        "三天后你翻这份文档，能在 1 分钟内回忆起「上一步停在解析器重同步没验完」。",
        "别写成流水账「今天写了解析器」——要写清验证现象，否则等于没记。",
        ""
      ],
      [
        "自检习惯：改完先跑 --selftest，跑通再插串口",
        "串口上出问题时你分不清是「解析写错」还是「线接错」；自检把这两类问题彻底分开。",
        "先在 Qt Creator 里构建一次（Ctrl+B），产物落在你选的构建目录（Qt Creator 默认 build\\Desktop_Qt_6_11_2_MinGW_64_bit-Debug\\）。命令行进到该目录，依次跑三个开关：--selftest（帧解析 + 统计，18 个用例）、--rangecheck（物理量边界，5 个用例）、--cmdselftest（下发命令字节，22 个用例）。三份报告都写在当前工作目录，退出码 0 = 全通过。\n\n→ 放在：是命令行动作（跑 build_verify 里的 exe --selftest），不涉及源码。",
        "cd /d <你的构建目录>\\build\\Desktop_Qt_6_11_2_MinGW_64_bit-Debug\nSerialMonitor.exe --selftest\necho %ERRORLEVEL%",
        "目录里出现 自检报告.txt，末尾是「通过 N / 18」。注意：frameparser.cpp 的协议层现在还是 TODO(阶段2) 的桩（FrameProto::* 返回空），所以**现在跑必然是大量 FAIL**——这是预期的，不是环境坏了；阶段 2 把解析器写完，才应看到「通过 18 / 18」。",
        "程序是 -subsystem,windows 编译的，双击看不到任何输出——结果只在 txt 文件与退出码里，必须用 cmd 跑并 echo %ERRORLEVEL%。",
        "--selftest --rangecheck --cmdselftest 自检报告.txt"
      ],
      [
        "报告怎么读：先看结论行，再看失败用例的期望/实际",
        "报告是给你定位问题用的，不是给你「看到 18/18 就关掉」用的。",
        "每份报告的结构：标题行 → 逐条用例（编号 + 名称 + PASS/FAIL + 详情）→ 末尾汇总行 + 结论。失败时先看末尾汇总，再回到失败那条的「期望 / 实际」字节串，逐字节对帧头、CMD、LEN、CRC。\n\n→ 放在：读 自检报告.txt，不涉及源码。",
        ":: 只挑结论行看，先判断是哪一类错\nfindstr /C:\"通过\" /C:\"结论\" 自检报告.txt",
        "你能在任何一份报告里指出：标题行、某条用例的期望与实际、末尾结论行分别在哪。",
        "报告是 UTF-8 带 BOM 的，用某些老编辑器打开会看到乱码；用 VSCode 或记事本（Win10 以上）看。",
        "自检报告.txt 量程校验自检.txt 下发控制自检.txt"
      ]
    ]
  },

  /* ======================= Qt · 阶段 1 ======================= */
  {
    n: "🧭 Qt · 阶段 1｜工程与串口打通",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "用 qmake 打开 SerialMonitor.pro，把 COM 口以 115200 8N1 打开，收到的字节先原样打成十六进制。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：工程能用 qmake 构建。\n本阶段产出：QSerialPort 成员、打开/关闭/刷新端口、原始字节十六进制显示。\n下一阶段怎么用：阶段 2 把收到的字节喂给 FrameParser，串口槽只负责取字节。\n\n本阶段的产物：① 一个能编译运行的 qmake 工程（SerialMonitor.pro，依赖 widgets/serialport/printsupport）；② 串口区能刷新端口、打开/关闭、异常断开后能重连；③ 一段十六进制日志区，说明「字节确实进来了」，此时还不做任何解析。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "qmake SerialMonitor.pro QSerialPort 打开串口 115200 8N1 十六进制 日志 断开重连 CH340",
    f: [
      [
        "用 qmake 打开工程：SerialMonitor.pro 与三个 Qt 模块",
        "上位机是 qmake 工程不是 CMake；少一个模块编译期就报 addGraph 或 QSerialPort 未定义。",
        "Qt Creator → 打开文件或项目 → 选 SerialMonitor.pro。确认 .pro 里三行依赖：widgets（界面）、serialport（串口）、printsupport（QCustomPlot 打印支持）。改完 .pro 必须「执行 qmake」再构建，否则新模块不生效。\n\n→ 放在：SerialMonitor.pro 的 QT += widgets serialport printsupport 行。",
        "QT += widgets serialport printsupport\nCONFIG += c++17\nTARGET = SerialMonitor\nFORMS += mainwindow.ui\nHEADERS += mainwindow.h frameparser.h datamodel.h statcalc.h alarmmanager.h devctl.h consistencytest.h thresholddialog.h physrange.h qcustomplot.h\nSOURCES += main.cpp mainwindow.cpp frameparser.cpp datamodel.cpp statcalc.cpp alarmmanager.cpp devctl.cpp consistencytest.cpp thresholddialog.cpp qcustomplot.cpp",
        "Qt Creator 底部「编译输出」0 error 0 warning 结束，build 目录里出现 SerialMonitor.exe；双击能弹出主窗口（左串口区、中曲线、右统计）。",
        "只改了 .pro 不执行 qmake 是最常见的假失败：报错说找不到 QSerialPort，其实模块已经加了；菜单里点「构建 → 执行 qmake」。",
        "QT += serialport printsupport qmake TARGET FORMS"
      ],
      [
        "串口对象建起来：QSerialPort 与 readyRead 信号",
        "Qt 的串口是事件驱动的：不连 readyRead，你只能靠轮询 readAll()，会漏字节、延迟还高。",
        "在 mainwindow.h 里加 QSerialPort 成员并为它接信号；对象父指针给 this，随窗口一起析构。readyRead 到达时先 readAll() 拿到一段字节，这一步只允许做「取字节」，不要在这里算数值、不要在这里刷界面。\n\n→ 放在：指针声明进 mainwindow.h 成员区；new 与 connect 进 mainwindow.cpp 的串口分区。",
        "/* 放在：指针声明进 mainwindow.h 成员区 */\n#include <QSerialPort>\n#include <QSerialPortInfo>\n\n/* mainwindow.h 私有成员 */\nQSerialPort *m_serial = nullptr;\n\n/* MainWindow 构造函数里 */\nm_serial = new QSerialPort(this);\nconnect(m_serial, &QSerialPort::readyRead, this, &MainWindow::onSerialReadyRead);\nconnect(m_serial, &QSerialPort::errorOccurred, this, &MainWindow::onSerialError);\n\n/* onSerialReadyRead 第一阶段：只取字节 */\nvoid MainWindow::onSerialReadyRead()\n{\n    const QByteArray chunk = m_serial->readAll();\n    logLine(QStringLiteral(\"收到 %1 字节：%2\").arg(chunk.size())\n            .arg(QString::fromLatin1(chunk.toHex(' ').toUpper())));\n}",
        "连好板子（或用一个 USB 转串口短接 TX/RX 自发自收）后，日志区每秒多出若干行「收到 20 字节：AA 55 01 0C …」。",
        "别在 readyRead 里直接刷新曲线或卡片：数据来得比界面快时，你是把 CPU 全花在重绘上；真实工程里这个槽只喂解析器，界面交给定时器。",
        "QSerialPort QSerialPort::readyRead QSerialPort::readAll QByteArray::toHex"
      ],
      [
        "刷新端口列表：QSerialPortInfo 与 CH340 驱动",
        "COM 号在不同电脑上会变；写死 COM3 的代码换台机器就打不开，而且用户还不知道为什么。",
        "把 QSerialPortInfo::availablePorts() 的结果填进端口下拉框：显示「COM3 (COM3)」这种人机都能读的字符串，保存真实 portName 在 itemData 或括号里。刷不到端口时明确提示「未发现串口，检查 CH340 驱动」，别只留一个空下拉框。\n\n→ 放在：槽声明进 mainwindow.h；实现进 mainwindow.cpp 的串口分区。",
        "/* 放在：槽声明进 mainwindow.h */\nvoid MainWindow::onRefreshPorts()\n{\n    ui->comboPort->clear();\n    const auto ports = QSerialPortInfo::availablePorts();\n    for (const QSerialPortInfo &info : ports) {\n        ui->comboPort->addItem(QStringLiteral(\"%1 (%2)\")\n                               .arg(info.portName(), info.description()),\n                               info.portName());\n    }\n    if (ports.isEmpty())\n        ui->comboPort->addItem(QStringLiteral(\"（未发现串口，检查 CH340 驱动）\"));\n    logLine(QStringLiteral(\"刷新串口列表：发现 %1 个\").arg(ports.size()));\n}",
        "插上板子前点刷新是「发现 0 个」+ 提示驱动；插上后（设备管理器能看到 USB-SERIAL CH340）再点刷新，下拉框里出现 COM 号。",
        "「发现 0 个」九成是驱动没装或线只插了电没插数据；别急着重装 Qt Creator。",
        "QSerialPortInfo::availablePorts QSerialPortInfo::portName QSerialPortInfo::description QComboBox::addItem"
      ],
      [
        "打开串口：115200 8N1，且必须 ReadWrite",
        "上位机既收数据又要下发控制命令，以只读方式打开时 write() 会静默失败，最坑的是它不报错。",
        "打开前按 115200 / 8 数据位 / 无校验 / 1 停止位 / 无流控设置参数，open() 用 QIODevice::ReadWrite；成功后把按钮文字改成「关闭串口」，并 reset() 解析器状态，避免上一轮的半帧残段串到这一轮。失败时把 QSerialPort::errorString() 打进日志。\n\n→ 放在：mainwindow.cpp 的串口分区（openPort 槽）。",
        "/* 放在：mainwindow.cpp 的串口分区（openPort 槽） */\nvoid MainWindow::onOpenPortClicked()\n{\n    if (m_serial->isOpen()) {\n        m_serial->close();\n        m_dev->detach();\n        ui->btnOpenPort->setText(QStringLiteral(\"打开串口\"));\n        return;\n    }\n    m_serial->setPortName(ui->comboPort->currentData().toString());\n    m_serial->setBaudRate(ui->comboBaud->currentText().toInt());\n    m_serial->setDataBits(QSerialPort::Data8);\n    m_serial->setParity(QSerialPort::NoParity);\n    m_serial->setStopBits(QSerialPort::OneStop);\n    m_serial->setFlowControl(QSerialPort::NoFlowControl);\n    if (!m_serial->open(QIODevice::ReadWrite)) {   /* 必写 ReadWrite */\n        logLine(QStringLiteral(\"打开失败：%1\").arg(m_serial->errorString()));\n        return;\n    }\n    m_parser.reset();\n    m_dev->attach(m_serial);\n    ui->btnOpenPort->setText(QStringLiteral(\"关闭串口\"));\n    logLine(QStringLiteral(\"已打开 %1 @ 115200 8N1（双向）\").arg(m_serial->portName()));\n}",
        "下拉框里选中 COM 号 → 点「打开串口」：按钮变「关闭串口」，日志区出现「已打开 COMx @ 115200 8N1（双向）」，随后每秒刷出十六进制帧。",
        "板子与上位机必须同波特率同帧格式；板2 转发口是 115200 8N1，改成 9600 你会收到满屏乱码字节（帧头 AA55 永远凑不齐）。",
        "QSerialPort::open QSerialPort::setBaudRate QSerialPort::setDataBits QSerialPort::setStopBits QSerialPort::setFlowControl QSerialPort::errorString"
      ],
      [
        "原始字节先打成十六进制（不解析也要看得见）",
        "解析器写错时，人眼能看到的唯一真相就是那串十六进制；先建立「字节确实在动」的基线。",
        "日志区用 QPlainTextEdit 只读显示，每行带时间戳；用 toHex(' ') 加空格、toUpper() 统一大写，方便跟板子抓的字节对。日志控件要设 setMaximumBlockCount(300)，只留最近 300 行，否则跑一晚上内存被日志吃满。\n\n→ 放在：mainwindow.cpp 的串口分区（readyRead 槽）；输出控件在 mainwindow.ui。",
        "/* 放在：mainwindow.cpp 的串口分区（readyRead 槽） */\n/* 构造函数里的调用顺序：先建日志区，再扫串口 —— 顺序反了就是往空指针写 */\nbuildHexLog();\nsetupSerialUi();\n\nvoid MainWindow::buildHexLog()\n{\n    m_hexLog = new QPlainTextEdit(ui->statsBox);\n    m_hexLog->setReadOnly(true);\n    m_hexLog->setMaximumBlockCount(300);   /* 内存有界 */\n    m_hexLog->setMaximumHeight(140);\n    ui->verticalLayoutStats->addWidget(m_hexLog);\n}",
        "日志区能看到形如「[10:31:02.117] 收到 20 字节：AA 55 01 0C 09 88 1A 2B 03 9A 00 C8 00 1E 02 10 3B 7A 55 AA」的行，每秒一行左右。",
        "logLine() 一进来就判 if (m_hexLog == nullptr) return;，否则构造函数里先来的那条日志会直接崩。",
        "QPlainTextEdit::appendPlainText QPlainTextEdit::setMaximumBlockCount QPlainTextEdit::setReadOnly QByteArray::toHex"
      ],
      [
        "断开与重连：errorOccurred 分类处理",
        "拔线是常态；串口异常后不 detach，下一条控制命令会往一个已经关掉的端口写，界面却一直显示「已打开」。",
        "onSerialError 里收 QSerialPort::SerialPortError：ResourceError / DeviceNotFoundError 视为掉线，close() 串口、detach() 下发通道、按钮文字恢复「打开串口」、状态栏提示「可点刷新后重连」。其余错误只记日志不关口（例如 ParityError 往往只是噪声）。\n\n→ 放在：mainwindow.cpp 的串口分区（errorOccurred 槽）。",
        "/* 放在：mainwindow.cpp 的串口分区（errorOccurred 槽） */\nvoid MainWindow::onSerialError(QSerialPort::SerialPortError err)\n{\n    if (err == QSerialPort::NoError)\n        return;\n    if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError) {\n        m_serial->close();\n        m_dev->detach();\n        ui->btnOpenPort->setText(QStringLiteral(\"打开串口\"));\n        statusBar()->showMessage(QStringLiteral(\"串口异常断开（%1），可点“刷新”后重连\")\n                                 .arg(m_serial->errorString()));\n        onRefreshPorts();\n    } else {\n        logLine(QStringLiteral(\"串口错误：%1\").arg(m_serial->errorString()));\n    }\n}",
        "运行中拔掉 USB：按钮自动变回「打开串口」，状态栏出现断开提示；再插回去点「刷新」→「打开串口」，日志恢复每秒出帧。",
        "别把 errorOccurred 的所有分支都 close()：某些 ParityError 只是偶发噪声，关口会把好不容易稳定的采集打断，用户还得手动重连。",
        "QSerialPort::errorOccurred QSerialPort::errorString QSerialPort::close statusBar"
      ]
    ]
  },

  /* ======================= Qt · 阶段 2 ======================= */
  {
    n: "🧭 Qt · 阶段 2｜协议解析与数据模型",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "把字节流变成 6 路环境值：20 字节帧、CRC16-MODBUS、状态机收帧与重同步，最后接进 DataModel。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：串口能收到字节。\n本阶段产出：FrameParser 状态机 + EnvFrame/AckFrame + DataModel 入模。\n下一阶段怎么用：阶段 3 把模型里的数据画成曲线（刷新分区）。\n\n本阶段的产物：① 一个与板1 组帧严格对应的解析器（帧头 + 长度 + CRC 三重确认，错帧丢 1 字节重找）；② CRC16-MODBUS 实现（init 0xFFFF / poly 0xA001），与板1 的期望字节逐字节一致；③ 一个数据模型：6 条曲线数据、统计值、序号横坐标；④ --selftest 的 18 个用例全过，其中真正难的是粘包与重同步。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "20 字节帧 AA55 CRC16 MODBUS 状态机 收帧 重同步 粘包 半帧 6 路环境值 DataModel EnvFrame FrameParser",
    f: [
      [
        "帧长这样：AA 55 | CMD | LEN | DATA | CRC | 55 AA",
        "所有解析错误都从「记错字段位置」开始；先把 20 字节的每一段落到纸上再写代码。",
        "环境帧 CMD = 0x01，LEN = 0x0C，整帧 ENV_TOTAL = 20 字节：[0][1] = AA 55 帧头；[2] = CMD；[3] = LEN；[4..15] = 6 个字段大端（TEMP×100 / HUMI×100 / PRESS×10 / LIGHT / TVOC / MQ135）；[16][17] = CRC16 低字节在前，校验范围是 [2..15] 共 14 字节；[18][19] = 55 AA 帧尾。\n\n→ 放在：frameparser.h 的协议工具函数分区（帧头常量、命令字）。",
        "/* 放在：frameparser.h 的协议工具函数分区（帧头常量、命令字） */\n/* frameparser.h 里的常量：跟板1/板2 是同一份协议 */\nnamespace FrameProto {\nconst quint8  HEAD_HI = 0xAAU, HEAD_LO = 0x55U;\nconst quint8  TAIL_HI = 0x55U, TAIL_LO = 0xAAU;\nconst int     OVERHEAD = 8;          /* 头 2 + CMD 1 + LEN 1 + CRC 2 + 尾 2 */\nconst quint8  CMD_ENV = 0x01U;\nconst quint8  ENV_LEN = 0x0CU;\nconst int     ENV_TOTAL = 20;\n}",
        "你拿板子实际发出的 20 字节，能逐段念出：帧头 AA55、CMD 01、LEN 0C、温度大端 0x0988 = 2440 → 24.40 ℃、CRC 低字节在前、帧尾 55AA。",
        "大端！板1 按大端组帧，Qt 这边 qFromBigEndian 读；用 qFromLittleEndian 会得到 0x8809 = 34825 → 348.25 ℃ 这种一眼假的温度。",
        "FrameProto::HEAD_HI ENV_TOTAL ENV_LEN CMD_ENV qFromBigEndian"
      ],
      [
        "CRC16-MODBUS：init 0xFFFF、poly 0xA001",
        "CRC 是这三端唯一能自动发现「字节错了」的手段；参数差一点，你连自己组帧的包都验不过。",
        "写一个纯函数 quint16 crc16Modbus(const quint8 *buf, int len)：初值 0xFFFF，每字节先异或进低字节，然后右移 8 次，每次若最低位为 1 就异或 0xA001。校验范围是 CMD..数据段末尾（[2..15]，14 字节），不含帧头帧尾。\n\n→ 放在：frameparser.cpp 的协议工具函数分区（FrameProto::crc16Modbus）。",
        "/* 放在：frameparser.cpp 的协议工具函数分区（FrameProto::crc16Modbus） */\nquint16 FrameProto::crc16Modbus(const quint8 *buf, int len)\n{\n    quint16 crc = 0xFFFFU;\n    for (int i = 0; i < len; ++i) {\n        crc ^= quint16(buf[i]);\n        for (int bit = 0; bit < 8; ++bit) {\n            if (crc & 0x0001U)\n                crc = quint16((crc >> 1) ^ 0xA001U);\n            else\n                crc >>= 1;\n        }\n    }\n    return crc;\n}",
        "用 --selftest 的「组帧回环」用例自证：buildEnv() 组出的帧再解析回来，数值与输入逐位相同；报告里该条 PASS。",
        "CRC 的字节序最容易错：帧里是低字节在前（crc & 0xFF 先写），如果你按高字节在前写，自己组的帧自己都验不过，却去怀疑板子。",
        "FrameProto::crc16Modbus FrameProto::buildEnv FrameProto::verify FrameProto::VerifyCode"
      ],
      [
        "收帧状态机：帧头 + 长度 + CRC 三重确认",
        "只靠「看到 AA55 就当帧头」在粘包时必错：上一帧的帧尾 55AA 接下一帧帧头 AA55 会拼出 AA55 的假帧头。",
        "feed() 把字节攒进环形缓冲，逐字节推进状态机；凑齐一帧后先验帧尾、再验 LEN（必须等于 ENV_LEN）、最后验 CRC，全过才 takeFrame 交给上层。解析器本身不是 QObject，不碰界面——它只负责字节进、帧出。\n\n→ 放在：frameparser.cpp 的 FrameParser 分区（改 feedByte/feed）。",
        "/* 放在：frameparser.cpp 的 FrameParser 分区（改 feedByte/feed） */\nvoid FrameParser::feed(const QByteArray &chunk)\n{\n    for (int i = 0; i < chunk.size(); ++i)\n        feedByte(quint8(chunk.at(i)));\n}\n\nvoid MainWindow::onSerialReadyRead()\n{\n    const QByteArray chunk = m_serial->readAll();   /* 1. 只取字节 */\n    const FrameParser::Stats before = m_parser.stats();\n    m_parser.feed(chunk);                            /* 2. 只解析 */\n    EnvFrame f;\n    while (m_parser.takeFrame(f))                    /* 3. 出的帧进模型 */\n        handleFrame(f, QDateTime::currentMSecsSinceEpoch());\n}",
        "串口开着时，界面曲线每来一帧就多一个点；且日志区不出现「CRC 错：累计 N」「帧尾错：累计 N」「长度字段非法：累计 N」这三种行。",
        "write()/feed() 里别做界面操作：readyRead 的频率远高于界面刷新能力，把重绘塞进来只会让串口线程被界面拖慢。",
        "FrameParser::feed FrameParser::feedByte FrameParser::takeFrame FrameParser::Stats FrameProto::verify"
      ],
      [
        "重同步：校验失败只丢 1 字节，不丢整帧",
        "丢整帧长度是最常见的「越错越狠」写法：接缝处 55 AA AA 55 会让它连续丢掉后面每一个正确帧。",
        "校验失败时只把窗口往前挪 1 字节，重新找帧头，并累加 resyncBytes 计数；这样最坏情况是多试几次，不会把好帧一起吃掉。同时把 errCrc / errLen / errTail 分开计数，方便判断是「线噪声（CRC 错多）」还是「帧格式变了（LEN 错多）」。\n\n→ 放在：frameparser.cpp 的 FrameParser 分区（改失败时丢几个字节）。",
        "/* 放在：frameparser.cpp 的 FrameParser 分区（改失败时丢几个字节） */\n/* 真实实现在 FrameParser::step() 里：CRC 不过只丢 1 字节 */\nconst quint16 crc = FrameProto::crc16Modbus(p + 2, 2 + dataLen);\nif (p[4 + dataLen] != (crc & 0xFFU) || p[5 + dataLen] != ((crc >> 8) & 0xFFU)) {\n    ++m_st.errCrc;\n    dropFront(1, true);          /* 关键：1 字节，不是整帧 ENV_TOTAL */\n    continue;\n}",
        "用 --selftest 的「三帧粘包（接缝 55 AA AA 55，60 字节）」与「半帧残段 + 噪声 → 重同步」两条用例验收：两条都 PASS，且解析出的帧数是 3 与 1。",
        "重同步期间别看 buffered() 一直不为 0 就以为「有半帧没收完」——噪声字节也会留在缓冲里，判断依据应该是 framesOk 与 errCrc 的涨速，不是缓冲长度。",
        "FrameParser::Stats::resyncBytes FrameParser::Stats::errCrc FrameParser::buffered FrameParser::reset"
      ],
      [
        "按字节分包也要能收：一个帧可能分 20 次到",
        "串口驱动的 readyRead 不保证一次给你一整帧；写完「一次读到 20 字节」的假设，现场就会偶发丢帧。",
        "把「攒字节」和「切帧」彻底分开：feed() 只负责 append，takeFrame() 只在缓冲里凑齐一整帧时才吐出来。这样一次 readyRead 给 1 字节还是给 200 字节，结果完全一样。\n\n→ 放在：frameparser.cpp 的 FrameParser 分区，不要另写一套。",
        "/* 放在：frameparser.cpp 的 FrameParser 分区，不要另写一套 */\n/* 逐字节喂 20 次，结果必须与一次喂 20 字节完全一致 */\nfor (int i = 0; i < frame.size(); ++i)\n    parser.feed(frame.mid(i, 1));\nEnvFrame out;\nif (!parser.takeFrame(out))\n    logLine(QStringLiteral(\"逐字节喂入应当凑出一整帧\"));   /* 用本工程自己的日志出口 */",
        "--selftest 的「逐字节分包（20 次 feed）」与「半帧残段 + 噪声 → 重同步」用例 PASS；现场日志不出现「长度字段非法：累计 N」。",
        "别用「等 50 ms 再看缓冲够不够 20 字节」这种定时切帧法：波特率一变或系统卡顿就切错，正确做法是纯状态机。",
        "FrameParser::feed FrameParser::feedByte FrameParser::pendingFrames"
      ],
      [
        "解析成 6 路环境值：接口是 EnvFrame::unpack",
        "上层（曲线、告警、CSV）只该看到「温度 24.40 ℃」这种数值，不该再碰字节偏移。",
        "FrameProto::EnvFrame::unpack(payload, len, out) 负责把 14 字节数据段拆成 6 个原始整型，并提供物理量换算：tempC() = tempX100/100.0、humiRh() = humiX100/100.0、pressHpa() = pressX10/10.0。湿度无效时板1 传 humiX100 = -1，humiValid() 返回 false，界面显示 --。\n\n→ 放在：frameparser.cpp 的 EnvFrame/AckFrame 分区（unpack）。",
        "/* 放在：frameparser.cpp 的 EnvFrame/AckFrame 分区（unpack） */\nEnvFrame f;\nif (FrameProto::EnvFrame::unpack(payload, FrameProto::ENV_LEN, f)) {\n    const double t = f.tempC();       /* tempX100 / 100.0 */\n    const double h = f.humiValid() ? f.humiRh() : std::numeric_limits<double>::quiet_NaN();\n    const double p = f.pressHpa();    /* pressX10 / 10.0 */\n}",
        "--selftest 的「负温度（0xFE0C → -5.00℃）」与「湿度无效标记（humiX100 = -1 → 界面显示 --）」两条 PASS。",
        "换算别用整数除法：tempX100 / 100 在 C++ 里是整除，24.99 ℃ 会变成 24。用 / 100.0，或写成 /100.0 的 double 除法。",
        "EnvFrame::unpack EnvFrame::tempC EnvFrame::humiRh EnvFrame::pressHpa EnvFrame::humiValid"
      ],
      [
        "接进数据模型：DataModel 存什么、怎么取",
        "模型是界面、告警、CSV、统计的唯一数据源；直接往界面控件里塞数据，后面加一个出口就要改一遍。",
        "handleFrame() 里只做一件事：m_model.addFrame(f, wallMs)。模型内部保存 6 条曲线的 QVector<QPointF>、每路的 Stat（n/min/max/sum）、时间戳，并在超容量时从最老端丢弃（默认 1800 点 ≈ 1 s 周期 30 分钟）。取值用 m_model.history(Series::Temp)、m_model.stat(Series::Temp)、m_model.latest()。\n\n→ 放在：datamodel.cpp 的 DataModel 分区（addFrame）；调用点在 mainwindow.cpp 的收帧处理分区。",
        "/* 放在：datamodel.cpp 的 DataModel 分区（addFrame） */\nvoid MainWindow::handleFrame(const EnvFrame &f, qint64 wallMs)\n{\n    m_model.addFrame(f, wallMs);\n    m_lastFrameWallMs = wallMs;\n    m_silenceWarned = false;\n}\n\n/* 别处取数据：*/ /* const QVector<QPointF> &pts = m_model.history(Series(view)); */",
        "喂几帧演示数据后，右侧统计区的「点数」「最大」「最小」跟着涨；曲线出现 6 条点位（一次只显示一条，其余靠图例切换）。",
        "别自己再存一份 QVector<double> 当缓存：模型里已经有带时间戳与序号的有序数据，两边各存一份必然对不齐。",
        "DataModel::addFrame DataModel::history DataModel::stat DataModel::latest enum class Series"
      ]
    ]
  },

  /* ======================= Qt · 阶段 3 ======================= */
  {
    n: "🧭 Qt · 阶段 3｜界面布局与曲线",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "界面由 AI 给（mainwindow.ui 已定），你只管把模型里的数据喂给曲线与卡片：X 轴用序号、Y 轴跟随查看量、点条能点选。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：模型里有帧、可取。\n本阶段产出：6 条曲线、统一 Y 轴、X 轴序号、点条与选中点。\n下一阶段怎么用：阶段 4 对模型值做告警判定，曲线照旧刷。\n\n本阶段的产物：① 6 条曲线共用一根 Y 轴，一次只显示当前查看量；② 卡片显示 6 路实时值，颜色与曲线一致；③ X 轴是「数据序号」不是时间戳；④ 图表下方一条可点击的点条，点哪儿就把竖线与选中数值挪到哪儿；⑤ 自动量程可选，关掉时用物理量兜底范围。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "QCustomPlot addGraph setData setRange 曲线 卡片 X 轴 数据序号 Y 轴 查看量 点条 选中 replot",
    f: [
      [
        "界面不用你写：mainwindow.ui 已给，你只填数据",
        "把界面当黑盒，才可能在不改一行 .ui 的前提下把功能写完；这也是「AI 负责界面」的落地方式。",
        "认识三个区域就够：leftPanel（串口 / 记录 / 统计 / 阈值，纵向排）、centerPanel（cardsHolder 卡片区 + plotEnv 曲线 + 后插入的点条与图例）、rightPanel（统计与告警表）。曲线控件 objectName 是 plotEnv，卡片容器是 cardsHolder。控件在 .ui 里，逻辑在 mainwindow.cpp 里。\n\n→ 放在：只改 mainwindow.ui，不写 C++。",
        "/* 只读 .ui 里的对象名，不要手改 xml */\nui->plotEnv          /* QCustomPlot：曲线 */\nui->cardsHolder      /* 卡片容器：6 路实时值 */\nui->comboStatSeries  /* 右侧统计的量选择 */\nui->lblStatAvg       /* 右侧平均值 */",
        "程序起来后三个区域都在，窗口缩放时曲线跟着拉伸（QSplitter 按 minimumSizeHint 分配宽度）。",
        "想加控件时优先在代码里 new 出来再 insertWidget，而不是改 .ui：改了 .ui，AI 给的下一版界面会覆盖你的改动。",
        "mainwindow.ui plotEnv cardsHolder comboStatSeries"
      ],
      [
        "6 条曲线建好：addGraph 与统一 Y 轴",
        "6 路物理量单位完全不同（℃ / %RH / hPa / lx / ppb / ADC），共用 Y 轴就必须一次只显示一条。",
        "setupPlot() 里循环 addGraph(xAxis, viewAxis) 建 6 条，各自设置颜色与线宽；默认全部 setVisible(false)，只有当前查看量那条为 true。yAxis 统一叫「查看量轴」，Y 轴范围随查看量切换。\n\n→ 放在：mainwindow.cpp 的界面搭建分区（构造函数里 addGraph）。",
        "/* 放在：mainwindow.cpp 的界面搭建分区（构造函数里 addGraph） */\nvoid MainWindow::setupPlot()\n{\n    QCustomPlot *p = ui->plotEnv;\n    p->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);\n    p->xAxis->setLabel(QStringLiteral(\"数据序号\"));\n    m_viewAxis = p->yAxis;\n    m_viewAxis->setRange(kYMin[0], kYMax[0]);\n    for (int i = 0; i < int(Series::Count); ++i) {\n        const QColor c(kColors[i]);\n        m_graph[i] = p->addGraph(p->xAxis, m_viewAxis);\n        m_graph[i]->setPen(QPen(c, 1.6));\n        m_graph[i]->setVisible(false);\n    }\n    m_graph[0]->setVisible(true);\n}",
        "窗口里出现一条温度曲线；右键或拖拽时只能横向平移/缩放（axisRect 的 rangeDrag / rangeZoom 只给了 Qt::Horizontal）。",
        "别给 6 条曲线各建一根 Y 轴：轴一多，右侧空间被刻度吃掉，而且用户根本分不清哪根轴对应哪条线。",
        "QCustomPlot::addGraph QCPGraph::setPen QCPGraph::setVisible QCustomPlot::setInteractions QCPAxis::setRange"
      ],
      [
        "把数据喂给曲线：refreshPlot 的重建与增量两条路",
        "每来一帧就 setData 全量重灌，1800 点时每秒都在搬几万个 double，CPU 全花在复制上。",
        "refreshPlot() 先看「图上的点数」与「模型里的点数」：模型变短（清过数据）或图上点数不足时走全量 setData；否则只 addData 新增的那一段。两条路都必须带 alreadySorted = true，因为序号是单调递增的。刷新的触发交给 50 ms 定时器，不放在 readyRead 里。\n\n→ 放在：mainwindow.cpp 的刷新分区（refreshPlot）。",
        "/* 放在：mainwindow.cpp 的刷新分区（refreshPlot） */\nvoid MainWindow::refreshPlot()\n{\n    const int view = qBound(0, m_statSeries, int(Series::Count) - 1);\n    const QVector<QPointF> &pts = m_model.history(Series(view));\n    QVector<double> keys, vals;\n    const bool rebuild = (m_graph[view]->dataCount() < pts.size())\n                      || (pts.size() < int(m_shownFrames));\n    if (rebuild) {\n        splitPoints(pts, keys, vals);\n        m_graph[view]->setData(keys, vals, true);   /* 全量 */\n    } else {\n        const int add = pts.size() - int(m_shownFrames);\n        splitPoints(pts.mid(pts.size() - add), keys, vals);\n        m_graph[view]->addData(keys, vals, true);    /* 增量 */\n    }\n    m_shownFrames = pts.size();\n}",
        "板子每秒发 1 帧时，曲线从左往右稳定生长，任务管理器里进程 CPU 占用个位数（不是 30% 以上）。",
        "alreadySorted 传 false 会让 QCustomPlot 每帧白排一次序；序号本来就是递增的，必须传 true。",
        "QCPGraph::setData QCPGraph::addData QCPGraph::dataCount QCustomPlot::replot"
      ],
      [
        "X 轴用数据序号：序号由模型统一编号",
        "用墙钟时间当 X 轴会出现两个问题：补传的历史点挤成一堆，且没人愿意去心算「这是第几秒」。",
        "模型在 renumber() 里把第 k 帧的 X 设成 k+1（第一帧就是 1）；界面上 X 轴标签写「数据序号」，ticker 用整数刻度（SampleIndexTicker）。这样坐标、点条、统计里的「第几个点」是同一个概念，互相能对上。\n\n→ 放在：mainwindow.cpp 的刷新分区（refreshPlot）。",
        "/* 放在：mainwindow.cpp 的刷新分区（refreshPlot） */\n/* DataModel::renumber()：横坐标 = 序号，从 1 开始 */\nfor (int k = 0; k < v.size(); ++k)\n    v[k].setX(double(k + 1));\n\n/* 主图 X 刻度用整数 */\np->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new SampleIndexTicker));",
        "曲线最右端的 X 值等于右侧统计区的「点数」；暂停后数点数，两者一致。",
        "序号与时间戳都要留：序号给 X 轴和选点用，时间戳给 CSV 与告警用；只留一个，另一个功能就得补。",
        "DataModel::renumber QCPAxis::setTicker SampleIndexTicker QCPAxis::setLabel"
      ],
      [
        "Y 轴跟随查看量：自动量程与兜底范围",
        "6 路量程差得远（湿度 0~100 vs MQ-135 0~4095），Y 轴不切换就什么都看不清。",
        "切查看量时把 6 条曲线里目标那条设为可见，Y 轴范围二选一：勾了自动量程就遍历当前数据求最大值，setRange(0, hi * 1.10)；没勾就用该物理量的兜底范围 kYMin/kYMax（例如温度 0~50 ℃、MQ-135 0~4095）。同时把 Y 轴标签、刻度颜色改成曲线同色，避免看错线。\n\n→ 放在：mainwindow.cpp 的刷新分区。",
        "/* 放在：mainwindow.cpp 的刷新分区 */\nconst double lo = (kYMin[view] < 0.0) ? 0.0 : kYMin[view];\nm_viewAxis->setRange(lo, kYMax[view]);\nm_viewAxis->setLabel(QStringLiteral(\"%1 (%2)\")\n                     .arg(QLatin1String(DataModel::name(Series(view))),\n                          QLatin1String(DataModel::unit(Series(view)))));\nm_viewAxis->setLabelColor(QColor(kColors[view]));",
        "点图例切换查看量：曲线颜色、Y 轴标签与刻度颜色一起变，Y 轴范围也跟着换（温度轴到 50，MQ-135 轴到 4095）。",
        "别迷信「自动量程永远更好」：一条恒 0 的线会把 Y 轴压成一条线，看不出波动；给出 kYMin/kYMax 兜底，或者用 rescaleAxes(true) 只放大不缩小。",
        "QCPAxis::setRange QCPAxis::setLabel QCPAxis::setLabelColor QCPAxis::setTickLabelColor QCPAxis::setTickPen"
      ],
      [
        "点条：X 轴用序号，点哪儿选哪儿",
        "曲线有 1800 个点时鼠标根本点不准某一个点；点条把「选点」这件事摊平在一条 32 px 高的窄带上。",
        "主图下方再放一个小 QCustomPlot 当点条：它画出来的点按最小间距抽稀，但**响应**的是可视范围内所有有效点——点条上任何 X 位置都能选中对应序号。mousePress / mouseMove 两个信号接 onXStripPressed / onXStripMoved，换算成序号后统一走选中流程。\n\n→ 放在：QCPItemLine 在 mainwindow.cpp 的界面搭建分区创建；鼠标事件在刷新分区。",
        "/* 放在：QCPItemLine 在 mainwindow.cpp 的界面搭建分区创建 */\nm_xStrip = new QCustomPlot(ui->centerPanel);\nm_xStrip->setInteractions(QCP::iNone);\nm_xStrip->setMouseTracking(true);\nm_xStrip->setMinimumHeight(kStripHeight);\nm_xMarkerGraph = m_xStrip->addGraph(m_xStrip->xAxis, m_xStrip->yAxis);\nm_xMarkerGraph->setLineStyle(QCPGraph::lsNone);\nconnect(m_xStrip, &QCustomPlot::mousePress, this, &MainWindow::onXStripPressed);\nconnect(m_xStrip, &QCustomPlot::mouseMove,  this, &MainWindow::onXStripMoved);",
        "在点条上左右拖动：主图竖线跟着走，右侧「选中点」的数值与序号实时变化。",
        "点条必须与主图共用同一根 X 范围（主图 xAxis 的 rangeChanged 里同步），否则你点到的序号和曲线上的位置对不上。",
        "QCustomPlot::mousePress QCustomPlot::mouseMove QCPGraph::setLineStyle QCPGraph::setScatterStyle"
      ],
      [
        "选中点怎么显示：竖线 + 放大圆点 + 右侧数值",
        "用户点了一个点，必须同时给出三个信息：位置（竖线）、是哪个点（高亮圆点）、值是多少（文字），少一个都要靠猜。",
        "选中序号确定后统一走 applySelectedIndex()：记下 m_selX，把竖线 QCPItemStraightLine 移到该 X；在点条上把该点画成放大加粗的圆点；右侧刷新「序号 / 时间 / 数值」。选中状态不随新数据到达而丢失（m_selX 是独立成员）。\n\n→ 放在：mainwindow.cpp 的刷新分区。",
        "/* 放在：mainwindow.cpp 的刷新分区 */\nm_selLine->point1->setCoords(m_selX, 0.0);\nm_selLine->point2->setCoords(m_selX, 1.0);\n/* 点条：普通点小空心圆，选中点大实心圆 */\nm_xMarkerGraph->setScatterStyle(\n    QCPScatterStyle(QCPScatterStyle::ssDisc,\n                    QPen(QColor(192, 57, 43), 2.6), QBrush(c), 12.0));\nm_xStrip->replot(QCustomPlot::rpQueuedReplot);",
        "点中某个点后，主图出现红色虚线竖线，点条上该样本变成大的实心圆，右侧标签显示形如「第 137 点 / 10:32:17 / 24.40 ℃」。",
        "重绘用 rpQueuedReplot（排队合并），别在选中回调里直接 replot()：拖动时 mouseMove 一秒几十次，同步重绘会让界面发黏。",
        "QCPItemStraightLine::setCoords QCPScatterStyle QCustomPlot::replot QCustomPlot::rpQueuedReplot"
      ]
    ]
  },

  /* ======================= Qt · 阶段 4 ======================= */
  {
    n: "🧭 Qt · 阶段 4｜告警与阈值",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "阈值只在 Qt 端：超限报警、回到回差内才恢复，记录表去重；阈值对话框可开关单侧、一键还原默认。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：曲线在刷、数据在动。\n本阶段产出：AlarmManager 阈值/回差/记录去重 + 阈值对话框。\n下一阶段怎么用：阶段 5 把帧与告警一起落成 CSV 记录。\n\n本阶段的产物：① 6 路 × 下限/上限/回差 的阈值设置（对话框 + 面板两种入口）；② AlarmManager 按帧判定，进入告警与恢复各产生一条记录；③ 告警记录表按时间倒序，重复告警不刷屏；④ 明确物理合理范围与告警阈值的区别：超出合理范围的值照常显示、照常记 CSV，但不参与告警判定，只在统计里计数。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "告警 阈值 回差 迟滞 AlarmManager 下限 上限 记录表 去重 对话框 ThresholdDialog 物理合理范围 PhysRange",
    f: [
      [
        "阈值数据结构：下限/上限/启用/回差各管一段",
        "6 路里有些量只关心一侧（光照只关心太暗、TVOC 只关心太高）；不给单侧开关，用户只能靠「设一个不可能达到的值」来临时禁用。",
        "每路一个 Threshold：lowEnabled / low / highEnabled / high / hyst。判定规则是超限即报警、回到回差内才恢复（上限 35.0 ℃、回差 1.0 ℃：35.01 报，降到 33.99 才恢复）。默认值：温度 5~35 回差 1、湿度 20~80 回差 2、气压 980~1030、光照只启用下限 50 lx、TVOC 只启用上限 1000 ppb、MQ-135 只启用上限 2000。\n\n→ 放在：alarmmanager.h（Threshold 结构与默认值）。",
        "/* 放在：alarmmanager.h（Threshold 结构与默认值） */\nAlarmManager::Threshold t = AlarmManager::defaultThreshold(Series::Temp);\nt.lowEnabled  = true;   t.low  = 5.0;   t.hyst = 1.0;\nt.highEnabled = true;   t.high = 35.0;\nm_alarm.setThreshold(Series::Temp, t);\nm_alarm.setEnabled(true);",
        "把温度上限临时改成 20 ℃，喂几帧 24 ℃ 的数据：记录表立刻出现一条高温告警，状态栏或统计区显示活动告警数 1。",
        "阈值只在 Qt 端存，不下发给板子：板1 自己那套蜂鸣器阈值是另一套口径，两边混淆会出现「板子响但界面不报」。",
        "AlarmManager::defaultThreshold AlarmManager::setThreshold AlarmManager::Threshold::hyst"
      ],
      [
        "回差（迟滞）是怎么消抖的",
        "没有回差时，数值在阈值附近抖动会一秒报一条，记录表几秒内被刷爆，真正的异常反而看不见。",
        "判定要记忆上一状态：未告警时只有越过阈值本身才进入告警；已告警时只有回到「阈值 ∓ 回差」以内才退出。所以同一次越限只产生 2 条记录（进入 + 恢复），中间无论怎么抖都不再记。\n\n→ 放在：alarmmanager.cpp 的 evaluate。",
        "/* 放在：alarmmanager.cpp 的 evaluate */\n/* 上限 35.0 / 回差 1.0 的两条边界 */\nif (v > 35.0)            /* 进入告警：35.01 起 */\n    /* High */;\nif (v < 35.0 - 1.0)      /* 恢复：必须降到 33.99 以下 */\n    /* None */;",
        "喂一串 34.9 / 35.2 / 34.8 / 35.3 的抖动数据：记录表只有一条「进入高温告警」，没有一堆重复行。",
        "别用「等于阈值」当恢复条件：浮点比较永远等不到，回差必须是一段区间。",
        "AlarmManager::evaluate AlarmManager::state AlarmManager::Threshold::hyst"
      ],
      [
        "按帧判定：evaluate 返回本次的记录",
        "判定要跟着数据走而不是跟着界面走：界面刷新是 50 ms 一次，数据是 1 s 一帧，两者绑在一起必然漏判。",
        "handleFrame() 里把帧交给 m_alarm.evaluate(f, wallMs, suspectMask)，返回值就是「这一帧产生的新记录」（可能 0 条、也可能进入+恢复两条）。界面只在 1 s 的统计定时器里把新记录追加到表上，判定本身不受界面影响。\n\n→ 放在：alarmmanager.cpp 的 evaluate；调用点在 mainwindow.cpp 的收帧处理分区。",
        "/* 放在：alarmmanager.cpp 的 evaluate */\nconst QVector<AlarmManager::Record> recs = m_alarm.evaluate(f, wallMs, suspectMask);\nfor (const AlarmManager::Record &r : recs)\n    appendAlarmRow(r);              /* 表上追加一行 */\nconst int active = m_alarm.activeCount();   /* 当前几路在告警 */",
        "喂一帧超限数据，1 s 内告警表顶部多一行，行内有时间、物理量、等级、当前值与阈值。",
        "别在 evaluate 里做界面操作（AlarmManager 不是 QObject）：判定函数越纯，越好写自检用例。",
        "AlarmManager::evaluate AlarmManager::Record AlarmManager::activeCount AlarmManager::enterCount"
      ],
      [
        "记录表与去重：进入 / 恢复各一条，超上限滚动",
        "告警表是给人回溯用的；不去重就是刷屏，不封顶就是内存泄漏。",
        "记录表按时间倒序显示，最新在最上面；每条记录本身带 recovery 标志，文本上区分「进入」与「恢复」。记录条数封顶 kMaxRecords = 5000，超了从最老端丢。清空按钮只清表与记录，不重置阈值。\n\n→ 放在：alarmmanager.h/.cpp。",
        "/* 放在：alarmmanager.h/.cpp */\nm_alarm.clearRecords();                  /* 只清记录 */\nconst QVector<AlarmManager::Record> &rs = m_alarm.records();\nfor (int i = rs.size() - 1; i >= 0; --i)\n    appendAlarmRow(rs.at(i));             /* 倒序：最新在上 */",
        "连续喂 6000 次越限（可用 --feed 造数据）后，记录表行数稳定在 5000 左右不再涨。",
        "清空告警表时别忘了同时把你的行号/缓存对齐清掉，否则下次追加会从错误的行号开始写。",
        "AlarmManager::records AlarmManager::clearRecords AlarmManager::kMaxRecords AlarmManager::levelText"
      ],
      [
        "阈值对话框：改完点确定才生效",
        "主界面左栏固定 330 px，把 6 × 3 个输入框和 6 × 2 个勾选框全摊在面板上，会把中间的曲线区挤没。",
        "阈值设置放独立对话框 ThresholdDialog（模态打开，点「确定」才把值写回 AlarmManager）：6 行 × 下限/上限/回差 + 单侧启用勾选框，每行旁边显示该物理量的合理范围提示（例如「−40 ~ 80」）。对话框里再放一个「恢复默认」按钮。主界面面板保留 4 列可直接改，约定「改了哪个框就等于启用哪一侧」。\n\n→ 放在：thresholddialog.cpp 的确定槽；打开对话框在 mainwindow.cpp 的设备控制槽或功能按钮分区。",
        "/* 放在：thresholddialog.cpp 的确定槽 */\n/* 静态入口：模态打开，返回 true 表示用户点了确定 */\nif (ThresholdDialog::edit(&m_alarm, this)) {\n    refreshThresholdPanel();\n    saveConfig();\n    logLine(QStringLiteral(\"阈值已更新\"));\n}",
        "点「阈值设置」弹出对话框；把温度上限改 30 后点确定，再喂 31 ℃ 的数据就开始报高温；点「恢复默认」后 35 ℃ 才报。",
        "每个输入框都要按物理量设范围（用 PhysRange::lo/hi），否则用户能设出「下限 9000、上限 10」这种自相矛盾的组合，然后来问你为什么不报警。",
        "ThresholdDialog::edit ThresholdDialog::ThresholdDialog PhysRange::text PhysRange::lo PhysRange::hi"
      ],
      [
        "物理合理范围 vs 告警阈值：别混成一个概念",
        "板1 的气压/光照/TVOC/MQ-135 还没接入时传的是 0，如果拿 0 去撞「气压低于 980」的告警阈值，联调时会刷出两条假告警。",
        "用 PhysRange 单独判「这个值物理上可不可信」：温度 -40~80 ℃、湿度 0~100 %RH、气压 300~1100 hPa、光照 0~20000 lx、TVOC 0~5000 ppb、MQ-135 0~4095。超范围的值照常显示、照常记 CSV，但用 skipMask 跳过告警判定，只统计与日志各报一次。\n\n→ 放在：量程进 physrange.h，阈值进 alarmmanager，两者别合并。",
        "/* 放在：量程进 physrange.h，阈值进 alarmmanager，两者别合并 */\nquint8 suspectMask = 0;\nfor (int s = 0; s < int(Series::Count); ++s) {\n    const double v = DataModel::value(f, Series(s));\n    if (PhysRange::suspect(Series(s), v))\n        suspectMask |= quint8(1u << s);     /* 跳过这路的告警判定 */\n}\nconst auto recs = m_alarm.evaluate(f, wallMs, suspectMask);",
        "--rangecheck 的 5 个用例全 PASS；现场表现是气压恒 0 时统计计数在涨、告警表里没有气压告警。",
        "注意光照 0 lx 是合理值：它拦得住「不可能的光照」，但拦不住「光照低于下限 50 lx」这条真的告警。",
        "PhysRange::suspect PhysRange::lo PhysRange::hi AlarmManager::evaluate"
      ]
    ]
  },

  /* ======================= Qt · 阶段 5 ======================= */
  {
    n: "🧭 Qt · 阶段 5｜记录与导出",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "把每帧落成 CSV（勾选即开始、每行 flush），导出历史数据，统计最近 50 个采样点：平均剔 1 大 1 小 / 最大 / 最小 / 窗口点数。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：数据与告警都有。\n本阶段产出：CsvRecorder 实时记录、导出历史、窗口统计。\n下一阶段怎么用：阶段 6 做下发控制与历史补传，记录通道不动。\n\n本阶段的产物：① 勾选框一勾就开始记录，文件落在 data\\环境数据_yyyyMMdd_HHmmss.csv；② 表头固定 10 列，湿度无效写 --，超合理范围加 +RANGE，最后一列标「实时 / 补传」；③ 导出按钮把模型里的历史整表导出；④ 右侧统计面板按窗口算平均值（≥3 点时各剔 1 个最大最小）、最大、最小、点数。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "CSV 记录 落盘 导出 CsvRecorder header append flush 统计 StatCalc kWindow 50 剔除 最大 最小 点数",
    f: [
      [
        "勾选就开始记录：路径、mkpath 与失败回滚",
        "记录是断电后唯一还在的证据；路径没建目录、勾选框状态和实际记录状态不一致，都会让人以为「记了其实没记」。",
        "勾上 chkCsv 时用当前时间生成文件名，先 QDir().mkpath(\"data\")，再 m_csv.start(path, &err)；start 失败（目录不可写、文件被 Excel 占着）必须弹警告并把勾选框退回未勾选，不能默默继续。\n\n→ 放在：mainwindow.cpp 的功能按钮分区；CsvRecorder 在 datamodel.cpp。",
        "/* 放在：mainwindow.cpp 的功能按钮分区 */\nvoid MainWindow::onCsvToggled(bool on)\n{\n    if (!on) { m_csv.stop(); ui->lblCsvInfo->setText(QStringLiteral(\"未记录\")); return; }\n    QDir().mkpath(QDir::current().filePath(QStringLiteral(\"data\")));\n    const QString path = QStringLiteral(\"data/环境数据_%1.csv\")\n            .arg(QDateTime::currentDateTime().toString(QStringLiteral(\"yyyyMMdd_HHmmss\")));\n    QString err;\n    if (!m_csv.start(path, &err)) {\n        QMessageBox::warning(this, QStringLiteral(\"无法记录\"), err);\n        ui->chkCsv->setChecked(false);    /* 状态回滚，别骗用户 */\n        return;\n    }\n    ui->lblCsvInfo->setText(QStringLiteral(\"记录中：%1\").arg(path));\n}",
        "勾上勾选框：左栏出现「记录中：data/环境数据_20260714_103102.csv」；去磁盘上看，文件已存在且第一行是表头。",
        "文件名不要用固定名：第二次启动会 Truncate 覆盖上次的记录，用户拿着空文件来找你。",
        "CsvRecorder::start QDir::mkpath QDateTime::toString QCheckBox::setChecked"
      ],
      [
        "每帧写一行、每行 flush：断网断电不丢已记录行",
        "CSV 的价值全在「断电后还能读」；攒在缓冲区里，一断电全没。",
        "CsvRecorder::append 每写完一行就 flush()。列顺序固定：时间、温度、湿度、气压、光照、TVOC、MQ-135、节点ID、CRC状态、数据来源。湿度无效写 --（不是 0，0 %RH 和「没读到」是两回事），CRC 状态 OK / ERR，物理上不可信再加 +RANGE，来源列「实时 / 补传」放最后。\n\n→ 放在：datamodel.cpp 的 CsvRecorder 分区；调用点在 mainwindow.cpp 的收帧处理。",
        "/* 放在：datamodel.cpp 的 CsvRecorder 分区 */\nvoid CsvRecorder::append(const EnvFrame &f, const QDateTime &when,\n                        quint8 crcState, quint8 suspectMask, Source source)\n{\n    QString status = (crcState == FrameProto::OK) ? QStringLiteral(\"OK\")\n                                                  : QStringLiteral(\"ERR\");\n    if (suspectMask != 0U)\n        status += QStringLiteral(\"+RANGE\");\n    /* ... 依次写 10 列 ... */\n    ts << '\\r' << '\\n';\n    ts.flush();          /* 关键：每行落盘 */\n}",
        "记录中拔掉 USB，再用记事本打开 CSV：能读到拔线之前所有行，最后一行时间与拔线时刻一致。",
        "用 Excel 打开正在写的 CSV 会锁文件，之后再启动记录会失败——记录中的文件用记事本或 VSCode 看。",
        "CsvRecorder::append CsvRecorder::header CsvRecorder::rows CsvRecorder::Source"
      ],
      [
        "表头与 UTF-8 BOM：中文列名在 Excel 里不乱码",
        "不带 BOM 的 UTF-8 CSV 用 Excel 双击打开会把中文列名读成乱码，用户第一眼就认为你的工具坏了。",
        "start() 里先写 UTF-8 BOM（U+FEFF），再写表头，行尾用 \\r\\n。表头逐字是：时间,温度(℃),湿度(%RH),气压(hPa),光照(lx),TVOC(ppb),MQ-135,节点ID,CRC状态,数据来源。注意来源列必须在最后：以后加列不会打乱前面列的语义。\n\n→ 放在：datamodel.cpp 的 CsvRecorder 分区。",
        "/* 放在：datamodel.cpp 的 CsvRecorder 分区 */\nQTextStream ts(&m_file);\nts.setEncoding(QStringConverter::Utf8);      /* Qt6：setCodec 已废弃 */\nts << QChar(0xFEFF);                        /* BOM：手写，别指望自动加 */\nts << CsvRecorder::header() << \"\\r\\n\";\nts.flush();\nm_rows = 0;",
        "双击 CSV 用 Excel 打开：列名是「温度(℃)」而不是「æ¸©åº¦」；10 列顺序与表头一致。",
        "别用逗号以外的分隔符又不说明：国内用户默认在 Excel 里看，逗号 + BOM 是最省事的组合。",
        "QTextStream::setEncoding QStringConverter::Utf8 QChar CsvRecorder::header"
      ],
      [
        "导出历史：saveHistoryCsv 与模型一致",
        "记录开关是「从现在开始记」，用户还经常需要把内存里已有的整段历史导出来做分析。",
        "导出走 m_model.saveHistoryCsv(path, &err)：把模型里当前的 6 条曲线与时间戳整表写出（默认 data\\导出_yyyyMMdd_HHmmss.csv）。成功后在日志区打一行「已导出 N 帧到 路径」。路径给用户用 QFileDialog::getSaveFileName 改的机会。\n\n→ 放在：mainwindow.cpp 的功能按钮分区（saveHistoryCsv）。",
        "/* 放在：mainwindow.cpp 的功能按钮分区（saveHistoryCsv） */\nQString path = QStringLiteral(\"data/导出_%1.csv\")\n        .arg(QDateTime::currentDateTime().toString(QStringLiteral(\"yyyyMMdd_HHmmss\")));\npath = QFileDialog::getSaveFileName(this, QStringLiteral(\"保存为 CSV\"), path,\n                                    QStringLiteral(\"CSV 文件 (*.csv)\"));\nif (path.isEmpty()) return;\nQString err;\nif (!m_model.saveHistoryCsv(path, &err))\n    QMessageBox::warning(this, QStringLiteral(\"导出失败\"), err);\nelse\n    logLine(QStringLiteral(\"已导出 %1 帧到 %2\").arg(m_model.size()).arg(path));",
        "点「导出 CSV」选一个路径：日志出现「已导出 137 帧到 …」；打开该文件，行数 = 右侧统计区的「点数」。",
        "导出的是内存里的数据（默认 1800 点上限），不是全部历史：要完整历史必须靠记录勾选框那一路。",
        "DataModel::saveHistoryCsv DataModel::size QFileDialog::getSaveFileName"
      ],
      [
        "窗口统计：最近 50 个采样点",
        "全量平均值会被几小时前的旧状态拖住，看不出「最近怎么了」；现场关心的是最近这段。",
        "统计走 StatCalc：kWindow = 50，即只取最近 50 个采样点。窗口点数不足 50 时就用手上全部点（窗口点数在界面上要显示出来，用户才能知道这个平均值是几个点算的）。取数函数是 DataModel::windowStat(Series, window)，window = 0 表示用默认 kWindow。\n\n→ 放在：statcalc.cpp + datamodel.cpp 的 windowStat。",
        "/* 放在：statcalc.cpp + datamodel.cpp 的 windowStat */\n/* statcalc.h */\nextern const int kWindow;      /* = 50 */\nStatCalc::WindowStat ws = m_model.windowStat(Series::Temp);\n/* ws.n / ws.avg / ws.max / ws.min / ws.trimmedHigh / ws.trimmedLow */",
        "右侧「窗口点数」显示 50（数据够时）；刚启动只有 3 帧时显示 3，平均值也随之变化。",
        "别把窗口统计和「全部数据统计」混为一谈又只显示一个：用户会拿着「最大 87 ℃」来问为什么平均值只有 24 ℃，其实一个来自全部点、一个来自窗口。",
        "StatCalc::kWindow StatCalc::compute StatCalc::tailValues DataModel::windowStat"
      ],
      [
        "平均值剔 1 大 1 小：什么时候剔、剔几个",
        "单点毛刺（一次 CRC 侥幸通过的错误帧）能把平均值拉动好几度；剔掉各一个极值能把毛刺的影响压下去。",
        "规则要写死并显示给用户：点数不少于 3 时才剔除，且**各剔 1 个**（重复的极值也只去 1 个，不是把所有最大值都去掉）；点数 < 3 时不剔，直接算。平均值界面要标明是「剔除后平均」。\n\n→ 放在：statcalc.cpp。",
        "/* 放在：statcalc.cpp */\n/* StatCalc::compute 的判定要点 */\nif (n >= 3) {\n    /* 排序或两遍扫描分别找到最大、最小的那 1 个，各剔 1 个 */\n    ws.trimmedHigh = 1;\n    ws.trimmedLow  = 1;\n    ws.avg = (sum - max - min) / (n - 2);\n} else {\n    ws.trimmedHigh = 0;\n    ws.trimmedLow  = 0;\n    ws.avg = sum / n;          /* 点数不足 3：照实平均 */\n}",
        "--selftest 的统计用例（不足 3 点不剔除、全部相同、重复极值只剔各 1、超 50 只取最近 50、NaN 跳过）全部 PASS；界面上窗口点数与平均值同屏可见。",
        "重复极值「只剔 1 个」比「剔掉全部最大值」更符合直觉：全 25.0 ℃ 的 5 个点，剔完还剩 3 个 25.0，平均值仍是 25.0。",
        "StatCalc::WindowStat::avgTrimmed StatCalc::WindowStat::trimmedHigh StatCalc::WindowStat::n"
      ]
    ]
  },

  /* ======================= Qt · 阶段 6 ======================= */
  {
    n: "🧭 Qt · 阶段 6｜设备控制与历史补传",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "往下发 0x10 转发开关、0x11 缓存开关、0x12 查询、0x13 清缓存、0x14 软复位、0x16 补传启停，并把 0x96 进度回执显示出来。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：显示 / 告警 / 记录闭环。\n本阶段产出：DevCtl 下发通道、应答解析、二次确认、补传进度。\n下一阶段怎么用：阶段 7 用三套自检把这些串起来验收。\n\n本阶段的产物：① 控制面板上的两个勾选框与四个按钮，各自对应一条下行命令；② 请求帧字节与协议文档逐字节一致（已由 --cmdselftest 的 22 个用例固定）；③ 应答解析：状态、转发位、缓存位、收帧正确/错误计数、缓存条数、丢帧数、运行时长；④ 补传进度回显（状态 / 已补 / 剩余），剩余到上限时显示「255+」。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "0x10 0x11 0x12 0x13 0x14 0x16 0x96 下发 控制 应答 补传 进度 超时 重发 DevCtl AckFrame",
    f: [
      [
        "下行命令的字节格式：一条命令一帧",
        "命令字节写错一位，板2 就当未知命令丢掉，而界面上没有任何反馈——最难查的一类问题。",
        "格式与数据帧同一套外壳：AA 55 | CMD | LEN | DATA | CRC16 | 55 AA。带参数的命令（0x10 转发、0x11 缓存、0x16 补传）LEN = 1，DATA 就是开关值 00/01；查询类命令（0x12 查询、0x13 清缓存、0x14 软复位）LEN = 0 不带数据。命令码：0x10 转发、0x11 缓存、0x12 查询、0x13 清缓存、0x14 软复位、0x16 补传启停。\n\n→ 放在：devctl.h 的请求结构 + frameparser.h 的命令字表。",
        "/* 真实字节（来自 tools/gen_ctrl_vectors.py 的独立算例） */\n/* 转发=开   */ AA 55 10 01 01 B0 55 55 AA\n/* 转发=关   */ AA 55 10 01 00 71 95 55 AA\n/* 缓存=开   */ AA 55 11 01 01 E1 95 55 AA\n/* 查询状态  */ AA 55 12 00 0D 10 55 AA\n/* 清缓存    */ AA 55 13 00 0C 80 55 AA\n/* 软复位    */ AA 55 14 00 0E B0 55 AA\n/* 开始补传  */ AA 55 16 01 01 50 54 55 AA\n/* 停止补传  */ AA 55 16 01 00 91 94 55 AA",
        "把这些字节抄进 --cmdselftest 的期望值里，22 个用例全 PASS；或抓串口线用串口助手看：点一次「查询」就多一帧 AA 55 12 00 0D 10 55 AA。",
        "LEN 与 DATA 必须匹配：查询类命令写成 LEN = 1 且塞个 00，CRC 也会算对，但板2 的参数校验会把整条命令拒掉（状态回 0x01 参数错）。",
        "FrameProto::build FrameProto::CMD_SET_FWD FrameProto::CMD_SET_CACHE FrameProto::CMD_QUERY FrameProto::CMD_CLR_CACHE FrameProto::CMD_REBOOT FrameProto::CMD_REPLAY"
      ],
      [
        "DevCtl：发送队列、超时重发与「只允许一条在途」",
        "应答帧里没有序号，同时发两条命令就分不清回来的应答属于谁；不管超时，界面会永远停在「等待」。",
        "DevCtl 维护一个发送队列，同一时刻只允许一条在途命令：发出后启动 600 ms 定时器，超时未收到应答就重发，最多 3 次（正常往返 <50 ms，约 10 倍余量），仍无应答就发 commandFailed 信号让界面提示。串口关闭时必须 detach()，在途命令立即作废。\n\n→ 放在：devctl.cpp。",
        "/* 放在：devctl.cpp */\nclass DevCtl : public QObject {\n    Q_OBJECT\npublic:\n    static const int TIMEOUT_MS = 600;\n    static const int MAX_TRIES  = 3;\n    bool setForward(bool on);\n    bool setCache(bool on);\n    bool query();\n    bool clearCache();\n    bool reboot();\n    bool startReplay();\n    bool stopReplay();\n    bool setReplay(bool on);\n    void attach(QSerialPort *port);\n    void detach();\n    void onAck(const AckFrame &a);\n    void tick();\n};",
        "把板2 断电，点「查询板2 状态」：约 1.8 s 后（600 ms × 3）日志出现失败提示，程序不卡死、还能继续点别的按钮。",
        "未 attach 时所有下发函数必须返回 false 而不是崩：这是 --cmdselftest 的护栏用例，改 DevCtl 时别把它弄丢。",
        "DevCtl::attach DevCtl::detach DevCtl::onAck DevCtl::tick DevCtl::TIMEOUT_MS DevCtl::MAX_TRIES"
      ],
      [
        "应答解析：CMD | 0x80 与状态码",
        "板的应答 CMD = 请求 CMD | 0x80，不认这一条就会把应答当未知帧丢掉，界面永远收不到回执。",
        "AckFrame 提供 reqCmd() = cmd & ~0x80 用来跟请求对上；status() 是应答第一个字节：0 正常 / 1 参数错 / 2 不支持 / 3 忙。解析入口是 onSerialReadyRead 里的 takeAck 循环，收到后交给 m_dev->onAck(a)。\n\n→ 放在：devctl.cpp + frameparser.cpp 的 AckFrame 分区。",
        "/* 放在：devctl.cpp + frameparser.cpp 的 AckFrame 分区 */\nAckFrame a;\nwhile (m_parser.takeAck(a))\n    m_dev->onAck(a);\n\n/* AckFrame 里可直接取：*/\n/* a.reqCmd()  a.status()  a.fwdOn()  a.cacheOn()  a.rxOk()  a.rxBad() */\n/* a.cacheCount()  a.lostCount()  a.uptimeS()  a.replayState()  a.replayedNum() */",
        "点「查询板2 状态」：日志出现板2 的应答字节，界面标签从「未知」变成「板2 状态：转发开 / 缓存开 / 收 1234 帧 / 缓存 87 条 / 丢 0 / 运行 3600 s」。",
        "状态码非 0 时也要把命令算「已应答」并弹提示，不能只在 status == 0 时才收尾：否则参数错会被当成超时重发三次，白等 1.8 s。",
        "AckFrame::reqCmd AckFrame::status AckFrame::fwdOn AckFrame::cacheOn FrameProto::ST_OK FrameProto::ACK_FLAG"
      ],
      [
        "转发与缓存勾选框：状态回显别把信号打成环",
        "勾选框的 toggled 会触发下发，而下发完又要把板2 的真实状态回填到勾选框——回填又会触发 toggled，形成回环。",
        "回填真实状态时用 m_loading 标志挡住 onDevFwdToggled / onDevCacheToggled 里的下发逻辑（或临时 QSignalBlocker 阻塞信号）。勾选框默认勾选并对应「板2 转发数据到本机（USART2）」「板2 把数据落 Flash 缓存」，这两项决定你能不能收到实时帧、断网时数据会不会丢。\n\n→ 放在：mainwindow.cpp 的设备控制槽分区（m_loading 防信号打环）。",
        "/* 放在：mainwindow.cpp 的设备控制槽分区（m_loading 防信号打环） */\nvoid MainWindow::onDevFwdToggled(bool on)\n{\n    if (m_loading) return;                 /* 回填状态时不重复下发 */\n    logLine(QStringLiteral(\"下发 0x10 转发=%1\").arg(on ? 1 : 0));\n    m_dev->setForward(on);\n}\n\n/* 收到查询应答后回填： */\nm_loading = true;\nm_chkFwd->setChecked(st.fwd);\nm_chkCache->setChecked(st.cache);\nm_loading = false;",
        "点「查询板2 状态」后，两个勾选框按板2 的真实状态自动纠正（比如你把转发勾掉、查询后它自己又勾上，说明板2 侧本来就在转发）。",
        "转发关掉后你收不到实时帧，界面会「静止」——这不是程序死了，先看勾选框状态再看日志。",
        "QCheckBox::toggled QCheckBox::setChecked QSignalBlocker DevCtl::setForward DevCtl::setCache"
      ],
      [
        "危险操作要二次确认：清缓存与软复位",
        "清缓存擦掉的是断网期间唯一的数据副本（最多 2048 条）；软复位会中断板2 正在做的事。误点一次的代价是不可恢复的数据。",
        "两个按钮都先弹 QMessageBox::question：清缓存说明「会擦掉全部已缓存记录（最多 2048 条），不可恢复」；软复位说明「板2 会先回应答、100 ms 后复位」。用户点「否」就直接返回，不发任何字节。\n\n→ 放在：mainwindow.cpp 的设备控制槽分区。",
        "/* 放在：mainwindow.cpp 的设备控制槽分区 */\nif (QMessageBox::question(this, QStringLiteral(\"确认清空\"),\n        QStringLiteral(\"会擦掉全部已缓存记录（最多 2048 条），不可恢复。继续？\"))\n        != QMessageBox::Yes)\n    return;\nm_dev->clearCache();",
        "点「清空板2 Flash 缓存」弹出确认框；选「否」后日志区没有新增任何下发记录，选「是」才出现 0x13 的字节。",
        "确认框别写成默认「是」：回车顺手一按就把缓存清了的场景非常常见。",
        "QMessageBox::question DevCtl::clearCache DevCtl::reboot"
      ],
      [
        "历史补传：0x16 启停与 0x96 进度回执",
        "断网时板2 把数据缓存起来了，联网后要按原始时间戳补传；补传必须有进度回显，否则不知道要等多久。",
        "补传按钮切换启停：开始发 0x16 + 01，停止发 0x16 + 00。板2 用 0x96 回执进度，4 字节载荷：状态（0 空闲 / 1 补传中 / 2 已完成 / 3 错误）、已补条数（u16）、剩余条数（u16，到 255 以上时置满并以 restCapped 标记显示「255+」）。补传回来的历史帧走 addHistory 入库去重（键 = 原始时间戳 + 原始帧内容），不参与实时告警。\n\n→ 放在：devctl.cpp + mainwindow.cpp 的设备控制槽。",
        "/* 放在：devctl.cpp + mainwindow.cpp 的设备控制槽 */\nvoid MainWindow::onDevReplayChanged(const ReplayStatus &st)\n{\n    if (!st.valid) return;\n    const QString state = AckFrame::replayStateName(st.state);\n    const QString rest  = st.restCapped ? QStringLiteral(\"255+\")\n                                        : QString::number(st.rest);\n    m_replayStatus->setText(QStringLiteral(\"补传：%1  已补 %2  剩余 %3\")\n                            .arg(state).arg(st.sent).arg(rest));\n}",
        "点「开始补传历史数据」：标签从「空闲」变「补传中 已补 N 剩余 M」，补传完变「已完成」；同时曲线左侧开始出现历史点（时间戳是过去的）。",
        "补传的历史点会插到曲线中间（按时间排序插入），如果实现里用 append 直接追加，曲线会出现「序号没变但数据乱跳」——必须走模型的 insertOrdered。",
        "DevCtl::startReplay DevCtl::stopReplay DevCtl::setReplay AckFrame::replayState AckFrame::replayedNum AckFrame::replayRest DataModel::addHistory"
      ]
    ]
  },

  /* ======================= Qt · 阶段 7 ======================= */
  {
    n: "🧭 Qt · 阶段 7｜自检与联调",
    m: "Qt 自建手册",
    _b: "Qt",
    d: "用 --selftest / --rangecheck / --cmdselftest 三个开关把协议、边界、下发命令各自钉死，再插上双板做端到端联调。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：全部功能已就位。\n本阶段产出：三套自检的用例、联调步骤、未被覆盖的部分清单。\n下一阶段怎么用：无（Qt 终点）；后面是双板联调，自检用例随功能改动同步补。\n\n本阶段的产物：① 三份自检报告都能在本地复现（自检报告.txt 18/18、量程校验自检.txt 5/5、下发控制自检.txt 22/22）；② 一份给板子侧的联调步骤：先自检、再单向、再双向、最后补传；③ 能分清「协议错」与「硬件/接线错」：自检全过而串口没数据，问题一定不在解析代码。\n\n【代码放哪 · Qt 端按「文件 + 分区」，不是按 main.c】\n· 协议字节层 → frameparser.h/.cpp 三个分区：协议工具函数 / EnvFrame·AckFrame / FrameParser 状态机\n· 数据层 → datamodel.h/.cpp：DataModel（曲线环形缓存与统计）/ CsvRecorder（记录落盘）\n· 告警 → alarmmanager.h/.cpp｜下发控制 → devctl.h/.cpp｜统计 → statcalc.h/.cpp｜量程 → physrange.h\n· 主界面声明 → mainwindow.h（控件指针 / 槽 / 成员变量）\n· 主界面实现 → mainwindow.cpp 八个分区：界面搭建 / 设备控制面板 / 设备控制槽 / 串口 / 收帧处理 / 刷新 / 功能按钮 / 配置落盘\n· 界面布局 → mainwindow.ui（只改 UI 文件，不写 C++）｜程序入口 → main.cpp｜自检用例 → consistencytest.cpp\n· 新增 .cpp/.h 必须同时写进 SerialMonitor.pro 的 SOURCES/HEADERS，否则「改了没生效」",
    k: "自检 --selftest --rangecheck --cmdselftest 自检报告.txt 联调 双板 端到端 退出码 判定",
    f: [
      [
        "--selftest：帧解析 + 统计，18 个用例",
        "解析器与统计是最底层的两块；它们错了，上面所有功能都在画错误的数据，而且看起来还挺像真的。",
        "在 exe 所在目录跑 --selftest，它把 ConsistencyTest::runBuiltin()（帧解析 11 条）与 runStatistics()（窗口统计 7 条）合起来写 自检报告.txt，退出码 0 = 全通过。18 条覆盖：组帧回环、正常帧、负温度、CRC 错帧必被丢、三帧粘包、逐字节分包、半帧残段 + 噪声重同步、湿度无效标记、历史帧解析、历史去重、不可用历史帧被拒，以及 7 条统计规则。\n\n→ 放在：用例实现进 consistencytest.cpp；运行本身走命令行。",
        "SerialMonitor.exe --selftest\nif %ERRORLEVEL% NEQ 0 echo 自检失败，先别插串口！\ntype 自检报告.txt | more",
        "自检报告.txt 最后一行是「通过 18 / 18    帧解析正确率 = 100.0%」，下面一行「结论：三端协议一致」；%ERRORLEVEL% 是 0。",
        "自检只验字节与统计，不验串口硬件：18/18 不等于「接上板子就能收」，别把这两件事混在一起。",
        "--selftest ConsistencyTest::runBuiltin ConsistencyTest::runStatistics ConsistencyTest::toText"
      ],
      [
        "--rangecheck：物理量边界，5 个用例",
        "边界值是最容易写错又最少被手工测到的地方（-40.0 ℃、0 lx、4095 这种正好卡在边上的值）。",
        "跑 --rangecheck：它用 FrameProto::buildEnv 真组帧、再用 FrameParser 解析回来，逐条检查合理范围边界的判定，写 量程校验自检.txt，退出码 0 = 全过。报告里同时写明 6 路的合理范围与「光照 0 lx 是合理值」这类容易误解的说明。\n\n→ 放在：consistencytest.cpp + physrange.h。",
        "SerialMonitor.exe --rangecheck\ntype 量程校验自检.txt",
        "量程校验自检.txt 里 5 条全 PASS，末行「结论：边界处理符合预期」，退出码 0。",
        "改了 physrange.h 的范围就一定要重跑这一项：范围的边界直接影响哪些帧会被跳过告警判定。",
        "--rangecheck PhysRange::suspect PhysRange::lo PhysRange::hi FrameParser::parseAll"
      ],
      [
        "--cmdselftest：下发通道，22 个用例",
        "下发命令的字节错一位，板2 只会静默丢弃；这一项把每条命令的字节与应答解析都固定下来，改 DevCtl 时能立刻发现回归。",
        "跑 --cmdselftest：逐个检查 0x10/0x11/0x12/0x13/0x14/0x16 的请求字节与 0x96 应答解析（状态、已补、剩余、剩余上限置满），并包含「串口未 attach 时下发返回 false 且不崩」的护栏用例，写 下发控制自检.txt，退出码 0 = 全过。\n\n→ 放在：consistencytest.cpp。",
        "SerialMonitor.exe --cmdselftest\ntype 下发控制自检.txt",
        "下发控制自检.txt 末行是「通过 22 / 22」+「结论：下发通道的字节与解析符合协议」；报告里能看到查询状态是 AA 55 12 00 0D 10 55 AA 这种逐字节的期望/实际对比。",
        "期望字节是独立 Python 脚本算的，不是用被测代码自己生成的：别图省事改成「用 build() 生成期望值」，那就变成自己验自己了。",
        "--cmdselftest DevCtl::query DevCtl::setForward DevCtl::startReplay DevCtl::onAck"
      ],
      [
        "报告怎么定位：先看末尾结论，再对失败那条的字节",
        "报告的价值在失败时；成功时你只需要看末尾一行。知道结构，排错能从 10 分钟缩到 1 分钟。",
        "三份报告同一结构：标题行（含开关名）→ 逐条用例（编号 + 名称 + PASS/FAIL + 详情）→ 汇总行 → 结论行。排错顺序：① 末尾结论确认是「解析错」还是「统计错」；② 找到 FAIL 那条，看它的期望字节与实际字节，逐段对 帧头 / CMD / LEN / DATA / CRC / 帧尾；③ 若是统计类，看期望值与实际值以及窗口点数。\n\n→ 放在：无代码。",
        ":: 直接跳到失败行与结论行\nfindstr /C:\"FAIL\" /C:\"结论\" /C:\"通过\" 自检报告.txt",
        "你能只凭报告说出「第 05 条粘包用例失败：期望解析出 3 帧，实际 2 帧」，并知道该去查重同步里的丢帧策略。",
        "报告是 UTF-8 BOM，某些编辑器显示乱码时先换编辑器，别急着怀疑报告内容错了。",
        "ConsistencyTest::CaseResult ConsistencyTest::passRate ConsistencyTest::allPassed ConsistencyTest::passedCount"
      ],
      [
        "界面上的自检入口与命令行等价",
        "命令行开关适合回归，界面按钮适合演示与现场：两者必须走同一段代码，否则会出现「按钮点了通过、命令行跑不过」。",
        "主界面的「自检」按钮（btnSelfTest）与工具菜单里的 actSelfTest 都接 onSelfTestClicked，内部调同一套 ConsistencyTest 并弹出结果摘要；命令行 --selftest 写文件是因为 -subsystem,windows 没有控制台，两条路共用同一批用例。\n\n→ 放在：mainwindow.cpp 的功能按钮分区。",
        "/* 放在：mainwindow.cpp 的功能按钮分区 */\nvoid MainWindow::onSelfTestClicked()\n{\n    const auto builtin = ConsistencyTest::runBuiltin();\n    const auto stats   = ConsistencyTest::runStatistics();\n    const double rate  = ConsistencyTest::passRate(builtin + stats);\n    QMessageBox::information(this, QStringLiteral(\"自检\"),\n        QStringLiteral(\"通过 %1 / %2，正确率 %3%\")\n            .arg(ConsistencyTest::passedCount(builtin + stats))\n            .arg(builtin.size() + stats.size())\n            .arg(rate * 100.0, 0, 'f', 1));\n}",
        "点界面上的「自检」按钮：弹窗显示「通过 18 / 18，正确率 100.0%」，与命令行跑出来的数字一致。",
        "别给界面按钮写一套「简化版自检」：两套实现意味着两套 bug，而且必然先坏在没人跑的那一套上。",
        "MainWindow::onSelfTestClicked ConsistencyTest::runBuiltin ConsistencyTest::runStatistics ConsistencyTest::passRate"
      ],
      [
        "双板联调步骤：先自检、再单向、再双向、最后补传",
        "联调时同时动三端（板1、板2、Qt）等于同时引入三个未知数；必须一次只加一个环节。",
        "建议顺序：① Qt 先跑三项自检全过（协议层已经被钉死）；② 板2 只做转发，Qt 打开串口，能稳定每秒收到 20 字节帧且 errCrc = 0；③ 接入板1 的 LoRa 链路，看 6 路数值是否与板1 的 OLED 一致；④ 点「查询板2 状态」验证下行通道；⑤ 断开板1 让板2 进入缓存，恢复后点「开始补传」，看 0x96 进度与曲线上的历史点。\n\n→ 放在：无代码。",
        "/* 打开串口后先看这几项是否正常（界面可见） */\n/* 日志：每秒 1 行 “收到 20 字节：AA 55 01 0C ...” */\n/* CRC 错计数：0 */\n/* 右侧统计：点数持续增长 */\n/* 状态栏：串口已打开，等待板2 转发的 20 字节帧… */",
        "每一步都有明确现象：② 阶段日志每秒出帧且 errCrc 为 0；④ 阶段查询后板2 状态标签由「未知」变为具体数值；⑤ 阶段补传标签从「补传中」走到「已完成」，曲线左侧出现历史点。",
        "自检全过但一个字节都收不到时，不要回头改解析代码：查波特率、TX/RX 是否交叉、地线、板2 是否真的在转发（转发勾选框 / 板2 侧转发标志）。",
        "FrameParser::Stats::errCrc DevCtl::query DevCtl::startReplay AckFrame::replayState"
      ],
      [
        "没被自检覆盖的部分：靠现象验收",
        "自检再全也有边界；把「自检覆盖不到什么」写清楚，联调时才不会拿自检通过去背锅。",
        "以下 5 项不在自动自检范围内，必须靠现场现象验收：① 真串口收发（驱动与接线）；② 板2 固件版本行为；③ USART2 RX 中断的实时性；④ DevCtl 的 600 ms × 3 次超时重发；⑤ 补传端到端（板2 侧对应实现是 Project_2_数据通信端/Project/main.c 的 vTaskCmdRx()）。这 5 项各写一句「什么现象算过」进你自己的 QT上位机进度文档.md。\n\n→ 放在：无代码，靠现象验收。",
        "/* 例：把板2 断电 2 s 再上电，Qt 侧应看到 */\n/* 串口关闭提示 -> 重连 -> 帧恢复；在途命令超时失败提示 */",
        "QT上位机进度文档.md 里这 5 项各有一行「验收现象 + 是否已过」，你能指着它说清哪一项还没验。",
        "把「自检 18/18」当成「联调完事」是这一阶段最常见的误判：自检只证明字节层没问题，不证明线接对了。",
        "DevCtl::TIMEOUT_MS DevCtl::MAX_TRIES DevCtl::timeoutCount DevCtl::pendingCount"
      ]
    ]
  }
];

if (typeof STD_BUILD_GUIDE !== "undefined") { STD_BUILD_GUIDE.push.apply(STD_BUILD_GUIDE, STD_BUILD_QT); }
