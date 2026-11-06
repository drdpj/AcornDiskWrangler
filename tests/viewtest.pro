# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT = core
CONFIG += c++17 console
TARGET = viewtest
INCLUDEPATH += ../src ../core
SOURCES += viewtest.cpp ../src/viewtext.cpp ../core/diskwrangler/text/textviews.cpp      # plain text and hex dump are DiskWranglerCore's
HEADERS += ../src/viewtext.h ../core/diskwrangler/text/textviews.h
