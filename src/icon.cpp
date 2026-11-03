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
#include "icon.h"
#include <QPainter>
#include <QPainterPath>

// Everything is laid out on a 100 x 100 grid and scaled to the requested size.
QImage floppyIconImage(int size) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 100.0, size / 100.0);
    p.setPen(Qt::NoPen);

    // Proportions measured from a photo of a real 5.25" disc: the sleeve is square, the hub opening is small
    // (about 15% of the width), and the head slot is a long narrow slot below it.
    const QColor liner("#55565c"), dark("#17181c");
    // sleeve: dark card with a small write-protect notch cut into the right edge
    QPainterPath sleeve;
    sleeve.addRoundedRect(QRectF(6, 6, 88, 88), 3, 3);
    QPainterPath notch;
    notch.addRect(QRectF(91.2, 23.5, 3, 7));
    p.setBrush(QColor("#2f3138"));
    p.drawPath(sleeve.subtracted(notch));
    p.setBrush(QColor("#3d4049"));                        // a lighter bevel along the top edge
    p.drawRoundedRect(QRectF(8, 7.3, 84, 2.4), 1.2, 1.2);

    // label (cream, with a red band and two ruled lines)
    p.setBrush(QColor("#f1e7c9"));
    p.drawRoundedRect(QRectF(15, 12, 56, 19), 2, 2);
    p.setBrush(QColor("#d6402f"));
    p.drawRect(QRectF(15, 12, 56, 5.5));
    p.setBrush(QColor("#8a8470"));
    p.drawRect(QRectF(19, 21, 40, 1.5));
    p.drawRect(QRectF(19, 25.5, 31, 1.5));

    // hub opening: the sleeve's grey liner, then the disc's white reinforcement ring with the spindle hole in it
    const QPointF c(50, 49);
    p.setBrush(liner);
    p.drawEllipse(c, 13.2, 13.2);
    p.setBrush(QColor("#ecece8"));
    p.drawEllipse(c, 10.2, 10.2);
    p.setBrush(dark);
    p.drawEllipse(c, 6.2, 6.2);                           // white ring is now as wide as the index hole (4)
    p.setBrush(liner);                                    // index hole
    p.drawEllipse(QPointF(c.x() + 16.4, c.y() + 5.3), 2.0, 2.0);

    // head slot: long and narrow
    p.setBrush(liner);
    p.drawRoundedRect(QRectF(46, 69.7, 8, 21.7), 4, 4);
    return img;
}

QIcon appIcon() {
    QIcon ic;
    const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
    for (int s : sizes) ic.addPixmap(QPixmap::fromImage(floppyIconImage(s)));
    return ic;
}
