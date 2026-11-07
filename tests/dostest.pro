# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = dostest
INCLUDEPATH += ../src
SOURCES += dostest.cpp ../src/adfs.cpp
HEADERS += ../src/adfs.h ../src/dosfs.h
!include(../core/diskwrangler-dfs.pri): error("core/ is empty: run 'git submodule update --init'")   # DFS and FAT: DiskWranglerCore modules on its device layer
include(../core/diskwrangler-fat.pri)
