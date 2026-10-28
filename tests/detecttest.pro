# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = detecttest
INCLUDEPATH += ..
SOURCES += detecttest.cpp ../diskformat.cpp ../adfs.cpp ../dfs.cpp ../dosfs.cpp
HEADERS += ../diskformat.h ../adfs.h ../dfs.h ../dosfs.h
