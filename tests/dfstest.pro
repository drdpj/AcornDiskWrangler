# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++11 console
TARGET = dfstest
INCLUDEPATH += ..
SOURCES += dfstest.cpp ../adfs.cpp ../dfs.cpp
HEADERS += ../adfs.h ../dfs.h
