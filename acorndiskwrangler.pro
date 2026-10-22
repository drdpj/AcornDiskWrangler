QT += widgets
CONFIG += c++11
TARGET = acorndiskwrangler
TEMPLATE = app
win32:RC_ICONS = icons/acorndiskwrangler.ico      # qmake generates the .rc for us
# Version-info strings qmake writes into that .rc (Explorer: file Properties > Details).
# Keep them in step with acorndiskwrangler.rc, which the CMake build uses instead.
QMAKE_TARGET_PRODUCT = Acorn Disk Wrangler
QMAKE_TARGET_DESCRIPTION = Acorn Disk Wrangler
QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
SOURCES += main.cpp mainwindow.cpp mainwindow_dos.cpp mainwindow_dfs.cpp viewer.cpp viewtext.cpp \
           dosfs.cpp dfs.cpp diskformat.cpp adfs.cpp icon.cpp
HEADERS += icon.h mainwindow.h mainwindow_p.h viewer.h viewtext.h dosfs.h dfs.h diskformat.h adfs.h
