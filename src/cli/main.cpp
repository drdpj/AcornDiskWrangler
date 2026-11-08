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
// main.cpp - acorndw, the command-line program: the same discs and the same checked saving as Acorn Disk Wrangler, without a window.
#include <QCoreApplication>

#include "backend.h"

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    AcornCli::Backend backend;
    dw::cli::AppInfo info;
    info.tool = "acorndw";
    info.appName = "Acorn Disk Wrangler";
    info.appVersion = APP_VERSION;
    info.backend = &backend;
    info.options = QStringList{"sides", "sidecars", "title", "dosboot", "to-sides"};
    return dw::runCli(argc, argv, info);
}
