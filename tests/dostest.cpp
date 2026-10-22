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
#include "dosfs.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <cstdio>
using namespace Adfs;
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
static QByteArray rnd(int n,int seed){ QByteArray b(n,0); quint32 s=seed*2654435761u+1; for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]=char(s>>24);} return b; }
static void dump(const Report&r){ for(auto&s:r.errors) printf("  ERR  %s\n",qPrintable(s)); for(auto&s:r.warnings) printf("  WARN %s\n",qPrintable(s)); for(auto&s:r.info) printf("  INFO %s\n",qPrintable(s)); }
static QString sha(const QByteArray&b){ return QCryptographicHash::hash(b,QCryptographicHash::Sha1).toHex(); }
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    Image img; QString err; CHECK(img.open(argv[1],&err)); CHECK(img.editable());
    Dir root; img.readDir(RootSector,&root,&err); Dir dd; img.readDir(root.entries[root.indexOf("DOS")].sin,&dd,&err);
    Entry dc=dd.entries[dd.indexOf("DRIVE_C")];
    Dos::Volume v; CHECK(v.attach(&img,dc.sin,dc.length,&err)); printf("attach: %s label='%s' fat16=%d cluster=%u\n",qPrintable(err),qPrintable(v.label()),v.isFat16(),v.clusterBytes());
    Report r=v.check(); printf("Original DRIVE_C:\n"); dump(r); CHECK(r.ok());
    QVector<Dos::DEntry> L; CHECK(v.listDir(0,&L,&err)); printf("root entries: %d\n",L.size());
    for(auto&e:L) if(e.name=="MEMDISK.COM"||e.name=="DOSPLUS.SYS"||e.name=="COMMAND.COM"){ QByteArray b; CHECK(v.readFile(e,&b,&err)); printf("  %s %u sha1=%s\n",qPrintable(e.name),e.size,qPrintable(sha(b))); CHECK(quint32(b.size())==e.size); }
    quint32 free0=v.freeClusters();

    // add files
    QByteArray big=rnd(300000,1), small=rnd(100,2);
    CHECK(v.addFile(0,"BIG.DAT",big,false,&err)); CHECK(v.addFile(0,"small.txt",small,false,&err)); CHECK(v.addFile(0,"EMPTY",QByteArray(),false,&err));
    CHECK(!v.addFile(0,"BIG.DAT",small,false,&err)); printf("  dup: %s\n",qPrintable(err));
    CHECK(!v.addFile(0,"toolongname.txt",small,false,&err)); CHECK(!v.addFile(0,"a b.txt",small,false,&err)); CHECK(!v.addFile(0,"A.TOOLONG",small,false,&err));
    CHECK(v.addFile(0,"SMALL.TXT",rnd(5000,3),true,&err));   // replace (case-insens)
    quint32 sub=0; CHECK(v.makeDir(0,"SUBDIR",&err,&sub)); CHECK(sub>=2);
    quint32 sub2=0; CHECK(v.makeDir(sub,"DEEP",&err,&sub2));
    CHECK(v.addFile(sub2,"X.BIN",rnd(9000,4),false,&err));
    // directory growth: 130 files in SUBDIR (> 128 slots per 4K cluster, minus . and ..)
    int ok=0; for(int i=0;i<140;i++) if(v.addFile(sub,QString("F%1.TXT").arg(i),rnd(10+i,10+i),false,&err)) ok++; CHECK(ok==140);
    r=v.check(); printf("After adds:\n"); dump(r); CHECK(r.ok());
    // disc full: rollback must be exact
    quint32 fb=v.freeClusters(); CHECK(!v.addFile(0,"HUGE.BIN",QByteArray(40*1024*1024,'x'),false,&err)); printf("  full: %s\n",qPrintable(err)); CHECK(v.freeClusters()==fb); CHECK(v.check().ok());
    // modify
    CHECK(v.modify(0,"SMALL.TXT","RENAMED.TXT",Dos::AttrR|Dos::AttrA,&err)); CHECK(!v.modify(0,"RENAMED.TXT","BIG.DAT",0,&err));
    CHECK(v.modify(0,"MEMDISK.COM","MEMDISK.COM",Dos::AttrS|Dos::AttrH|Dos::AttrR,&err));
    // delete
    CHECK(v.remove(0,"EMPTY",&err)); CHECK(v.remove(sub,"DEEP",&err)); CHECK(!v.remove(0,"NOPE.TXT",&err));
    r=v.check(); CHECK(r.ok()); dump(r);
    printf("free clusters: before %u now %u\n",free0,v.freeClusters());

    Report post; CHECK(img.commit(&err,false,&post)); CHECK(!img.dirty()); img.close();
    CHECK(img.open(argv[1],&err)); CHECK(img.openReport().ok());
    img.readDir(RootSector,&root,&err); img.readDir(root.entries[root.indexOf("DOS")].sin,&dd,&err); dc=dd.entries[dd.indexOf("DRIVE_C")];
    Dos::Volume v2; CHECK(v2.attach(&img,dc.sin,dc.length,&err)); r=v2.check(); printf("Reopened:\n"); dump(r); CHECK(r.ok());
    L.clear(); v2.listDir(0,&L,&err);
    for(auto&e:L) if(e.name=="BIG.DAT"){ QByteArray b; CHECK(v2.readFile(e,&b,&err)); CHECK(b==big); }
    for(auto&e:L) if(e.name=="MEMDISK.COM") CHECK(e.attr&Dos::AttrH && e.attr&Dos::AttrS);
    printf("big.dat sha1=%s\n",qPrintable(sha(big)));

    // ---- gate test: corrupt the FAT behind the DOS layer's back, check() must object and the save must be vetoed
    Image i3; CHECK(i3.open(argv[1],&err)); Dos::Volume v3; CHECK(v3.attach(&i3,dc.sin,dc.length,&err));
    i3.setExtraValidator([&]{ QStringList o; if(i3.pendingTouches(dc.sin,sectorsFor(dc.length))) for(auto&e:v3.check().errors) o<<e; return o; });
    CHECK(v3.addFile(0,"OK.TXT",small,false,&err));
    CHECK(i3.commit(&err,false,nullptr));                       // healthy: allowed
    QVector<Dos::DEntry> L3; v3.listDir(0,&L3,&err); quint32 cA=0,cB=0; for(auto&e:L3){ if(e.name=="BIG.DAT") cA=e.cluster; if(e.name=="OK.TXT") cB=e.cluster; }
    QByteArray w(2,0); w[0]=char(cA&255); w[1]=char(cA>>8);                       // FAT16: point OK.TXT's FAT entry at BIG.DAT's first cluster
    CHECK(i3.writeFileBytes(dc.sin,dc.length,1024+cB*2,w,&err));
    Report bad=v3.check(); printf("Corrupted FAT:\n"); dump(bad); CHECK(!bad.ok());
    CHECK(!i3.commit(&err,false,nullptr)); printf("  veto: %s\n",qPrintable(err.left(160)));

    // ---- volume label
    { Image i4; CHECK(i4.open(argv[1],&err)); Dir r4,d4; i4.readDir(RootSector,&r4,&err); i4.readDir(r4.entries[r4.indexOf("DOS")].sin,&d4,&err); Entry dc4=d4.entries[d4.indexOf("DRIVE_C")];
      Dos::Volume v4; CHECK(v4.attach(&i4,dc4.sin,dc4.length,&err)); printf("  label before: '%s'\n",qPrintable(v4.label()));
      CHECK(v4.setLabel("beeb disk",&err)); CHECK(v4.label()=="BEEB DISK");
      CHECK(!v4.setLabel("WAY TOO LONG LABEL",&err)); CHECK(!v4.setLabel("BAD*CHAR",&err));
      CHECK(v4.check().ok()); QVector<Dos::DEntry> L4; v4.listDir(0,&L4,&err); for(auto&e:L4) CHECK(e.name!="BEEB DISK");   // label is not listed as a file
      CHECK(i4.commit(&err,false,nullptr)); i4.close(); CHECK(i4.open(argv[1],&err));
      Dos::Volume v5; i4.readDir(RootSector,&r4,&err); i4.readDir(r4.entries[r4.indexOf("DOS")].sin,&d4,&err); dc4=d4.entries[d4.indexOf("DRIVE_C")];
      CHECK(v5.attach(&i4,dc4.sin,dc4.length,&err)); CHECK(v5.label()=="BEEB DISK"); CHECK(v5.check().ok());
      CHECK(v5.setLabel("",&err)); CHECK(v5.label().isEmpty()); CHECK(v5.check().ok());
      CHECK(v5.setLabel("MASTER 512",&err)); CHECK(i4.commit(&err,false,nullptr)); }
    printf("\n%s (%d failures)\n",fails?"FAILED":"ALL OK",fails); return fails?1:0;
}
