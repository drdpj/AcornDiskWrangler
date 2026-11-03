# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = dfstest
INCLUDEPATH += ../src
SOURCES += dfstest.cpp ../src/adfs.cpp ../src/dfs.cpp
HEADERS += ../src/adfs.h ../src/dfs.h
