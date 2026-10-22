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
#include "diskformat.h"
#include "dosfs.h"
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
using namespace Adfs;
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
static QByteArray rnd(int n,int seed){ QByteArray b(n,0); quint32 s=seed*2654435761u+1; for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]=char(s>>24);} return b; }
static void dump(const Report&r){ for(auto&s:r.errors) printf("  ERR  %s\n",qPrintable(s)); for(auto&s:r.warnings) printf("  WARN %s\n",qPrintable(s)); for(auto&s:r.info) printf("  INFO %s\n",qPrintable(s)); }

// exercise a volume: add, subdir, replace, delete, rename, label, check, commit, reopen
static void exercise(const QString& path, Dos::Format fmt, bool useBpb, const char* tag){
    printf("== %s\n",tag);
    QFile f(path); f.open(QIODevice::ReadOnly); quint64 size=f.size(); f.close();
    Image img; QString err; CHECK(img.openRaw(path,size,&err)); CHECK(img.editable());
    Dos::Volume v; bool ok = useBpb ? v.attach(&img,0,quint32(size),&err) : v.attachFormat(&img,0,quint32(size),fmt,&err);
    printf("  attach: %d %s  fat16=%d cluster=%u bps=%u total clusters=%u free=%u\n",ok,qPrintable(err),v.isFat16(),v.clusterBytes(),v.bytesPerSector(),v.totalClusters(),v.freeClusters());
    CHECK(ok); if(!ok) return;
    Report r=v.check(); dump(r); CHECK(r.ok());
    QByteArray a=rnd(40000,1), b=rnd(777,2);
    CHECK(v.addFile(0,"A.BIN",a,false,&err)); CHECK(v.addFile(0,"readme.txt",b,false,&err));
    quint32 d=0; CHECK(v.makeDir(0,"SUB",&err,&d)); CHECK(v.addFile(d,"IN.DAT",rnd(5000,3),false,&err));
    CHECK(v.addFile(0,"A.BIN",rnd(100,4),true,&err)); CHECK(v.addFile(0,"A.BIN",a,true,&err));
    CHECK(v.modify(0,"README.TXT","NOTES.TXT",Dos::AttrR|Dos::AttrA,&err));
    CHECK(v.setLabel("TESTVOL",&err)); CHECK(v.label()=="TESTVOL");
    CHECK(!v.addFile(0,"HUGE.BIN",QByteArray(3*1024*1024,'x'),false,&err)); printf("  full: %s\n",qPrintable(err));
    r=v.check(); dump(r); CHECK(r.ok());
    CHECK(img.commit(&err,false,nullptr)); img.close();
    CHECK(img.openRaw(path,size,&err));
    Dos::Volume v2; ok = useBpb ? v2.attach(&img,0,quint32(size),&err) : v2.attachFormat(&img,0,quint32(size),fmt,&err); CHECK(ok);
    r=v2.check(); CHECK(r.ok()); CHECK(v2.label()=="TESTVOL");
    QVector<Dos::DEntry> L; v2.listDir(0,&L,&err); bool fa=false,fb=false; for(auto&e:L){ QByteArray x; if(e.name=="A.BIN"){ CHECK(v2.readFile(e,&x,&err)); fa=(x==a);} if(e.name=="NOTES.TXT"){ v2.readFile(e,&x,&err); fb=(x==b)&&(e.attr&Dos::AttrR);} }
    CHECK(fa&&fb); CHECK(QFile(path).size()==qint64(size));
}

