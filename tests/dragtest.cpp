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
// Drag-out tests (offscreen):   dragtest <outdir>
// The drag data a tree makes is driven the way a drop target would drive it: Explorer / a file manager asks for
// text/uri-list, another copy of the program or another tool asks for the retro-files JSON
// (docs/retro-files-format.md). Checked: nothing is written until a target asks; the files, folders, names, .inf
// sidecars (per the option) and dates; the JSON; drops between windows of one process still copy directly
// (nothing written); a drop from "another process" goes through the JSON and keeps the attributes; a drop back
// onto its own pane is refused; a read-only image can be dragged from; readJson refuses unsafe lists;
// cleanStale. The JSON and files of each drag are left in <outdir>/drag-* for tests/retrofilescheck.py.
#include "mainwindow.h"
#include "dragout.h"
#include "newdisc.h"
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractButton>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <cstdio>
#include <utime.h>
using namespace Adfs;
static int fails = 0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)
static QByteArray rnd(int n,int seed){ QByteArray b(n,0); quint32 s=seed*2654435761u+1; for(int i=0;i<n;i++){ s=s*1664525u+1013904223u; b[i]=char(s>>24);} return b; }
static QByteArray slurp(const QString &p){ QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void pump(){ for(int i=0;i<5;++i){ QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);} }
static QTreeWidgetItem* findIn(QTreeWidgetItem* it,const QString& name){
    if(it->data(0,RoleName).toString()==name && !it->data(0,RoleIsRoot).toBool()) return it;
    for(int i=0;i<it->childCount();++i) if(auto*r=findIn(it->child(i),name)) return r;
    return nullptr; }
static QTreeWidgetItem* find(QTreeWidget* t,const QString& name){ return t->topLevelItemCount()?findIn(t->topLevelItem(0),name):nullptr; }
static void sendDrop(QTreeWidget* tree,QMimeData* mime){
    QPoint pt(5,tree->viewport()->height()-5);
    QDragEnterEvent en(pt,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&en);
    QDragMoveEvent mv(pt,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&mv);
    QDropEvent ev(QPointF(pt),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&ev);
    pump();
}
static void sendDropOn(QTreeWidget* tree,QTreeWidgetItem* it,QMimeData* mime){      // onto a row (e.g. a directory)
    tree->scrollToItem(it); QPoint pt=tree->visualItemRect(it).center();
    QDragEnterEvent en(pt,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&en);
    QDragMoveEvent mv(pt,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&mv);
    QDropEvent ev(QPointF(pt),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&ev);
    pump();
}
// Answers the next QMessageBoxes in turn with the given buttons; prompts() says how many were answered.
static QList<QMessageBox::StandardButton> g_answers; static int g_answered=0; static QStringList g_promptTexts;
static void answer(QList<QMessageBox::StandardButton> seq){
    g_answers=seq; g_answered=0; g_promptTexts.clear();
    auto* t=new QTimer; t->setInterval(50);
    QObject::connect(t,&QTimer::timeout,[t]{
        if(g_answers.isEmpty()){ t->deleteLater(); return; }
        auto* mb=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(!mb) return;
        QAbstractButton* b=mb->button(g_answers.takeFirst()); g_promptTexts<<mb->text(); ++g_answered;
        if(b) b->click(); else { printf("  !! prompt has no such button\n"); mb->close(); } });
    t->start();
}
static int dragDirs(){ QFileInfo p(RetroFiles::dragDirPrefix()); return QDir(p.absolutePath()).entryList({p.fileName()+"*"},QDir::Dirs|QDir::NoDotAndDotDot).size(); }
static RetroFiles::DragOutMime* mimeFor(MainWindow& w, DropTree* t, QList<QTreeWidgetItem*> items){
    w.tabs()->setCurrentWidget(t); pump();
    return qobject_cast<RetroFiles::DragOutMime*>(t->makeMime(items));
}
static QJsonObject jsonOf(QMimeData* m){ return QJsonDocument::fromJson(m->data(RetroFiles::MimeType)).object(); }
static QJsonObject itemAt(const QJsonObject& j,const QString& path){ for(auto v:j["items"].toArray()) if(v.toObject()["path"].toString()==path) return v.toObject(); return QJsonObject(); }
// keep a copy of what a drag produced, for retrofilescheck.py
static void keep(QMimeData* m,const QString& out,const QString& tag){
    QJsonObject j=jsonOf(m); QString base=QUrl(j["base"].toString()).toLocalFile(), dst=QDir(out).absoluteFilePath("drag-"+tag);
    QDir(dst).removeRecursively(); QDir().mkpath(dst);
    QDirIterator it(base,QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()){ QString p=it.next(), rel=QDir(base).relativeFilePath(p); if(QFileInfo(p).isDir()) QDir().mkpath(dst+"/files/"+rel); else { QDir().mkpath(QFileInfo(dst+"/files/"+rel).absolutePath()); QFile::copy(p,dst+"/files/"+rel); QFile c(dst+"/files/"+rel); if(c.open(QIODevice::Append)) c.setFileTime(QFileInfo(p).lastModified(),QFileDevice::FileModificationTime);} }
    j["base"]=QUrl::fromLocalFile(dst+"/files").toString();
    QFile f(dst+"/list.json"); f.open(QIODevice::WriteOnly); f.write(QJsonDocument(j).toJson());
    QStringList uris; for(auto&u:m->urls()) uris<<QDir(base).relativeFilePath(u.toLocalFile());
    QFile g(dst+"/uri-list.txt"); g.open(QIODevice::WriteOnly); g.write(uris.join("\n").toUtf8()+"\n");
}
// a copy of a drag as it would arrive from ANOTHER process: same formats, but the private tag names another pid
static QMimeData* asForeign(QMimeData* m){
    auto* f=new QMimeData;
    for(const QString& fmt:{QString(WRANGLER_MIME_ITEMS),QString(RetroFiles::MimeType)}) f->setData(fmt,m->data(fmt));
    QList<QByteArray> tag=m->data(WRANGLER_MIME_SOURCE).split(':'); tag[0]=QByteArray::number(tag[0].toLongLong()+1);
    f->setData(WRANGLER_MIME_SOURCE,tag.join(':'));
    f->setUrls(m->urls());
    return f;
}

