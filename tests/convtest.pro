# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = convtest
INCLUDEPATH += ../src
SOURCES += convtest.cpp ../src/cli/backend.cpp ../src/diskformat.cpp ../src/adfs.cpp ../src/adfsvolume.cpp ../src/newdisc.cpp ../src/hostpolicy.cpp
HEADERS += ../src/cli/backend.h ../src/diskformat.h ../src/adfs.h ../src/adfsvolume.h ../src/newdisc.h ../src/hostpolicy.h
!include(../core/diskwrangler-dfs.pri): error("core/ is empty: run 'git submodule update --init'")
include(../core/diskwrangler-fat.pri)
include(../core/diskwrangler-cli.pri)
