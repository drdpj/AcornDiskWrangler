# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = viewtest
INCLUDEPATH += ../src
SOURCES += viewtest.cpp ../src/viewtext.cpp
HEADERS += ../src/viewtext.h
