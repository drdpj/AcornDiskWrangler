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
// Writes the application icon as PNGs: mkicon <outdir>   (used to refresh icons/*.png and the .ico/.icns, and to eyeball the result)
#include <QGuiApplication>
#include <QDir>
#include "icon.h"
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc < 2) return 2;
    QDir().mkpath(argv[1]);
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512, 1024})   // 1024 only feeds the macOS .icns (see icons/README)
        if (!floppyIconImage(s).save(QString("%1/acorndiskwrangler-%2.png").arg(argv[1]).arg(s))) return 1;
    return 0;
}
