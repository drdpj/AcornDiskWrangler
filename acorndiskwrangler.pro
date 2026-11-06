# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17
TARGET = acorndiskwrangler
TEMPLATE = app
win32:RC_ICONS = icons/acorndiskwrangler.ico      # qmake generates the .rc for us
macx {                                             # the .app's icon (Finder, Dock before it starts)
    ICON = icons/acorndiskwrangler.icns
    QMAKE_TARGET_BUNDLE_PREFIX = io.github.drdpj
}
# Linux/BSD: "make install" puts the program, its .desktop file and icons where docks and menus look
# (GNOME's dock shows a gear without them). PREFIX defaults to /usr/local; use PREFIX=$$HOME/.local for one user.
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    desktop.files = acorndiskwrangler.desktop
    desktop.path = $$PREFIX/share/applications
    svgicon.files = icons/acorndiskwrangler.svg
    svgicon.path = $$PREFIX/share/icons/hicolor/scalable/apps
    INSTALLS += target desktop svgicon
    for(s, $$list(16 24 32 48 64 128 256 512)) {
        icon$${s}.path = $$PREFIX/share/icons/hicolor/$${s}x$${s}/apps
        icon$${s}.extra = $(INSTALL_FILE) $$PWD/icons/acorndiskwrangler-$${s}.png $(INSTALL_ROOT)$$PREFIX/share/icons/hicolor/$${s}x$${s}/apps/acorndiskwrangler.png
        INSTALLS += icon$${s}
    }
}
# Version-info strings qmake writes into that .rc (Explorer: file Properties > Details).
# Keep them in step with acorndiskwrangler.rc, which the CMake build uses instead.
QMAKE_TARGET_PRODUCT = Acorn Disk Wrangler
QMAKE_TARGET_DESCRIPTION = Acorn Disk Wrangler
QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
SOURCES += src/main.cpp src/mainwindow.cpp src/mainwindow_dos.cpp src/mainwindow_dfs.cpp src/viewer.cpp src/viewtext.cpp \
           src/dosfs.cpp src/dfs.cpp src/diskformat.cpp src/adfs.cpp src/icon.cpp src/newdisc.cpp src/dragout.cpp
HEADERS += src/icon.h src/mainwindow.h src/mainwindow_p.h src/viewer.h src/viewtext.h src/dosfs.h src/dfs.h src/diskformat.h src/adfs.h src/newdisc.h src/dragout.h
RESOURCES += src/licences.qrc                      # Help > Licences: LICENSE, notices, the LGPL for Qt
exists(licences-bundled.qrc): RESOURCES += licences-bundled.qrc   # a release build's library licences (ci/)
!include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
include(version.pri)                              # APP_VERSION from git describe (Help > About)
