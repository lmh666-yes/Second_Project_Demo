QT += widgets
QT += serialport
QT += printsupport          # QCustomPlot 需要 QtPrintSupport

CONFIG += c++17

# 打开后会让所有被弃用的 Qt 6 之前 API 直接编译失败
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    thresholddialog.cpp \
    devctl.cpp \
    frameparser.cpp \
    datamodel.cpp \
    alarmmanager.cpp \
    consistencytest.cpp \
    qcustomplot.cpp

HEADERS += \
    mainwindow.h \
    thresholddialog.h \
    physrange.h \
    devctl.h \
    frameparser.h \
    datamodel.h \
    alarmmanager.h \
    consistencytest.h \
    qcustomplot.h

FORMS += \
    mainwindow.ui

# 自检：构建完手动跑一次（不挂进构建流程）
#   release\SerialMonitor.exe --selftest     → 结果写 自检报告.txt，退出码 0 = 全通过
# 无 GUI 的快速版在 tests/parsertest/parsertest.pro（不含界面，适合改完解析器立即验）

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
