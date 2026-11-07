# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = selftest
SOURCES += selftest.cpp ../src/adfs.cpp
HEADERS += ../src/adfs.h
INCLUDEPATH += ../src
!include(../core/diskwrangler-device.pri): error("core/ is empty: run 'git submodule update --init'")   # adfs.cpp sits on the core's device layer
