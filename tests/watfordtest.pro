# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = watfordtest
INCLUDEPATH += ../src
SOURCES += watfordtest.cpp ../src/newdisc.cpp ../src/diskformat.cpp ../src/adfs.cpp ../src/dfs.cpp ../src/dosfs.cpp
HEADERS += ../src/newdisc.h ../src/diskformat.h ../src/adfs.h ../src/dfs.h ../src/dosfs.h
