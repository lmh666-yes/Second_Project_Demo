QT += core gui widgets
CONFIG += c++17
TEMPLATE = app
TARGET = GitUploader

# 源码与后端脚本都在本目录的 src/ 下（ASCII 路径，qmake 不认 .pro 里的非 ASCII 路径）
INCLUDEPATH += src

SOURCES += \
    main.cpp \
    src/uploader.cpp \
    src/selftest.cpp \
    src/profile.cpp \
    src/uploaderwindow.cpp

HEADERS += \
    src/uploader.h \
    src/profile.h \
    src/uploaderwindow.h
