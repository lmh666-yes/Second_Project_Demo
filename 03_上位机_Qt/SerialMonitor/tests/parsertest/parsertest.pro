# ---------------------------------------------------------------------------
#  parsertest.pro —— 无 GUI 的解析器自检工程
#  单独建工程的原因：SerialMonitor 是 GUI 子系统程序，控制台输出看不见。
#  把 frameparser.cpp / consistencytest.cpp 直接链到这里即可看结果，
#  这两个文件不含任何 QObject，不需要 moc，也不依赖 QtWidgets。
#  构建（在 Qt 的 MinGW 环境里）：
#      E:\Qt\6.11.2\mingw_64\bin\qmake.exe parsertest.pro
#      E:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe
#      .\release\parsertest.exe
# ---------------------------------------------------------------------------
QT       -= gui
QT       += core
CONFIG   += console c++17
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = parsertest

INCLUDEPATH += $$PWD/../..

SOURCES += \
    main.cpp \
    ../../frameparser.cpp \
    ../../consistencytest.cpp

HEADERS += \
    ../../frameparser.h \
    ../../consistencytest.h
