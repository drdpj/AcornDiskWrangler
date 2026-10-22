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
#include "adfs.h"
#include <QCoreApplication>
#include <QFile>
using namespace Adfs;
static void put(const QString& p, const QByteArray& b){ QFile f(p); f.open(QIODevice::WriteOnly); f.write(b); }
int main(int argc,char**argv){ QCoreApplication app(argc,argv); QString d=argv[1];
    put(d+"/adfs_s.ads", makeBlankImage(640,"FLOPPYS"));
    put(d+"/adfs_m.adm", makeBlankImage(1280,"FLOPPYM"));
    put(d+"/adfs_l.adl", makeBlankImage(2560,"FLOPPYL"));
    put(d+"/adfs_l_trunc.adl", makeBlankImage(2560,"TRUNC").left(20000));
    QByteArray dfake(819200,0); dfake.replace(0x401,4,"Hugo"); put(d+"/adfs_d.adf", dfake);
    put(d+"/mystery.bin", QByteArray(50000,'x'));
    return 0; }
