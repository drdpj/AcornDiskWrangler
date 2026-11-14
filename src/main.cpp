/*
 * Acorn Disk Wrangler - view and edit Acorn disc images (ADFS, DFS, DOS Plus, PC FAT)
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <QApplication>
#include <QLoggingCategory>
#include "mainwindow.h"
#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/app/fileopen.h>
#include "icon.h"
#include "dragout.h"

#ifndef APP_VERSION                // set by version.pri / version.cmake from git describe
#define APP_VERSION "unknown"
#endif

int main(int argc, char *argv[]) {
#if defined(Q_OS_LINUX)
    // On a GNOME Wayland session Qt starts under XWayland (xcb) unless it is told otherwise, and says so on stderr:
    // "Warning: Ignoring WAYLAND_DISPLAY on Gnome. Use QT_QPA_PLATFORM=wayland to run on Wayland anyway." Make that choice
    // ourselves, so there is nothing to warn about. Wayland stays the fallback, and QT_QPA_PLATFORM set by the user wins.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qgetenv("XDG_SESSION_TYPE") == "wayland" &&
        qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("GNOME"))
        qputenv("QT_QPA_PLATFORM", "xcb;wayland");
#endif
    // Qt's Wayland text-input-v3 plugin warns "Try to disable surface ... with focusing surface" whenever focus moves
    // between a window and a dialog. It is a harmless Qt-internal bookkeeping complaint, so hide that one category's
    // warnings.
    QLoggingCategory::setFilterRules("qt.qpa.wayland.textinput.warning=false");
    QGuiApplication::setDesktopFileName("acorndiskwrangler");   // before QApplication: registers with the portal first
    QApplication app(argc, argv);
    QApplication::setOrganizationName("acorndiskwrangler");
    QApplication::setApplicationName("acorndiskwrangler");
    AppSettings::init();                             // portable mode if <program>.ini is beside the program
    QApplication::setApplicationVersion(APP_VERSION);
    // Ties the windows to acorndiskwrangler.desktop, which is where Linux docks and task bars (GNOME, KDE) take the
    // icon and name from; the window icon below is not enough there. (X11 matches StartupWMClass, Wayland this.)
    QGuiApplication::setDesktopFileName("acorndiskwrangler");
    QApplication::setWindowIcon(appIcon());          // inherited by every window and dialog
    RetroFiles::cleanStale();                        // drag folders left behind by a crash
    FileOpen::FileOpenFilter<MainWindow> fileOpen;    // macOS: files opened from the Finder (fileopen.h)
    app.installEventFilter(&fileOpen);
    MainWindow w;
    w.show();
    for (int i = 1; i < argc; ++i) {
        QString p = QString::fromLocal8Bit(argv[i]);
        if (i == 1) {
            w.openFile(p);
        } else {                       // further images each get their own window
            auto *x = new MainWindow;
            x->setAttribute(Qt::WA_DeleteOnClose);
            x->show();
            x->openFile(p);
        }
    }
    return app.exec();
}