int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    QString mode=argv[1]; QString path=argv[2];
    if(mode=="bpb")      exercise(path,Dos::FmtPc720,true,"PC image via BPB");
    else if(mode=="table") exercise(path,Dos::Format(atoi(argv[3])),false,"PC image via format table");
    else if(mode=="acorn800"){
        QByteArray img(819200,0); Dos::Params p=Dos::Volume::standard(Dos::FmtAcorn800);
        img[0]=char(p.media); img[1]=char(0xFF); img[2]=char(0xFF);
        QFile f(path); f.open(QIODevice::WriteOnly); f.write(img); f.close();
        exercise(path,Dos::FmtAcorn800,false,"Acorn 800K (no boot record)");
    } else if(mode=="hybrid640"){
        QByteArray img=makeBlankImage(2560,"HYBRID",false); Dos::Params p=Dos::Volume::standard(Dos::FmtAcorn640);
        int fat=2*4096; /* logical sector 16 = file track 2 (tracks interleaved) */ img[fat]=char(p.media); img[fat+1]=char(0xFF); img[fat+2]=char(0xFF);
        QFile f(path); f.open(QIODevice::WriteOnly); f.write(img); f.close();
        // ADFS side must still be valid, and DOS edits must not disturb it
        Image a; QString e; CHECK(a.open(path,&e)); printf("  hybrid ADFS side:\n"); dump(a.openReport()); CHECK(a.openReport().ok()); a.setAdfsLocked(true);
        Dos::Volume v; CHECK(v.attachFormat(&a,0,655360,Dos::FmtAcorn640,&e)); printf("  dos: clusters=%u free=%u cluster bytes=%u\n",v.totalClusters(),v.freeClusters(),v.clusterBytes());
        CHECK(v.check().ok());
        CHECK(v.addFile(0,"HELLO.TXT",rnd(5000,7),false,&e)); CHECK(v.makeDir(0,"D",&e));
        CHECK(!a.addFile(RootSector,"X",QByteArray("x"),0,0,3,false,&e)); printf("  ADFS write refused: %s\n",qPrintable(e));
        CHECK(a.commit(&e,false,nullptr)); a.close();
        Image b; CHECK(b.open(path,&e)); CHECK(b.openReport().ok()); Dos::Volume w; CHECK(w.attachFormat(&b,0,655360,Dos::FmtAcorn640,&e)); CHECK(w.check().ok());
        QVector<Dos::DEntry> L; w.listDir(0,&L,&e); CHECK(L.size()==2);
    } else if(mode=="plainl"){
        // an ordinary ADFS L floppy (blank or not) must NOT be mistaken for a DOS Plus hybrid
        DiskInfo info=detectDisk(path); printf("  detected: %s\n",qPrintable(info.description));
        CHECK(info.kind==DiskInfo::Adfs); CHECK(info.fullSize==655360);
    } else if(mode=="real800"){
        // A REAL Acorn DOS Plus 800K disc (M512_3.IMG, a GEM disc; pass a copy, it is modified). Stored sequentially (NOT track-interleaved).
        QByteArray before; { QFile f(path); f.open(QIODevice::ReadOnly); before=f.readAll(); }
        CHECK(before.size()==819200);
        DiskInfo info=detectDisk(path); printf("  detected: %s\n",qPrintable(info.description));
        CHECK(info.kind==DiskInfo::DosAcorn800); CHECK(info.fullSize==819200); CHECK(info.notes.isEmpty());
        Image a; QString e; CHECK(a.openRaw(path,819200,&e)); CHECK(a.editable());
        Dos::Volume v; CHECK(v.attachFormat(&a,0,819200,Dos::FmtAcorn800,&e));
        printf("  dos: label '%s' clusters=%u free=%u cluster bytes=%u\n",qPrintable(v.label()),v.totalClusters(),v.freeClusters(),v.clusterBytes());
        Report r0=v.check(); dump(r0); CHECK(r0.ok()); CHECK(v.label()=="M512 Disk 3"); CHECK(v.clusterBytes()==1024 && v.totalClusters()==792);
        QVector<Dos::DEntry> L0; CHECK(v.listDir(0,&L0,&e)); printf("  root entries: %d\n",L0.size()); CHECK(L0.size()==42);
        // CONTENT checks (a valid directory/FAT proves nothing about where the data is)
        bool issue=false,font=false,rsc=false;
        for(auto&x:L0){ QByteArray b;
            if(x.name=="ISSUE.BAT"){ issue=true; CHECK(v.readFile(x,&b,&e)); CHECK(b.startsWith("echo off\r\ncls\r\n")); CHECK(b.contains("This is an issue 2 disk")); }
            if(x.name=="DIBHSS07.FNT"){ font=true; CHECK(v.readFile(x,&b,&e)); CHECK(b.size()==2560); CHECK(b.mid(4,5)=="Swiss"); }      // GEM font header
            if(x.name=="SNAPSHOT.RSC"){ rsc=true; CHECK(v.readFile(x,&b,&e)); CHECK(b.contains("How to Use Snapshot")); } }
        CHECK(issue && font && rsc);
        // directories (DOCUMENT, IMAGES) must be readable too
        for(auto&x:L0) if(x.isDir()){ QVector<Dos::DEntry> sub; CHECK(v.listDir(x.cluster,&sub,&e)); printf("  dir %s: %d entries\n",qPrintable(x.name),sub.size()); }
        CHECK(a.commit(&e,false,nullptr)); { QFile f(path); f.open(QIODevice::ReadOnly); CHECK(f.readAll()==before); }   // untouched save is byte-identical
        quint32 freeBefore=v.freeClusters();
        CHECK(v.addFile(0,"HELLO.TXT",rnd(5000,7),false,&e)); quint32 d=0; CHECK(v.makeDir(0,"NEWDIR",&e,&d)); CHECK(v.addFile(d,"IN.DAT",rnd(3000,9),false,&e));
        CHECK(v.freeClusters()<freeBefore); CHECK(v.check().ok());
        CHECK(a.commit(&e,false,nullptr)); a.close();
        QByteArray after; { QFile f(path); f.open(QIODevice::ReadOnly); after=f.readAll(); }
        CHECK(after.size()==819200);
        Image b; CHECK(b.openRaw(path,819200,&e)); Dos::Volume w; CHECK(w.attachFormat(&b,0,819200,Dos::FmtAcorn800,&e)); Report r1=w.check(); dump(r1); CHECK(r1.ok());
        QVector<Dos::DEntry> L1; w.listDir(0,&L1,&e); CHECK(L1.size()==L0.size()+2);
        QByteArray back; for(auto&x:L1) if(x.name=="HELLO.TXT"){ CHECK(w.readFile(x,&back,&e)); CHECK(back==rnd(5000,7)); }
        for(auto&x:L1) if(x.name=="ISSUE.BAT"){ QByteArray b2; CHECK(w.readFile(x,&b2,&e)); CHECK(b2.startsWith("echo off")); }
    } else if(mode=="real640"){
        // A REAL Acorn 640K ADFS/DOS Plus hybrid disc (the file is supplied by the caller; it is modified, so pass a copy).
        QByteArray before; { QFile f(path); f.open(QIODevice::ReadOnly); before=f.readAll(); }
        CHECK(before.size()==655360);
        DiskInfo info=detectDisk(path); printf("  detected: %s | full=%llu\n",qPrintable(info.description),(unsigned long long)info.fullSize);
        CHECK(info.kind==DiskInfo::AdfsDosHybrid); CHECK(info.fullSize==655360); CHECK(info.notes.isEmpty());   // never padded
        Image a; QString e; CHECK(a.open(path,&e,info.fullSize)); printf("  ADFS side:\n"); dump(a.openReport()); CHECK(a.openReport().ok());
        CHECK(!a.needsPadding()); CHECK(a.fullSize()==655360); a.setAdfsLocked(true);
        Dir root; CHECK(a.readDir(RootSector,&root,&e)); CHECK(root.indexOf("dosboot")>=0); printf("  ADFS title '%s', %d entries\n",qPrintable(root.title()),root.entries.size());
        Dos::Volume v; CHECK(v.attachFormat(&a,0,655360,Dos::FmtAcorn640,&e));
        printf("  dos: label '%s' clusters=%u free=%u cluster bytes=%u\n",qPrintable(v.label()),v.totalClusters(),v.freeClusters(),v.clusterBytes());
        Report r0=v.check(); dump(r0); CHECK(r0.ok()); CHECK(v.clusterBytes()==2048 && v.totalClusters()==316);
        QVector<Dos::DEntry> L0; CHECK(v.listDir(0,&L0,&e)); printf("  root entries: %d\n",L0.size());
        bool cmd=false; for(auto&x:L0) if(x.name=="COMMAND.COM"){ cmd=true; CHECK(x.size==26888); QByteArray b; CHECK(v.readFile(x,&b,&e)); CHECK(b.size()==26888); } CHECK(cmd);
        // CONTENT checks (structure alone proved nothing: the first version had the right directory and the wrong data offset)
        for(auto&x:L0) if(x.name=="ISSUE.BAT"){ QByteArray b; CHECK(v.readFile(x,&b,&e)); CHECK(b.startsWith("echo off")); int bad=0; for(char c:b.left(70)) if(c!='\r'&&c!='\n'&&c!=0x1A&&(uchar(c)<32||uchar(c)>126)) ++bad; printf("  ISSUE.BAT: '%s' non-text bytes=%d\n",qPrintable(QString::fromLatin1(b.left(30)).simplified()),bad); CHECK(bad==0); }
        if(argc>3){   // compare every same-size file with the copy extracted from the hard disc's DOS partition (tests/extract_hd_dos.py)
            int same=0,diff=0;
            for(auto&x:L0){ QFile g(QString(argv[3])+"/"+x.name); if(!g.open(QIODevice::ReadOnly)) continue; QByteArray ref=g.readAll(); if(quint32(ref.size())!=x.size) continue;
                QByteArray b; CHECK(v.readFile(x,&b,&e)); if(b==ref) ++same; else { ++diff; printf("  DIFFERS: %s\n",qPrintable(x.name)); } }
            printf("  files identical to the hard-disc copies: %d, different: %d\n",same,diff); CHECK(same>=25); CHECK(diff==0);
        }
        // saving with no changes must not touch the file at all
        CHECK(a.commit(&e,false,nullptr)); { QFile f(path); f.open(QIODevice::ReadOnly); CHECK(f.readAll()==before); }
        // DOS edits: add a file and a directory, replace nothing
        quint32 freeBefore=v.freeClusters();
        CHECK(v.addFile(0,"HELLO.TXT",rnd(5000,7),false,&e)); quint32 d=0; CHECK(v.makeDir(0,"NEWDIR",&e,&d)); CHECK(v.addFile(d,"IN.DAT",rnd(3000,9),false,&e));
        CHECK(!a.addFile(RootSector,"X",QByteArray("x"),0,0,3,false,&e)); printf("  ADFS write refused: %s\n",qPrintable(e));
        CHECK(v.freeClusters()<freeBefore); CHECK(v.check().ok());
        CHECK(a.commit(&e,false,nullptr)); a.close();
        QByteArray after; { QFile f(path); f.open(QIODevice::ReadOnly); after=f.readAll(); }
        CHECK(after.size()==655360);                                   // the file did not grow
        CHECK(after.left(16*256)==before.left(16*256));                // file track 0: ADFS catalogue + DOSBOOT untouched
        int changed=0; for(int s=0;s<2560;++s) if(after.mid(s*256,256)!=before.mid(s*256,256)) ++changed; printf("  sectors changed by the DOS edits: %d \n",changed);
        Image b; CHECK(b.open(path,&e,655360)); CHECK(b.openReport().ok()); Dos::Volume w; CHECK(w.attachFormat(&b,0,655360,Dos::FmtAcorn640,&e)); Report r1=w.check(); dump(r1); CHECK(r1.ok());
        QVector<Dos::DEntry> L1; w.listDir(0,&L1,&e); CHECK(L1.size()==L0.size()+2);
        QByteArray back; for(auto&x:L1) if(x.name=="HELLO.TXT"){ CHECK(w.readFile(x,&back,&e)); CHECK(back==rnd(5000,7)); }
    }
    printf("%s (%d failures)\n",fails?"FAILED":"ALL OK",fails); return fails?1:0;
}
