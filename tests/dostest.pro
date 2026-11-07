# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = dostest
INCLUDEPATH += ../src
SOURCES += dostest.cpp ../src/adfs.cpp ../src/dosfs.cpp
HEADERS += ../src/adfs.h ../src/dosfs.h
!include(../core/diskwrangler-device.pri): error("core/ is empty: run 'git submodule update --init'")   # adfs.cpp sits on the core's device layer
