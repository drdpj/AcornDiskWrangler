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
#include "dfs.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <cstdio>
using namespace Adfs;
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
static QByteArray rnd(int n,int seed){ QByteArray b(n,0); quint32 s=seed*2654435761u+1; for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]=char(s>>24);} return b; }
static void dump(const Report&r){ for(auto&s:r.errors) printf("  ERR  %s\n",qPrintable(s)); for(auto&s:r.warnings) printf("  WARN %s\n",qPrintable(s)); for(auto&s:r.info) printf("  INFO %s\n",qPrintable(s)); }
static void listing(Dfs::Disc& d){
    for(const Dfs::File& f: d.files()){ QByteArray b; QString e; d.readFile(f,&b,&e);
        printf("LIST %d %c.%s %u %u %u %u %d %s\n",d.side(),f.dir.toLatin1(),qPrintable(f.name),f.start,f.length,f.load,f.exec,f.locked,qPrintable(QCryptographicHash::hash(b,QCryptographicHash::Sha1).toHex())); }
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    QString path=argv[1]; bool dsd=QString(argv[2])=="dsd"; quint32 sec=atoi(argv[3]);   // sectors per side
    quint64 full = quint64(sec)*256*(dsd?2:1);
    Dfs::Layout lay; lay.doubleSided=dsd; lay.interleaved=true; lay.sectorsPerSide=sec;
    Image img; QString err; CHECK(img.openRaw(path,full,&err)); CHECK(img.editable());
    printf("file size %llu full %llu needsPadding=%d\n",(unsigned long long)img.fileSize(),(unsigned long long)img.fullSize(),img.needsPadding());
    Dfs::Disc d0,d1; CHECK(d0.attach(&img,0,lay,&err)); if(dsd) CHECK(d1.attach(&img,1,lay,&err));
    printf("title0='%s' sectors=%u free=%u\n",qPrintable(d0.title()),d0.sectors(),d0.freeSectors());
    CHECK(d0.check().ok());
    CHECK(d0.addFile('$',"BOOT",rnd(300,1),0x1900,0x8023,false,false,&err));
    CHECK(d0.addFile('$',"Prog",rnd(5000,2),0xFFFF1900,0xFFFF8023,true,false,&err));       // 32-bit addresses are masked to 18 bits
    CHECK(d0.addFile('A',"Data",rnd(70000,3),0,0,false,false,&err));
    CHECK(d0.addFile('$',"EMPTY",QByteArray(),0,0,false,false,&err));
    CHECK(!d0.addFile('$',"boot",rnd(10,4),0,0,false,false,&err)); printf("  dup: %s\n",qPrintable(err));
    CHECK(!d0.addFile('$',"toolongname",rnd(10,4),0,0,false,false,&err)); CHECK(!d0.addFile('$',"a.b",rnd(10,4),0,0,false,false,&err)); CHECK(!d0.addFile('.',"X",rnd(10,4),0,0,false,false,&err));
    CHECK(d0.addFile('$',"BOOT",rnd(900,5),0x1900,0x8023,false,true,&err));                // replace, different size
    CHECK(d0.modify('$',"Prog",'$',"PROG2",false,0x2000,0x2001,&err)); CHECK(d0.modify('A',"DATA",'B',"DATA",true,1,2,&err));
    CHECK(d0.setTitle("My Title 123",&err)); CHECK(!d0.setTitle("This is much too long",&err));
    // too big for the disc: must roll back exactly
    quint32 fr=d0.freeSectors(); int nf=d0.files().size(); CHECK(!d0.addFile('$',"HUGE",QByteArray(int(sec)*256,'x'),0,0,false,false,&err)); printf("  full: %s\n",qPrintable(err)); CHECK(d0.freeSectors()==fr && d0.files().size()==nf);
    // catalogue limit
    int added=0; for(int i=0;i<40;i++) if(d0.addFile('$',QString("F%1").arg(i),rnd(1,10+i),0,0,false,false,&err)) added++;
    printf("  catalogue: added %d more, last error: %s\n",added,qPrintable(err)); CHECK(d0.files().size()==31);
    CHECK(d0.remove('$',"F0",&err)); CHECK(d0.remove('$',"F1",&err)); CHECK(!d0.remove('$',"NOPE",&err));
    Report r=d0.check(); dump(r); CHECK(r.ok());
    if(dsd){ CHECK(d1.addFile('$',"SIDE2",rnd(1234,20),0x1900,0x1900,false,false,&err)); CHECK(d1.setTitle("DRIVE2",&err)); CHECK(d1.check().ok()); }
    CHECK(img.commit(&err,false,nullptr)); printf("commit: %s\n",qPrintable(err));
    CHECK(QFile(path).size()==qint64(full)); CHECK(!img.needsPadding());
    img.close();
    CHECK(img.openRaw(path,full,&err)); Dfs::Disc e0,e1; CHECK(e0.attach(&img,0,lay,&err)); CHECK(e0.check().ok()); listing(e0);
    if(dsd){ CHECK(e1.attach(&img,1,lay,&err)); CHECK(e1.check().ok()); listing(e1); printf("title1='%s'\n",qPrintable(e1.title())); }
    printf("title0='%s'\n",qPrintable(e0.title()));
    printf("%s (%d failures)\n",fails?"FAILED":"ALL OK",fails); return fails?1:0;
}
