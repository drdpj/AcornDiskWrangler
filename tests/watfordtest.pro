# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = watfordtest
INCLUDEPATH += ../src
SOURCES += watfordtest.cpp ../src/newdisc.cpp ../src/diskformat.cpp ../src/cfs.cpp ../src/cfsvolume.cpp ../src/adfs.cpp
HEADERS += ../src/newdisc.h ../src/diskformat.h ../src/adfs.h
!include(../core/diskwrangler-dfs.pri): error("core/ is empty: run 'git submodule update --init'")   # DFS and FAT: DiskWranglerCore modules on its device layer
include(../core/diskwrangler-fat.pri)
include(../core/diskwrangler-tape.pri)               # tape images (UEF): the cassette filing system
