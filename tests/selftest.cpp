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
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <cstdio>
using namespace Adfs;
static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while(0)
static QByteArray rnd(int n, int seed){ QByteArray b(n,0); quint32 s=seed*2654435761u+1; for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]=char(s>>24);} return b; }
static void dump(const Report&r){ for(auto&s:r.errors) printf("  ERR  %s\n",qPrintable(s)); for(auto&s:r.warnings) printf("  WARN %s\n",qPrintable(s)); for(auto&s:r.info) printf("  INFO %s\n",qPrintable(s)); }
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    QString path = argv[1];
    Image img; QString err;
    CHECK(img.open(path,&err));
    printf("Opened original:\n"); dump(img.openReport());
    CHECK(img.editable());
    quint32 free0 = img.freeSectors();

    // --- add files & dirs
    QByteArray big = rnd(3*1024*1024+77, 1), small = rnd(1000,2);
    CHECK(img.addFile(RootSector,"BigFile",big,0x1900,0x8023,FlagR|FlagW,false,&err));
    CHECK(img.addFile(RootSector,"small",small,0xFFFFFF3F,0x12345678,FlagR|FlagL,false,&err));
    CHECK(img.addFile(RootSector,"Empty",QByteArray(),0,0,FlagR|FlagW,false,&err));
    quint32 sub=0; CHECK(img.makeDir(RootSector,"NewDir",&err,&sub));
    CHECK(img.addFile(sub,"inner",rnd(513,3),0,0,FlagR|FlagW,false,&err));
    quint32 sub2=0; CHECK(img.makeDir(sub,"Deeper",&err,&sub2));
    CHECK(img.addFile(sub2,"x",rnd(1,4),0,0,FlagR,false,&err));
    // duplicates / bad names
    CHECK(!img.addFile(RootSector,"BIGFILE",small,0,0,3,false,&err)); printf("  dup: %s\n",qPrintable(err));
    CHECK(!img.addFile(RootSector,"bad.name",small,0,0,3,false,&err)); printf("  badname: %s\n",qPrintable(err));
    CHECK(!img.addFile(RootSector,"waytoolongname",small,0,0,3,false,&err));
    // replace
    CHECK(img.addFile(RootSector,"small",rnd(300,5),1,2,FlagR|FlagW,true,&err));
    Report r = img.validate(); printf("After adds:\n"); dump(r); CHECK(r.ok());

    // --- disc full: must roll back completely
    quint32 freeBefore = img.freeSectors();
    CHECK(!img.addFile(RootSector,"Huge",QByteArray(600*1024*1024,'x'),0,0,3,false,&err)); printf("  full: %s\n",qPrintable(err));
    CHECK(img.freeSectors()==freeBefore);
    CHECK(img.validate().ok());

    // --- dir full (47 entries)
    quint32 full=0; CHECK(img.makeDir(RootSector,"Full",&err,&full));
    int added=0; for(int i=0;i<60;i++){ if(img.addFile(full,QString("f%1").arg(i),rnd(10,i),0,0,3,false,&err)) added++; }
    printf("  dir-full: added %d, last err: %s\n",added,qPrintable(err)); CHECK(added==47);
    CHECK(img.validate().ok());

    // --- rename + attr change
    Dir d; CHECK(img.readDir(RootSector,&d,&err));
    Entry e = d.entries[d.indexOf("small")]; e.setName("Renamed"); e.setFlag(Entry::W,false); e.setFlag(Entry::E,true); e.load=0xDEADBEEF;
    CHECK(img.replaceEntry(RootSector,"small",e,&err));
    d = Dir(); CHECK(img.readDir(RootSector,&d,&err));
    CHECK(d.indexOf("small")<0 && d.indexOf("renamed")>=0);
    CHECK(img.replaceEntry(RootSector,"NewDir",[&]{ Entry x=d.entries[d.indexOf("NewDir")]; x.setName("Moved"); return x;}(),&err));
    CHECK(img.validate().ok());

    // --- delete a subtree
    CHECK(img.deleteEntry(RootSector,"Full",&err));
    CHECK(img.deleteEntry(RootSector,"Empty",&err));
    CHECK(img.validate().ok());
    printf("free sectors: before %u now %u\n",free0,img.freeSectors());

    // --- commit and reopen
    Report post; CHECK(img.commit(&err,false,&post)); if(!err.isEmpty()) printf("  commit err: %s\n",qPrintable(err));
    CHECK(!img.dirty());
    img.close();
    CHECK(img.open(path,&err)); printf("Reopened after save:\n"); dump(img.openReport()); CHECK(img.editable());
    d=Dir(); CHECK(img.readDir(RootSector,&d,&err));
    for(const Entry&x:d.entries) printf("  %-10s %-5s load=%08X exec=%08X len=%u sin=%u seq=%02X\n",qPrintable(x.name()),qPrintable(x.attrString()),x.load,x.exec,x.length,x.sin,x.seq);
    QByteArray got; Entry bf=d.entries[d.indexOf("BigFile")]; CHECK(img.readFile(bf,&got,&err)); CHECK(got==big);
    Entry rn=d.entries[d.indexOf("Renamed")]; CHECK(rn.load==0xDEADBEEF && rn.flag(Entry::E) && !rn.flag(Entry::W));
    // original content untouched?
    CHECK(d.indexOf("!Config")>=0 && d.indexOf("panosprogs")>=0);
    Dir md; CHECK(img.readDir(d.entries[d.indexOf("Moved")].sin,&md,&err)); CHECK(md.entries.size()==2);
    { Entry tn; tn.nameRaw=md.tail.mid(1,10); printf("  Moved dir tail name: '%s' parent=%u title='%s'\n",qPrintable(tn.name()),md.parent(),qPrintable(md.title())); }

    // --- corruption: flip a byte in the map on a copy
    QString bad = path + ".bad"; QFile::remove(bad); QFile::copy(path,bad);
    { QFile f(bad); f.open(QIODevice::ReadWrite); f.seek(100); char c; f.read(&c,1); c^=0x40; f.seek(100); f.write(&c,1); }
    Image b; CHECK(b.open(bad,&err)); CHECK(!b.editable()); printf("Corrupted copy:\n"); dump(b.openReport());
    CHECK(!b.addFile(RootSector,"nope",small,0,0,3,false,&err)); printf("  refused: %s\n",qPrintable(err));
    QFile::remove(bad);

    // --- stale-on-disk detection: modify map behind our back
    Image c2; CHECK(c2.open(path,&err)); CHECK(c2.addFile(RootSector,"tmp1",small,0,0,3,false,&err));
    { QFile f(path); f.open(QIODevice::ReadWrite); Image t; QString e2; t.open(path,&e2); QByteArray m; f.seek(0); m=f.read(512);
      // legit second writer: change free map + valid checksum
      Image w; w.open(path,&e2); w.addFile(RootSector,"other",small,0,0,3,false,&e2); w.commit(&e2,false,nullptr); }
    CHECK(!c2.commit(&err,false,nullptr)); printf("  stale: %s\n",qPrintable(err));

    // ---- disc name (root title)
    { Image t; CHECK(t.open(path,&err)); CHECK(!t.setDiscTitle("My Test Disc",&err)); CHECK(t.setDiscTitle(QString(10,'x'),&err)); CHECK(!t.setDiscTitle(QString(11,'x'),&err)); CHECK(t.setDiscTitle("MyTestDisc",&err)); CHECK(!t.setDiscTitle(QString("bad\x01"),&err));
      CHECK(t.validate().ok()); CHECK(t.commit(&err,false,nullptr)); t.close(); CHECK(t.open(path,&err)); CHECK(t.openReport().ok());
      Dir r; CHECK(t.readDir(RootSector,&r,&err)); CHECK(r.title()=="MyTestDisc");
      QFile f(path); f.open(QIODevice::ReadOnly); QByteArray m=f.read(512);
      CHECK(m.mid(0xF6,5)==QByteArray(5,0) && m.mid(256+0xF6,5)==QByteArray(5,0));      // unused map name stays unused
      Image u; CHECK(u.open(path,&err)); CHECK(u.setDiscTitle("",&err)); CHECK(u.commit(&err,false,nullptr)); }
    if (argc>2) {   // an image whose free space map already holds a disc name ("OLDDISC", CR padded)
      Image t; CHECK(t.open(argv[2],&err)); CHECK(t.editable()); CHECK(t.setDiscTitle("NEW",&err)); CHECK(t.commit(&err,false,nullptr)); t.close();
      CHECK(t.open(argv[2],&err)); CHECK(t.openReport().ok());
      QFile f(argv[2]); f.open(QIODevice::ReadOnly); QByteArray m=f.read(512);
      QByteArray name; for(int i=0;i<10;i++) name.append(m[(i%2?256:0)+0xF6+i/2]);
      printf("  map disc name bytes: %s\n",name.toHex().constData());
      CHECK(name==QByteArray("NEW")+QByteArray(7,'\r')); }
    printf("\n%s (%d failures)\n",fails?"FAILED":"ALL OK",fails);
    return fails?1:0;
}
