# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17
TARGET = dragtest
INCLUDEPATH += ../src
SOURCES += dragtest.cpp ../src/mainwindow.cpp ../src/mainwindow_dos.cpp ../src/mainwindow_dfs.cpp ../src/viewer.cpp ../src/viewtext.cpp \
           ../src/diskformat.cpp ../src/adfs.cpp ../src/newdisc.cpp ../src/dragout.cpp
HEADERS += ../src/mainwindow.h ../src/mainwindow_p.h ../src/viewer.h ../src/viewtext.h ../src/dosfs.h ../src/dfs.h ../src/diskformat.h ../src/adfs.h ../src/newdisc.h ../src/dragout.h
!include(../core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
include(../core/diskwrangler-dfs.pri)                # DFS and FAT are DiskWranglerCore modules (step 4)
include(../core/diskwrangler-fat.pri)
