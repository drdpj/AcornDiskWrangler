# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
# acorndw: the command-line program (QtCore only: no window, no display needed). Build it beside the GUI program:
#     mkdir b && cd b && qmake ../acorndw.pro && make
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = acorndw
TEMPLATE = app
win32 {                                            # version-info strings in the .exe (Explorer: file Properties > Details)
    QMAKE_TARGET_PRODUCT = Acorn Disk Wrangler
    QMAKE_TARGET_DESCRIPTION = acorndw, the command-line program of Acorn Disk Wrangler
    QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
}
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    INSTALLS += target
}
SOURCES += src/cli/main.cpp src/cli/backend.cpp src/diskformat.cpp src/adfs.cpp src/adfsvolume.cpp src/newdisc.cpp src/hostpolicy.cpp
HEADERS += src/cli/backend.h src/diskformat.h src/adfs.h src/adfsvolume.h src/newdisc.h src/hostpolicy.h src/dfs.h src/dosfs.h
!exists(core/diskwrangler/cli/commands.cpp): error("core/ is empty or too old: run 'git submodule update --init'")
INCLUDEPATH += core
include(core/diskwrangler-dfs.pri)
include(core/diskwrangler-fat.pri)
include(core/diskwrangler-cli.pri)
include(version.pri)
