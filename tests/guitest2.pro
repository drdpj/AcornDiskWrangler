# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets testlib
CONFIG += c++17
TARGET = guitest2
INCLUDEPATH += ../src
SOURCES += guitest2.cpp ../src/mainwindow.cpp ../src/licences.cpp ../src/mainwindow_dos.cpp ../src/mainwindow_dfs.cpp ../src/viewer.cpp ../src/viewtext.cpp \
           ../src/dosfs.cpp ../src/dfs.cpp ../src/diskformat.cpp ../src/adfs.cpp ../src/newdisc.cpp ../src/dragout.cpp
HEADERS += ../src/dragout.h ../src/fileopen.h ../src/mainwindow.h ../src/mainwindow_p.h ../src/viewer.h ../src/viewtext.h ../src/dosfs.h ../src/dfs.h ../src/diskformat.h ../src/adfs.h
