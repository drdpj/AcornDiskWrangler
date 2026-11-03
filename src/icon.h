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
#ifndef ICON_H
#define ICON_H
#include <QIcon>
#include <QImage>

// The application icon: a 5.25" floppy disc, drawn with QPainter (no image resources or extra Qt modules needed).
// Keep icons/acorndiskwrangler.svg in step with the geometry in icon.cpp if you change it.
QImage floppyIconImage(int size);
QIcon appIcon();

#endif
