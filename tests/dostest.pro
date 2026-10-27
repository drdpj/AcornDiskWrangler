# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++11 console
TARGET = dostest
INCLUDEPATH += ..
SOURCES += dostest.cpp ../adfs.cpp ../dosfs.cpp
HEADERS += ../adfs.h ../dosfs.h