int main(int argc,char**argv){
    QApplication app(argc,argv);
    QApplication::setOrganizationName("acorndiskwrangler-dragtest"); QApplication::setApplicationName("t");
    QSettings().clear(); QSettings().setValue("writeInf",true);
    if(argc<2){ printf("usage: dragtest <outdir>\n"); return 2; }
    QString out=argv[1]; QDir().mkpath(out); QString e;
    QTimer guard; guard.setInterval(2000);                        // an unexpected dialog is a failure, never a hang
    QObject::connect(&guard,&QTimer::timeout,[]{ if(QWidget* m=QApplication::activeModalWidget()){ QString t; if(auto* mb=qobject_cast<QMessageBox*>(m)) t=mb->text();
        printf("  !! unexpected dialog: %s\n",qPrintable(t.replace('\n',' '))); ++fails; m->close(); } });
    guard.start();

    // ---- test discs
    QByteArray elite=rnd(21504,1), elite2=rnd(3000,2), tiny=rnd(77,3), aux=rnd(500,4), stamped=rnd(1000,5);
    QString adl=out+"/src.adl"; CHECK(NewDisc::create(adl,NewDisc::AdfsL,"DRAG",false,&e));
    QDateTime when(QDate(1994,6,1),QTime(13,30,0));
    { Image im; CHECK(im.open(adl,&e,0,detectDisk(adl).sides));
      CHECK(im.addFile(RootSector,"Elite",elite,0xFFFF1900,0xFFFF8023,FlagL|FlagW|FlagR,false,&e));
      CHECK(im.addFile(RootSector,"Aux",aux,0x1900,0x1900,FlagW|FlagR,false,&e));
      // a RISC OS typed + time-stamped file: centiseconds since 1900 in exec + low byte of load, type FFF
      quint64 cs=quint64(QDateTime(QDate(1900,1,1),QTime(0,0),Qt::UTC).secsTo(QDateTime(when.date(),when.time(),Qt::UTC)))*100;
      CHECK(im.addFile(RootSector,"Text",stamped,0xFFFFFF00u|quint32(cs>>32),quint32(cs),FlagW|FlagR,false,&e));
      quint32 games=0,deep=0; CHECK(im.makeDir(RootSector,"Games",&e,&games));
      CHECK(im.addFile(games,"Elite",elite2,0x3000,0x3000,FlagR|FlagE,false,&e));
      CHECK(im.makeDir(games,"Deep",&e,&deep)); CHECK(im.addFile(deep,"Tiny",tiny,0,0,FlagR,false,&e));
      CHECK(im.commit(&e,false,nullptr)); }
    QString dsd=out+"/src.dsd"; CHECK(NewDisc::create(dsd,NewDisc::DfsDs40,"",false,&e));
    QByteArray boot=rnd(300,6), aboot=rnd(1234,7);
    { DiskInfo i=detectDisk(dsd); Image im; CHECK(im.openRaw(dsd,i.fullSize,&e,i.sides)); Dfs::Disc d; CHECK(d.attach(&im,0,i.dfs,&e));
      CHECK(d.addFile('$',"BOOT",boot,0x31900,0x38023,false,false,&e)); CHECK(d.addFile('A',"BOOT",aboot,0x1100,0x1200,true,false,&e)); CHECK(im.commit(&e,false,nullptr)); }
    QString pc=out+"/src.img"; CHECK(NewDisc::create(pc,NewDisc::Pc720,"",false,&e));
    QByteArray readme=rnd(4000,8), xbin=rnd(9000,9);
    { Image im; CHECK(im.openRaw(pc,737280,&e)); Dos::Volume v; CHECK(v.attach(&im,0,737280,&e));
      CHECK(v.addFile(0,"README.TXT",readme,false,&e)); quint32 sub=0; CHECK(v.makeDir(0,"SUB",&e,&sub)); CHECK(v.addFile(sub,"X.BIN",xbin,false,&e));
      CHECK(v.modify(sub,"X.BIN","X.BIN",Dos::AttrR|Dos::AttrH|Dos::AttrS,&e)); CHECK(im.commit(&e,false,nullptr)); }

    // ---- ADFS: lazy, files, names, .inf, dates, JSON
    printf("== ADFS drag out\n");
    { MainWindow w; w.show(); CHECK(w.openImage(adl)); pump();
      DropTree* t=w.tree(); auto* games=find(t,"Games"); auto* gElite=games?findIn(games,"Elite"):nullptr;
      QList<QTreeWidgetItem*> sel={find(t,"Elite"),find(t,"Aux"),find(t,"Text"),games};
      for(auto* x:sel) CHECK(x);
      int before=dragDirs();
      auto* m=mimeFor(w,t,sel); CHECK(m);
      CHECK(m->hasFormat("text/uri-list")); CHECK(m->hasUrls()); CHECK(m->hasFormat(RetroFiles::MimeType));
      CHECK(!m->state()->extracted); CHECK(dragDirs()==before);              // nothing written yet
      QList<QUrl> urls=m->urls(); CHECK(m->state()->extracted); CHECK(dragDirs()==before+1);
      QJsonObject j=jsonOf(m); QString base=QUrl(j["base"].toString()).toLocalFile();
      QStringList names; for(auto&u:urls) names<<QFileInfo(u.toLocalFile()).fileName(); names.sort();
      printf("  dropped on the host: %s\n",qPrintable(names.join(", ")));
      CHECK(names==QStringList({"Aux_","Aux_.inf","Elite","Elite.inf","Games","Text","Text.inf"}));
      CHECK(slurp(base+"/Elite")==elite); CHECK(slurp(base+"/Aux_")==aux);
      CHECK(slurp(base+"/Games/Elite")==elite2); CHECK(slurp(base+"/Games/Deep/Tiny")==tiny); CHECK(QFileInfo::exists(base+"/Games/Elite.inf"));
      CHECK(slurp(base+"/Elite.inf")=="Elite      FFFF1900 FFFF8023 00005400 0B\n");        // as Extract to host writes it
      CHECK(slurp(base+"/Games/Elite.inf")=="Elite      00003000 00003000 00000BB8 05\n");
      CHECK(QFileInfo(base+"/Text").lastModified()==when);                                 // date from the RISC OS stamp
      CHECK(j["format"].toString()=="retro-files"); CHECK(j["version"].toInt()==1);
      CHECK(j["source"].toObject()["filesystem"].toString()=="adfs"); CHECK(j["source"].toObject()["image"].toString()=="src.adl");
      CHECK(j["items"].toArray().size()==7);                                              // 4 files + Games, Deep, Tiny
      QJsonObject a=itemAt(j,"Elite"); CHECK(a["name"].toString()=="Elite"); CHECK(a["size"].toInt()==21504);
      CHECK(a["attributes"].toObject()["acorn"].toObject()["load"].toString()=="FFFF1900");
      CHECK(a["attributes"].toObject()["acorn"].toObject()["access"].toString()=="LWR");
      CHECK(itemAt(j,"Aux_")["name"].toString()=="Aux");
      CHECK(itemAt(j,"Text")["modified"].toString()=="1994-06-01T13:30:00");
      CHECK(itemAt(j,"Games")["type"].toString()=="directory"); CHECK(itemAt(j,"Games/Deep/Tiny")["size"].toInt()==77);
      CHECK(itemAt(j,"Elite.inf").isEmpty());                                             // sidecars are not items
      keep(m,out,"adfs");
      delete m;
      // two selected items with the same name (from different directories) are made unique on the host
      auto* m1=mimeFor(w,t,{find(t,"Elite"),gElite});
      QStringList n1; for(auto&u:m1->urls()) n1<<QFileInfo(u.toLocalFile()).fileName(); n1.sort();
      CHECK(n1==QStringList({"Elite","Elite.inf","Elite_2","Elite_2.inf"}));
      QJsonObject j1=jsonOf(m1); QString b1=QUrl(j1["base"].toString()).toLocalFile();
      CHECK(slurp(b1+"/Elite")==elite); CHECK(slurp(b1+"/Elite_2")==elite2); CHECK(itemAt(j1,"Elite_2")["name"].toString()=="Elite");
      CHECK(slurp(b1+"/Elite_2.inf").startsWith("Elite "));                              // the .inf keeps the real name
      keep(m1,out,"clash"); delete m1;
      // .inf off: none on the host
      QSettings().setValue("writeInf",false);
      { MainWindow w2; w2.show(); CHECK(w2.openImage(adl)); pump(); auto* m2=mimeFor(w2,w2.tree(),{find(w2.tree(),"Elite")});
        QStringList n2; for(auto&u:m2->urls()) n2<<QFileInfo(u.toLocalFile()).fileName(); CHECK(n2==QStringList({"Elite"})); delete m2; }
      QSettings().setValue("writeInf",true);
    }

    // ---- DFS and DOS drag out
    printf("== DFS and DOS drag out\n");
    { MainWindow w; w.show(); CHECK(w.openImage(dsd)); pump();
      auto* m=mimeFor(w,w.dfsTree(0),{find(w.dfsTree(0),"$.BOOT"),find(w.dfsTree(0),"A.BOOT")});
      QStringList n; for(auto&u:m->urls()) n<<QFileInfo(u.toLocalFile()).fileName(); n.sort();
      CHECK(n==QStringList({"A.BOOT","A.BOOT.inf","BOOT","BOOT.inf"}));
      QJsonObject j=jsonOf(m); QString base=QUrl(j["base"].toString()).toLocalFile();
      CHECK(slurp(base+"/BOOT")==boot); CHECK(slurp(base+"/A.BOOT")==aboot);
      CHECK(slurp(base+"/BOOT.inf")=="$.BOOT    FFFF1900 FFFF8023 0000012C 03\n");
      QJsonObject a=itemAt(j,"A.BOOT"); CHECK(a["name"].toString()=="A.BOOT");     // the directory is part of a DFS name
      CHECK(itemAt(j,"BOOT")["name"].toString()=="$.BOOT"); CHECK(!a["attributes"].toObject().contains("dfs"));
      CHECK(a["attributes"].toObject()["acorn"].toObject()["access"].toString()=="L");
      CHECK(itemAt(j,"BOOT")["attributes"].toObject()["acorn"].toObject()["exec"].toString()=="FFFF8023");
      CHECK(j["source"].toObject()["filesystem"].toString()=="dfs");
      keep(m,out,"dfs"); delete m; }
    { MainWindow w; w.show(); CHECK(w.openImage(pc)); pump();
      auto* m=mimeFor(w,w.dosTree(),{find(w.dosTree(),"README.TXT"),find(w.dosTree(),"SUB")});
      QStringList n; for(auto&u:m->urls()) n<<QFileInfo(u.toLocalFile()).fileName(); n.sort();
      CHECK(n==QStringList({"README.TXT","SUB"}));                                       // no .inf for DOS files
      QJsonObject j=jsonOf(m); QString base=QUrl(j["base"].toString()).toLocalFile();
      CHECK(slurp(base+"/SUB/X.BIN")==xbin);
      QJsonObject f=itemAt(j,"SUB/X.BIN")["attributes"].toObject()["fat"].toObject();
      CHECK(f["readonly"].toBool() && f["hidden"].toBool() && f["system"].toBool() && !f["archive"].toBool());
      CHECK(itemAt(j,"README.TXT")["modified"].toString().size()==19);                   // DOS stamps every file
      CHECK(j["source"].toObject()["filesystem"].toString()=="fat");
      keep(m,out,"fat"); delete m; }

    // ---- drops: same process (direct, nothing written), "another process" (JSON), back onto itself (refused)
    printf("== drops\n");
    { QString dst=out+"/dst.adl"; CHECK(NewDisc::create(dst,NewDisc::AdfsL,"DST",false,&e));
      QString dst2=out+"/dst2.adl"; CHECK(NewDisc::create(dst2,NewDisc::AdfsL,"DST2",false,&e));
      QString ddsd=out+"/dst.dsd"; CHECK(NewDisc::create(ddsd,NewDisc::DfsDs40,"",false,&e));
      QString dpc=out+"/dst.img"; CHECK(NewDisc::create(dpc,NewDisc::Pc720,"",false,&e));
      MainWindow s; s.show(); CHECK(s.openImage(adl)); pump();
      MainWindow d; d.show(); CHECK(d.openImage(dst)); pump();
      int before=dragDirs();
      auto* m=mimeFor(s,s.tree(),{find(s.tree(),"Elite"),find(s.tree(),"Games")});
      d.tabs()->setCurrentWidget(d.tree()); sendDrop(d.tree(),m);
      CHECK(!m->state()->extracted); CHECK(dragDirs()==before);                           // copied directly
      { Dir r; CHECK(d.image().readDir(RootSector,&r,&e)); CHECK(r.indexOf("Elite")>=0 && r.indexOf("Games")>=0); }
      // back onto its own pane: refused, nothing written
      s.tabs()->setCurrentWidget(s.tree()); sendDrop(s.tree(),m); CHECK(!s.image().dirty()); CHECK(!m->state()->extracted);
      // the same drag arriving from another process: through the JSON, attributes kept
      QMimeData* f=asForeign(m); CHECK(m->state()->extracted);
      MainWindow d2; d2.show(); CHECK(d2.openImage(dst2)); pump(); d2.tabs()->setCurrentWidget(d2.tree()); sendDrop(d2.tree(),f);
      { Dir r; CHECK(d2.image().readDir(RootSector,&r,&e)); int i=r.indexOf("Elite"); CHECK(i>=0);
        if(i>=0){ const Entry& x=r.entries[i]; CHECK(x.load==0xFFFF1900u && x.exec==0xFFFF8023u); CHECK(x.flag(Entry::L)&&x.flag(Entry::W)&&x.flag(Entry::R)&&!x.flag(Entry::E));
          QByteArray b; CHECK(d2.image().readFile(x,&b,&e)); CHECK(b==elite); }
        int g=r.indexOf("Games"); CHECK(g>=0);
        if(g>=0){ Dir gd; CHECK(d2.image().readDir(r.entries[g].sin,&gd,&e)); int k=gd.indexOf("Elite"); CHECK(k>=0);
          if(k>=0){ CHECK(gd.entries[k].flag(Entry::E)); CHECK(gd.entries[k].load==0x3000); } CHECK(gd.indexOf("Deep")>=0); }
        CHECK(r.indexOf("Elite_inf")<0 && r.indexOf("Elite/inf")<0); }                  // the sidecars were not imported
      delete f; delete m;
      // DFS -> DFS and DOS -> DOS from "another process"
      MainWindow sd; sd.show(); CHECK(sd.openImage(dsd)); pump();
      auto* m2=mimeFor(sd,sd.dfsTree(0),{find(sd.dfsTree(0),"A.BOOT")}); QMimeData* f2=asForeign(m2);
      MainWindow dd; dd.show(); CHECK(dd.openImage(ddsd)); pump(); dd.tabs()->setCurrentWidget(dd.dfsTree(1)); pump(); sendDrop(dd.dfsTree(1),f2);
      { int i=dd.dfs(1).indexOf('A',"BOOT"); CHECK(i>=0); if(i>=0){ const Dfs::File& x=dd.dfs(1).files()[i]; CHECK(x.locked); CHECK(x.load==0x1100 && x.exec==0x1200); } }
      delete f2; delete m2;
      MainWindow sp; sp.show(); CHECK(sp.openImage(pc)); pump();
      auto* m3=mimeFor(sp,sp.dosTree(),{find(sp.dosTree(),"SUB")}); QMimeData* f3=asForeign(m3);
      MainWindow dp; dp.show(); CHECK(dp.openImage(dpc)); pump(); dp.tabs()->setCurrentWidget(dp.dosTree()); pump(); sendDrop(dp.dosTree(),f3);
      { QVector<Dos::DEntry> L; CHECK(dp.dos().listDir(0,&L,&e)); quint32 sub=0; for(auto&x:L) if(x.name=="SUB") sub=x.cluster; CHECK(sub);
        QVector<Dos::DEntry> S; CHECK(dp.dos().listDir(sub,&S,&e)); bool ok=false; for(auto&x:S) if(x.name=="X.BIN"){ ok=true; CHECK(x.attr&Dos::AttrR); QByteArray b; CHECK(dp.dos().readFile(x,&b,&e)); CHECK(b==xbin);} CHECK(ok); }
      delete f3; delete m3;

      // DFS -> ADFS: a file in directory A goes into a subdirectory A of wherever it is dropped
      printf("== DFS directory letters -> ADFS subdirectories\n");
      auto adfsDir=[&](Image& im,quint32 dir,const QString& n)->quint32{ Dir r; QString er; if(!im.readDir(dir,&r,&er)) return 0;
          int i=r.indexOf(n); return i>=0 && r.entries[i].isDir() ? r.entries[i].sin : 0; };
      auto adfsFile=[&](Image& im,quint32 dir,const QString& n,Entry* out)->bool{ Dir r; QString er; if(!im.readDir(dir,&r,&er)) return false;
          int i=r.indexOf(n); if(i<0||r.entries[i].isDir()) return false; *out=r.entries[i]; return true; };
      {   // same process, dropped on the Games directory row: $.BOOT -> $.Games.BOOT, A.BOOT -> $.Games.A.BOOT
          auto* m4=mimeFor(sd,sd.dfsTree(0),{find(sd.dfsTree(0),"$.BOOT"),find(sd.dfsTree(0),"A.BOOT")});
          d.tabs()->setCurrentWidget(d.tree()); pump(); sendDropOn(d.tree(),find(d.tree(),"Games"),m4);
          quint32 g=adfsDir(d.image(),RootSector,"Games"); CHECK(g);
          Entry x; CHECK(adfsFile(d.image(),g,"BOOT",&x)); CHECK(x.load==0xFFFF1900u && x.exec==0xFFFF8023u && !x.flag(Entry::L));
          quint32 a=adfsDir(d.image(),g,"A"); CHECK(a);
          CHECK(adfsFile(d.image(),a,"BOOT",&x)); CHECK(x.load==0x1100 && x.exec==0x1200 && x.flag(Entry::L));
          { QByteArray b; CHECK(d.image().readFile(x,&b,&e)); CHECK(b==aboot); }
          CHECK(!adfsDir(d.image(),RootSector,"A"));                                   // nothing at the root
          delete m4; }
      {   // the directory is reused if it is already there (no A~1)
          quint32 pre=0; CHECK(d.image().makeDir(RootSector,"A",&e,&pre)); d.refreshTree(); pump();
          auto* m5=mimeFor(sd,sd.dfsTree(0),{find(sd.dfsTree(0),"A.BOOT")});
          d.tabs()->setCurrentWidget(d.tree()); pump(); sendDrop(d.tree(),m5);
          Entry x; CHECK(adfsFile(d.image(),pre,"BOOT",&x)); CHECK(!adfsDir(d.image(),RootSector,"A~1"));
          delete m5; }
      {   // from another process (the retro-files JSON): the same, at the root
          auto* m6=mimeFor(sd,sd.dfsTree(0),{find(sd.dfsTree(0),"$.BOOT"),find(sd.dfsTree(0),"A.BOOT")}); QMimeData* f6=asForeign(m6);
          d2.tabs()->setCurrentWidget(d2.tree()); pump(); sendDrop(d2.tree(),f6);
          Entry x; CHECK(adfsFile(d2.image(),RootSector,"BOOT",&x));
          quint32 a=adfsDir(d2.image(),RootSector,"A"); CHECK(a); CHECK(adfsFile(d2.image(),a,"BOOT",&x) && x.load==0x1100 && x.flag(Entry::L));
          QString err; CHECK(d2.image().commit(&err,false,nullptr)); if(!err.isEmpty()) printf("  commit: %s\n",qPrintable(err));
          delete f6; delete m6; }

      // ADFS -> DFS: a single-letter directory may keep its letter (asked); other directories flatten into $
      printf("== ADFS single-letter directories -> DFS directory letters\n");
      QString ladl=out+"/letters.adl"; CHECK(NewDisc::create(ladl,NewDisc::AdfsL,"LET",false,&e));
      QByteArray one=rnd(100,11), two=rnd(200,12), three=rnd(300,13), hfile=rnd(50,14), four=rnd(70,15);
      { Image im; CHECK(im.open(ladl,&e,0,detectDisk(ladl).sides)); quint32 f=0,sub=0,g=0,k=0;
        CHECK(im.makeDir(RootSector,"F",&e,&f)); CHECK(im.addFile(f,"ONE",one,0x1900,0x8023,FlagR|FlagW|FlagL,false,&e));
        CHECK(im.makeDir(f,"Sub",&e,&sub)); CHECK(im.addFile(sub,"TWO",two,0,0,FlagR|FlagW,false,&e));
        CHECK(im.makeDir(RootSector,"g",&e,&g)); CHECK(im.addFile(g,"THREE",three,0,0,FlagR|FlagW,false,&e));
        CHECK(im.makeDir(RootSector,"Kit",&e,&k)); CHECK(im.addFile(k,"FOUR",four,0,0,FlagR|FlagW,false,&e));
        CHECK(im.addFile(RootSector,"H",hfile,0,0,FlagR|FlagW,false,&e)); CHECK(im.commit(&e,false,nullptr)); }
      QString ldsd=out+"/letters.dsd"; CHECK(NewDisc::create(ldsd,NewDisc::DfsDs40,"",false,&e));
      MainWindow ls; ls.show(); CHECK(ls.openImage(ladl)); pump();
      MainWindow ld; ld.show(); CHECK(ld.openImage(ldsd)); pump();
      auto dfsHas=[&](int side,QChar dir,const QString& n)->const Dfs::File*{ int i=ld.dfs(side).indexOf(dir,n); return i>=0?&ld.dfs(side).files()[i]:nullptr; };
      {   // Yes for F (nested Sub's file keeps F too), No for g; a single-letter *file* and a longer name aren't asked about
          auto* m7=mimeFor(ls,ls.tree(),{find(ls.tree(),"F"),find(ls.tree(),"g"),find(ls.tree(),"H"),find(ls.tree(),"Kit")});
          answer({QMessageBox::Yes,QMessageBox::No});
          ld.tabs()->setCurrentWidget(ld.dfsTree(0)); pump(); sendDrop(ld.dfsTree(0),m7); pump();
          CHECK(g_answered==2); CHECK(g_promptTexts.size()==2 && g_promptTexts[0].contains("'F'") && g_promptTexts[1].contains("'g'"));
          const Dfs::File* x=dfsHas(0,'F',"ONE"); CHECK(x && x->load==0x1900 && x->exec==0x8023 && x->locked);
          CHECK(dfsHas(0,'F',"TWO")); CHECK(!dfsHas(0,'$',"ONE") && !dfsHas(0,'$',"TWO"));
          CHECK(dfsHas(0,'$',"THREE")); CHECK(!dfsHas(0,'g',"THREE"));
          CHECK(dfsHas(0,'$',"H")); CHECK(dfsHas(0,'$',"FOUR"));
          delete m7; }
      {   // from another process (JSON): "Yes to all" answers once for both directories, keeping the letter's case
          auto* m8=mimeFor(ls,ls.tree(),{find(ls.tree(),"F"),find(ls.tree(),"g")}); QMimeData* f8=asForeign(m8);
          answer({QMessageBox::YesToAll});
          ld.tabs()->setCurrentWidget(ld.dfsTree(1)); pump(); sendDrop(ld.dfsTree(1),f8); pump();
          CHECK(g_answered==1);
          auto has1=[&](QChar dir,const QString& n){ int i=ld.dfs(1).indexOf(dir,n); return i>=0 && ld.dfs(1).files()[i].dir==dir; };
          CHECK(has1('F',"ONE")); CHECK(has1('F',"TWO")); CHECK(has1('g',"THREE"));
          QString err; CHECK(ld.image().commit(&err,false,nullptr)); if(!err.isEmpty()) printf("  commit: %s\n",qPrintable(err));
          delete f8; delete m8; }
      {   // Cancel stops the whole copy: H after it isn't reached (it would raise a "replace?" prompt, caught as unexpected)
          int before0=ld.dfs(0).files().size(); int failsBefore=fails;
          auto* m9=mimeFor(ls,ls.tree(),{find(ls.tree(),"F"),find(ls.tree(),"H")});
          answer({QMessageBox::Cancel});
          ld.tabs()->setCurrentWidget(ld.dfsTree(0)); pump(); sendDrop(ld.dfsTree(0),m9); pump(); pump();
          CHECK(g_answered==1); CHECK(ld.dfs(0).files().size()==before0); CHECK(fails==failsBefore);
          delete m9; }
    }

    // ---- a list from a tool this program doesn't know (CP/M): plain files, readonly honoured
    printf("== foreign tool\n");
    { QString base=out+"/cpmdrop"; QDir(base).removeRecursively(); QDir().mkpath(base);
      QByteArray pip=rnd(2048,10); { QFile f(base+"/PIP.COM"); f.open(QIODevice::WriteOnly); f.write(pip); }
      { QFile f(base+"/junk.txt"); f.open(QIODevice::WriteOnly); f.write("not listed"); }
      QJsonObject it{{"path","PIP.COM"},{"type","file"},{"name","PIP.COM"},{"size",2048},{"attributes",QJsonObject{{"cpm",QJsonObject{{"user",0},{"readonly",true}}}}}};
      QJsonObject doc{{"format","retro-files"},{"version",1},{"generator","CP/M tool"},{"source",QJsonObject{{"filesystem","cpm"}}},
                      {"base",QUrl::fromLocalFile(base).toString()},{"items",QJsonArray{it}}};
      QMimeData mm; mm.setData(RetroFiles::MimeType,QJsonDocument(doc).toJson()); mm.setUrls({QUrl::fromLocalFile(base+"/PIP.COM"),QUrl::fromLocalFile(base+"/junk.txt")});
      QString dpc2=out+"/dst2.img"; CHECK(NewDisc::create(dpc2,NewDisc::Pc720,"",false,&e));
      MainWindow dp; dp.show(); CHECK(dp.openImage(dpc2)); pump(); dp.tabs()->setCurrentWidget(dp.dosTree()); pump(); sendDrop(dp.dosTree(),&mm);
      QVector<Dos::DEntry> L; CHECK(dp.dos().listDir(0,&L,&e)); CHECK(L.size()==1);         // only the listed item
      if(L.size()==1){ CHECK(L[0].name=="PIP.COM"); CHECK(L[0].attr&Dos::AttrR); }
      // unsafe or broken lists are refused
      QVector<XItem> x; QString err;
      for(const QString& bad:{QString("../outside"),QString("/etc/passwd"),QString("a/../../b"),QString("")}){
          QJsonObject b=doc; QJsonObject bi=it; bi["path"]=bad; b["items"]=QJsonArray{bi};
          CHECK(!RetroFiles::readJson(QJsonDocument(b).toJson(),&x,&err)); }
      { QJsonObject b=doc; b["version"]=2; CHECK(!RetroFiles::readJson(QJsonDocument(b).toJson(),&x,&err)); printf("  version 2: %s\n",qPrintable(err)); }
      { QJsonObject b=doc; QJsonObject bi=it; bi["size"]=99; b["items"]=QJsonArray{bi}; CHECK(!RetroFiles::readJson(QJsonDocument(b).toJson(),&x,&err)); }
      CHECK(!RetroFiles::readJson("not json",&x,&err));
    }

    // ---- read-only image (a damaged free space map opens read-only): dragging out still works
    { QString ro=out+"/ro.adl"; QFile::remove(ro); QFile::copy(adl,ro);
      { QFile f(ro); f.open(QIODevice::ReadWrite); f.seek(0xFF); char c; f.getChar(&c); f.seek(0xFF); f.putChar(char(c^0x55)); }
      MainWindow w; w.show(); QTimer::singleShot(200,[]{ if(QWidget* d=QApplication::activeModalWidget()) d->close(); });   // the "opened read-only" report
      CHECK(w.openImage(ro)); pump(); CHECK(!w.image().editable());
      auto* m=mimeFor(w,w.tree(),{find(w.tree(),"Elite")}); CHECK(m); if(m){ CHECK(!m->urls().isEmpty()); CHECK(m->state()->errors.isEmpty());
      QJsonObject j=jsonOf(m); CHECK(slurp(QUrl(j["base"].toString()).toLocalFile()+"/Elite")==elite); delete m; } }

    // ---- stale folders from a crash are removed, current ones kept
    { QString old=RetroFiles::dragDirPrefix()+"testold"; QDir().mkpath(old);
      struct utimbuf t; t.actime=t.modtime=time(nullptr)-3*24*3600; utime(old.toLocal8Bit().constData(),&t);
      QString fresh=RetroFiles::dragDirPrefix()+"testnew"; QDir().mkpath(fresh);
      CHECK(RetroFiles::cleanStale()>=1); CHECK(!QFileInfo::exists(old)); CHECK(QFileInfo::exists(fresh)); QDir(fresh).removeRecursively(); }

    QSettings().clear();
    printf(fails ? "FAILED (%d failures)\n" : "ALL OK (%d failures)\n", fails);
    return fails ? 1 : 0;
}
