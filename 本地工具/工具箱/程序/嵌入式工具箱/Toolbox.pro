QT += core gui widgets
CONFIG += c++17
TEMPLATE = app
TARGET = Toolbox

# 复用共享源码（ASCII 路径，qmake 不认 .pro 里的非 ASCII 路径）
INCLUDEPATH += ../../shared/common ../../shared/kickstart ../../shared/protocol ../../shared/lookup

SOURCES += \
    main.cpp \
    toolboxwindow.cpp \
    toolrunpage.cpp \
    scaffoldpage.cpp \
    buildflashpage.cpp \
    rtosgen.cpp \
    rtosgenpage.cpp \
    cockpitpage.cpp \
    overviewpage.cpp \
    dashboardcanvas.cpp \
    ../../shared/common/settings.cpp \
    ../../shared/common/theme.cpp \
    ../../shared/common/pyfind.cpp \
    ../../shared/kickstart/docbuilder.cpp \
    ../../shared/kickstart/kickstartwindow.cpp \
    ../../shared/protocol/protocol.cpp \
    ../../shared/protocol/workbenchwindow.cpp \
    ../../shared/lookup/apppaths.cpp \
    ../../shared/lookup/cardhtml.cpp \
    ../../shared/lookup/dataset.cpp \
    ../../shared/lookup/lookupdata.cpp \
    ../../shared/lookup/mainwindow.cpp \
    ../../shared/lookup/searchengine.cpp \
    ../../shared/lookup/searchtables.cpp \
    ../../shared/lookup/store.cpp \
    ../../shared/lookup/lookupcli.cpp

HEADERS += \
    toolboxwindow.h \
    toolrunpage.h \
    scaffoldpage.h \
    buildflashpage.h \
    rtosgen.h \
    rtosgenpage.h \
    cockpitpage.h \
    overviewpage.h \
    dashboardcanvas.h \
    ../../shared/common/settings.h \
    ../../shared/common/theme.h \
    ../../shared/common/pyfind.h \
    ../../shared/kickstart/docbuilder.h \
    ../../shared/kickstart/kickstartwindow.h \
    ../../shared/protocol/protocol.h \
    ../../shared/protocol/workbenchwindow.h \
    ../../shared/lookup/apppaths.h \
    ../../shared/lookup/cardhtml.h \
    ../../shared/lookup/dataset.h \
    ../../shared/lookup/lookupdata.h \
    ../../shared/lookup/mainwindow.h \
    ../../shared/lookup/searchengine.h \
    ../../shared/lookup/searchtables.h \
    ../../shared/lookup/store.h \
    ../../shared/lookup/lookupcli.h
