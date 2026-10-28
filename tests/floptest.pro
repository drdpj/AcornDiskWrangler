# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = floptest
INCLUDEPATH += ..
SOURCES += floptest.cpp ../adfs.cpp ../dosfs.cpp ../dfs.cpp ../diskformat.cpp
HEADERS += ../adfs.h ../dosfs.h ../dfs.h ../diskformat.h
