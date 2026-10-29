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
#include "mainwindow.h"
#include "appsettings.h"
#include "icon.h"
#include "dragout.h"

#ifndef APP_VERSION                // set by version.pri / version.cmake from git describe
#define APP_VERSION "unknown"
#endif

int main(int argc, char *argv[]) {
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
    MainWindow w;
    w.show();
    for (int i = 1; i < argc; ++i) {
        QString p = QString::fromLocal8Bit(argv[i]);
        if (i == 1) {
            w.openImage(p);
        } else {                       // further images each get their own window
            auto *x = new MainWindow;
            x->setAttribute(Qt::WA_DeleteOnClose);
            x->show();
            x->openImage(p);
        }
    }
    return app.exec();
}
