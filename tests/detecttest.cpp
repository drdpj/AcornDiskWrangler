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
#include "diskformat.h"
#include <QCoreApplication>
#include <cstdio>
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    for(int i=1;i<argc;i++){ DiskInfo d=detectDisk(argv[i]);
        const char* k[]={"Unknown","Adfs","AdfsDosHybrid","Dfs","DosPc","DosAcorn800","AdfsNewMap"};
        printf("%-22s -> %-14s %s | full=%llu%s%s\n",argv[i],k[d.kind],qPrintable(d.description),(unsigned long long)d.fullSize,
               qPrintable(QString(d.kind==DiskInfo::Dfs?(d.dfs.doubleSided?" DSD":" SSD"):"") +
                          (d.sides.doubleSided()?QString(" | sides %1%2").arg(d.sides.sequential?"sequential":"interleaved",
                                                                                 d.sidesConfirmed?"":" (can't tell, assumed)"):QString())),
               d.notes.isEmpty()?"":qPrintable(" | NOTE: "+d.notes.join("; "))); }
}
