# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = floptest
INCLUDEPATH += ../src
SOURCES += floptest.cpp ../src/adfs.cpp ../src/dosfs.cpp ../src/dfs.cpp ../src/diskformat.cpp
HEADERS += ../src/adfs.h ../src/dosfs.h ../src/dfs.h ../src/diskformat.h
!include(../core/diskwrangler-device.pri): error("core/ is empty: run 'git submodule update --init'")   # adfs.cpp sits on the core's device layer
