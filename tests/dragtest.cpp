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
// Step 5l: the window and acorndw put place host files whose .inf names a DFS letter in the same directories.
#include "mainwindow.h"
#include "windowhelp.h"
#include "dragout.h"
#include "newdisc.h"
#include "cli/backend.h"
#include <diskwrangler/cli/cli.h>
#include <diskwrangler/xfer/mime.h>
#include <diskwrangler/xfer/retrofiles.h>
#include <QApplication>
#include <QCryptographicHash>
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
#include <QTimeZone>
#include <cstdio>
#include <functional>
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
// Clicks the next QMessageBoxes' buttons by their text (for boxes with buttons of their own).
static QStringList g_clicks; static QStringList g_clickTexts;
static void clickText(QStringList seq){
    g_clicks=seq; g_clickTexts.clear();
    auto* t=new QTimer; t->setInterval(50);
    QObject::connect(t,&QTimer::timeout,[t]{
        if(g_clicks.isEmpty()){ t->deleteLater(); return; }
        auto* mb=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(!mb) return;
        QString want=g_clicks.takeFirst(); g_clickTexts<<mb->text();
        for(QAbstractButton* b:mb->buttons()) if(b->text()==want){ b->click(); return; }
        printf("  !! no button '%s'\n",qPrintable(want)); mb->close(); });
    t->start();
}
static void dropPaths(QTreeWidget* tree,const QStringList& paths){
    QMimeData m; QList<QUrl> u; for(auto&p:paths) u<<QUrl::fromLocalFile(p); m.setUrls(u); sendDrop(tree,&m);
    for(int i=0;i<10;++i) pump();
}
static QList<MainWindow*> newWindows(const QList<MainWindow*>& before){
    QList<MainWindow*> n; for(MainWindow* w:wt::windows()) if(!before.contains(w)) n<<w; return n;
}
static void closeAll(const QList<MainWindow*>& l){ for(MainWindow* w:l) w->close(); pump(); }
static int dragDirs(){ QFileInfo p(RetroFiles::dragDirPrefix()); return QDir(p.absolutePath()).entryList({p.fileName()+"*"},QDir::Dirs|QDir::NoDotAndDotDot).size(); }
static dw::DragOutMime* mimeFor(MainWindow& w, DropTree* t, QList<QTreeWidgetItem*> items){
    w.tabWidget()->setCurrentWidget(t); pump();
    return qobject_cast<dw::DragOutMime*>(t->makeMime(items));
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
    for(const QString& fmt:{QString(DW_MIME_ITEMS),QString(RetroFiles::MimeType)}) f->setData(fmt,m->data(fmt));
    QList<QByteArray> tag=m->data(DW_MIME_SOURCE).split(':'); tag[0]=QByteArray::number(tag[0].toLongLong()+1);
    f->setData(DW_MIME_SOURCE,tag.join(':'));
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
      quint64 cs=quint64(QDateTime(QDate(1900,1,1),QTime(0,0),QTimeZone::utc()).secsTo(QDateTime(when.date(),when.time(),QTimeZone::utc())))*100;
      CHECK(im.addFile(RootSector,"Text",stamped,0xFFFFFF00u|quint32(cs>>32),quint32(cs),FlagW|FlagR,false,&e));
      quint32 games=0,deep=0; CHECK(im.makeDir(RootSector,"Games",&e,&games));
      CHECK(im.addFile(games,"Elite",elite2,0x3000,0x3000,FlagR|FlagE,false,&e));
      CHECK(im.makeDir(games,"Deep",&e,&deep)); CHECK(im.addFile(deep,"Tiny",tiny,0,0,FlagR,false,&e));
      CHECK(im.commit(&e,false,nullptr)); }
    QString dsd=out+"/src.dsd"; CHECK(NewDisc::create(dsd,NewDisc::DfsDs40,"",false,&e));
    QByteArray boot=rnd(300,6), aboot=rnd(1234,7);
    { DiskInfo i=detectDisk(dsd); Image im; CHECK(im.openRaw(dsd,i.fullSize,&e,i.sides)); dw::dfs::Disc d; CHECK(d.attach(&im,0,i.dfs,&e));
      CHECK(d.addFile('$',"BOOT",boot,0x31900,0x38023,false,false,&e)); CHECK(d.addFile('A',"BOOT",aboot,0x1100,0x1200,true,false,&e)); CHECK(im.commit(&e,false,nullptr)); }
    QString pc=out+"/src.img"; CHECK(NewDisc::create(pc,NewDisc::Pc720,"",false,&e));
    QByteArray readme=rnd(4000,8), xbin=rnd(9000,9);
    { Image im; CHECK(im.openRaw(pc,737280,&e)); dw::fat::Volume v; CHECK(v.attach(&im,0,737280,&e));
      CHECK(v.addFile(0,"README.TXT",readme,false,&e)); quint32 sub=0; CHECK(v.makeDir(0,"SUB",&e,&sub)); CHECK(v.addFile(sub,"X.BIN",xbin,false,&e));
      CHECK(v.modify(sub,"X.BIN","X.BIN",dw::fat::AttrR|dw::fat::AttrH|dw::fat::AttrS,&e)); CHECK(im.commit(&e,false,nullptr)); }

    // ---- ADFS: lazy, files, names, .inf, dates, JSON
    printf("== ADFS drag out\n");
    { MainWindow w; w.show(); CHECK(w.openFile(adl)); pump();
      DropTree* t=wt::adfsTree(w); auto* games=find(t,"Games"); auto* gElite=games?findIn(games,"Elite"):nullptr;
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
      CHECK(!itemAt(j,"Games")["attributes"].toObject()["acorn"].toObject()["access"].toString().contains('D'));   // Stardot: no D
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
      { MainWindow w2; w2.show(); CHECK(w2.openFile(adl)); pump(); auto* m2=mimeFor(w2,wt::adfsTree(w2),{find(wt::adfsTree(w2),"Elite")});
        QStringList n2; for(auto&u:m2->urls()) n2<<QFileInfo(u.toLocalFile()).fileName(); CHECK(n2==QStringList({"Elite"})); delete m2; }
      QSettings().setValue("writeInf",true);
    }

    // ---- DFS and DOS drag out
    printf("== DFS and DOS drag out\n");
    { MainWindow w; w.show(); CHECK(w.openFile(dsd)); pump();
      auto* m=mimeFor(w,wt::dfsTree(w),{find(wt::dfsTree(w),"$.BOOT"),find(wt::dfsTree(w),"A.BOOT")});
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
    { MainWindow w; w.show(); CHECK(w.openFile(pc)); pump();
      auto* m=mimeFor(w,wt::dosTree(w),{find(wt::dosTree(w),"README.TXT"),find(wt::dosTree(w),"SUB")});
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
      MainWindow s; s.show(); CHECK(s.openFile(adl)); pump();
      MainWindow d; d.show(); CHECK(d.openFile(dst)); pump();
      int before=dragDirs();
      auto* m=mimeFor(s,wt::adfsTree(s),{find(wt::adfsTree(s),"Elite"),find(wt::adfsTree(s),"Games")});
      d.tabWidget()->setCurrentWidget(wt::adfsTree(d)); sendDrop(wt::adfsTree(d),m);
      CHECK(!m->state()->extracted); CHECK(dragDirs()==before);                           // copied directly
      { Dir r; CHECK(d.document()->image().readDir(RootSector,&r,&e)); CHECK(r.indexOf("Elite")>=0 && r.indexOf("Games")>=0); }
      // back onto its own pane: refused, nothing written
      s.tabWidget()->setCurrentWidget(wt::adfsTree(s)); sendDrop(wt::adfsTree(s),m); CHECK(!s.document()->image().dirty()); CHECK(!m->state()->extracted);
      // the same drag arriving from another process: through the JSON, attributes kept
      QMimeData* f=asForeign(m); CHECK(m->state()->extracted);
      MainWindow d2; d2.show(); CHECK(d2.openFile(dst2)); pump(); d2.tabWidget()->setCurrentWidget(wt::adfsTree(d2)); sendDrop(wt::adfsTree(d2),f);
      { Dir r; CHECK(d2.document()->image().readDir(RootSector,&r,&e)); int i=r.indexOf("Elite"); CHECK(i>=0);
        if(i>=0){ const Entry& x=r.entries[i]; CHECK(x.load==0xFFFF1900u && x.exec==0xFFFF8023u); CHECK(x.flag(Entry::L)&&x.flag(Entry::W)&&x.flag(Entry::R)&&!x.flag(Entry::E));
          QByteArray b; CHECK(d2.document()->image().readFile(x,&b,&e)); CHECK(b==elite); }
        int g=r.indexOf("Games"); CHECK(g>=0);
        if(g>=0){ Dir gd; CHECK(d2.document()->image().readDir(r.entries[g].sin,&gd,&e)); int k=gd.indexOf("Elite"); CHECK(k>=0);
          if(k>=0){ CHECK(gd.entries[k].flag(Entry::E)); CHECK(gd.entries[k].load==0x3000); } CHECK(gd.indexOf("Deep")>=0); }
        CHECK(r.indexOf("Elite_inf")<0 && r.indexOf("Elite/inf")<0); }                  // the sidecars were not imported
      delete f; delete m;
      // DFS -> DFS and DOS -> DOS from "another process"
      MainWindow sd; sd.show(); CHECK(sd.openFile(dsd)); pump();
      auto* m2=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"A.BOOT")}); QMimeData* f2=asForeign(m2);
      MainWindow dd; dd.show(); CHECK(dd.openFile(ddsd)); pump(); dd.tabWidget()->setCurrentWidget(wt::dfsTree(dd)); pump(); sendDropOn(wt::dfsTree(dd),dd.dfsRoot(1),f2);
      { int i=dd.document()->dfs(1).indexOf('A',"BOOT"); CHECK(i>=0); if(i>=0){ const dw::dfs::File& x=dd.document()->dfs(1).files()[i]; CHECK(x.locked); CHECK(x.load==0x1100 && x.exec==0x1200); } }
      delete f2; delete m2;
      MainWindow sp; sp.show(); CHECK(sp.openFile(pc)); pump();
      auto* m3=mimeFor(sp,wt::dosTree(sp),{find(wt::dosTree(sp),"SUB")}); QMimeData* f3=asForeign(m3);
      MainWindow dp; dp.show(); CHECK(dp.openFile(dpc)); pump(); dp.tabWidget()->setCurrentWidget(wt::dosTree(dp)); pump(); sendDrop(wt::dosTree(dp),f3);
      { QVector<dw::fat::DEntry> L; CHECK(dp.document()->dos().listDir(0,&L,&e)); quint32 sub=0; for(auto&x:L) if(x.name=="SUB") sub=x.cluster; CHECK(sub);
        QVector<dw::fat::DEntry> S; CHECK(dp.document()->dos().listDir(sub,&S,&e)); bool ok=false; for(auto&x:S) if(x.name=="X.BIN"){ ok=true; CHECK(x.attr&dw::fat::AttrR); QByteArray b; CHECK(dp.document()->dos().readFile(x,&b,&e)); CHECK(b==xbin);} CHECK(ok); }
      delete f3; delete m3;

      // DFS -> ADFS: a file in directory A goes into a subdirectory A of wherever it is dropped
      printf("== DFS directory letters -> ADFS subdirectories\n");
      auto adfsDir=[&](Image& im,quint32 dir,const QString& n)->quint32{ Dir r; QString er; if(!im.readDir(dir,&r,&er)) return 0;
          int i=r.indexOf(n); return i>=0 && r.entries[i].isDir() ? r.entries[i].sin : 0; };
      auto adfsFile=[&](Image& im,quint32 dir,const QString& n,Entry* out)->bool{ Dir r; QString er; if(!im.readDir(dir,&r,&er)) return false;
          int i=r.indexOf(n); if(i<0||r.entries[i].isDir()) return false; *out=r.entries[i]; return true; };
      {   // same process, dropped on the Games directory row: $.BOOT -> $.Games.BOOT, A.BOOT -> $.Games.A.BOOT
          auto* m4=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"$.BOOT"),find(wt::dfsTree(sd),"A.BOOT")});
          d.tabWidget()->setCurrentWidget(wt::adfsTree(d)); pump(); sendDropOn(wt::adfsTree(d),find(wt::adfsTree(d),"Games"),m4);
          quint32 g=adfsDir(d.document()->image(),RootSector,"Games"); CHECK(g);
          Entry x; CHECK(adfsFile(d.document()->image(),g,"BOOT",&x)); CHECK(x.load==0xFFFF1900u && x.exec==0xFFFF8023u && !x.flag(Entry::L));
          quint32 a=adfsDir(d.document()->image(),g,"A"); CHECK(a);
          CHECK(adfsFile(d.document()->image(),a,"BOOT",&x)); CHECK(x.load==0x1100 && x.exec==0x1200 && x.flag(Entry::L));
          { QByteArray b; CHECK(d.document()->image().readFile(x,&b,&e)); CHECK(b==aboot); }
          CHECK(!adfsDir(d.document()->image(),RootSector,"A"));                                   // nothing at the root
          delete m4; }
      {   // the directory is reused if it is already there (no A~1)
          quint32 pre=0; CHECK(d.document()->image().makeDir(RootSector,"A",&e,&pre)); d.refresh(); pump();
          auto* m5=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"A.BOOT")});
          d.tabWidget()->setCurrentWidget(wt::adfsTree(d)); pump(); sendDrop(wt::adfsTree(d),m5);
          Entry x; CHECK(adfsFile(d.document()->image(),pre,"BOOT",&x)); CHECK(!adfsDir(d.document()->image(),RootSector,"A~1"));
          delete m5; }
      {   // a letter ADFS can't have as a name (^): $.X_HAT in the drop directory, not a directory "_", with a warning
          CHECK(sd.document()->dfs(0).addFile('^',"HAT",QByteArray("hat"),0x1900,0x1900,false,false,&e)); sd.refresh(); pump();
          auto* m7=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"^.HAT")});
          answer({QMessageBox::Ok});                              // the import summary lists the warning
          d.tabWidget()->setCurrentWidget(wt::adfsTree(d)); pump(); sendDrop(wt::adfsTree(d),m7); for(int i=0;i<10;++i) pump();
          Entry x; CHECK(adfsFile(d.document()->image(),RootSector,"X_HAT",&x)); CHECK(!adfsDir(d.document()->image(),RootSector,"_"));
          CHECK(g_answered==1);
          { QByteArray b; CHECK(d.document()->image().readFile(x,&b,&e)); CHECK(b=="hat"); }
          CHECK(d.document()->image().deleteEntry(RootSector,"X_HAT",&e)); d.refresh();
          CHECK(sd.document()->dfs(0).remove('^',"HAT",&e)); sd.refresh(); pump();
          delete m7; }
      {   // from another process (the retro-files JSON): the same, at the root
          auto* m6=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"$.BOOT"),find(wt::dfsTree(sd),"A.BOOT")}); QMimeData* f6=asForeign(m6);
          d2.tabWidget()->setCurrentWidget(wt::adfsTree(d2)); pump(); sendDrop(wt::adfsTree(d2),f6);
          Entry x; CHECK(adfsFile(d2.document()->image(),RootSector,"BOOT",&x));
          quint32 a=adfsDir(d2.document()->image(),RootSector,"A"); CHECK(a); CHECK(adfsFile(d2.document()->image(),a,"BOOT",&x) && x.load==0x1100 && x.flag(Entry::L));
          QString err; CHECK(d2.document()->image().commit(&err,false,nullptr)); if(!err.isEmpty()) printf("  commit: %s\n",qPrintable(err));
          delete f6; delete m6; }

      // DFS -> host -> ADFS: the .inf carries the letter, so the files land as a direct copy puts them
      printf("== DFS -> host (.inf) -> ADFS, letter case, flatten setting\n");
      {   QString h3=out+"/host3.adl"; CHECK(NewDisc::create(h3,NewDisc::AdfsL,"H3",false,&e));
          MainWindow d3; d3.show(); CHECK(d3.openFile(h3)); pump();
          auto* mh=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"$.BOOT"),find(wt::dfsTree(sd),"A.BOOT")});
          QStringList hp; for(auto&u:mh->urls()) hp<<u.toLocalFile();             // BOOT, BOOT.inf, A.BOOT, A.BOOT.inf
          CHECK(hp.size()==4);
          d3.tabWidget()->setCurrentWidget(wt::adfsTree(d3)); pump(); dropPaths(wt::adfsTree(d3),hp);
          Entry x; CHECK(adfsFile(d3.document()->image(),RootSector,"BOOT",&x)); CHECK(x.load==0xFFFF1900u && x.exec==0xFFFF8023u);
          quint32 a=adfsDir(d3.document()->image(),RootSector,"A"); CHECK(a);
          CHECK(adfsFile(d3.document()->image(),a,"BOOT",&x) && x.load==0x1100 && x.exec==0x1200 && x.flag(Entry::L));
          { QByteArray b; CHECK(d3.document()->image().readFile(x,&b,&e)); CHECK(b==aboot); }
          CHECK(!adfsFile(d3.document()->image(),RootSector,"A_BOOT",&x));                       // not flattened into the name either
          delete mh;
          // a lower-case letter keeps its case, directly and through the host
          CHECK(sd.document()->dfs(0).addFile('b',"LOW",QByteArray("low"),0x1900,0x1900,false,false,&e)); sd.refresh(); pump();
          auto* ml=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"b.LOW")});
          d3.tabWidget()->setCurrentWidget(wt::adfsTree(d3)); pump(); sendDrop(wt::adfsTree(d3),ml);
          { Dir r; QString er; CHECK(d3.document()->image().readDir(RootSector,&r,&er)); int i=r.indexOf("b");
            CHECK(i>=0 && r.entries[i].isDir() && r.entries[i].name()=="b"); }
          quint32 lb=adfsDir(d3.document()->image(),RootSector,"b"); CHECK(lb && adfsFile(d3.document()->image(),lb,"LOW",&x));
          QString h4=out+"/host4.adl"; CHECK(NewDisc::create(h4,NewDisc::AdfsL,"H4",false,&e));
          { MainWindow d4; d4.show(); CHECK(d4.openFile(h4)); pump();
            QStringList lp; for(auto&u:ml->urls()) lp<<u.toLocalFile();
            d4.tabWidget()->setCurrentWidget(wt::adfsTree(d4)); pump(); dropPaths(wt::adfsTree(d4),lp);
            Dir r; QString er; CHECK(d4.document()->image().readDir(RootSector,&r,&er)); int i=r.indexOf("b");
            CHECK(i>=0 && r.entries[i].isDir() && r.entries[i].name()=="b");
            quint32 l4=adfsDir(d4.document()->image(),RootSector,"b"); CHECK(l4 && adfsFile(d4.document()->image(),l4,"LOW",&x)); }
          delete ml;
          // Tools > "DFS directories become ADFS directories" off: flattened, from DFS and from the host
          QSettings().setValue("dfsLetterDirs",false);
          QString h5=out+"/host5.adl"; CHECK(NewDisc::create(h5,NewDisc::AdfsL,"H5",false,&e));
          { MainWindow d5; d5.show(); CHECK(d5.openFile(h5)); pump();
            auto* mf=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"A.BOOT")});
            QStringList fp; for(auto&u:mf->urls()) fp<<u.toLocalFile();
            d5.tabWidget()->setCurrentWidget(wt::adfsTree(d5)); pump(); dropPaths(wt::adfsTree(d5),fp);
            auto* mg=mimeFor(sd,wt::dfsTree(sd),{find(wt::dfsTree(sd),"b.LOW")});
            d5.tabWidget()->setCurrentWidget(wt::adfsTree(d5)); pump(); sendDrop(wt::adfsTree(d5),mg);
            CHECK(adfsFile(d5.document()->image(),RootSector,"BOOT",&x) && x.load==0x1100 && x.flag(Entry::L));
            CHECK(adfsFile(d5.document()->image(),RootSector,"LOW",&x));
            CHECK(!adfsDir(d5.document()->image(),RootSector,"A") && !adfsDir(d5.document()->image(),RootSector,"b"));
            delete mg; delete mf; }
          QSettings().setValue("dfsLetterDirs",true);
          CHECK(sd.document()->dfs(0).remove('b',"LOW",&e)); sd.refresh(); pump(); }

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
      MainWindow ls; ls.show(); CHECK(ls.openFile(ladl)); pump();
      MainWindow ld; ld.show(); CHECK(ld.openFile(ldsd)); pump();
      auto dfsHas=[&](int side,QChar dir,const QString& n)->const dw::dfs::File*{ int i=ld.document()->dfs(side).indexOf(dir,n); return i>=0?&ld.document()->dfs(side).files()[i]:nullptr; };
      {   // Yes for F (nested Sub's file keeps F too), No for g; a single-letter *file* and a longer name aren't asked about
          auto* m7=mimeFor(ls,wt::adfsTree(ls),{find(wt::adfsTree(ls),"F"),find(wt::adfsTree(ls),"g"),find(wt::adfsTree(ls),"H"),find(wt::adfsTree(ls),"Kit")});
          answer({QMessageBox::Yes,QMessageBox::No});
          ld.tabWidget()->setCurrentWidget(wt::dfsTree(ld)); pump(); sendDrop(wt::dfsTree(ld),m7); pump();
          CHECK(g_answered==2); CHECK(g_promptTexts.size()==2 && g_promptTexts[0].contains("'F'") && g_promptTexts[1].contains("'g'"));
          const dw::dfs::File* x=dfsHas(0,'F',"ONE"); CHECK(x && x->load==0x1900 && x->exec==0x8023 && x->locked);
          CHECK(dfsHas(0,'F',"TWO")); CHECK(!dfsHas(0,'$',"ONE") && !dfsHas(0,'$',"TWO"));
          CHECK(dfsHas(0,'$',"THREE")); CHECK(!dfsHas(0,'g',"THREE"));
          CHECK(dfsHas(0,'$',"H")); CHECK(dfsHas(0,'$',"FOUR"));
          delete m7; }
      {   // from another process (JSON): "Yes to all" answers once for both directories, keeping the letter's case
          auto* m8=mimeFor(ls,wt::adfsTree(ls),{find(wt::adfsTree(ls),"F"),find(wt::adfsTree(ls),"g")}); QMimeData* f8=asForeign(m8);
          answer({QMessageBox::YesToAll});
          ld.tabWidget()->setCurrentWidget(wt::dfsTree(ld)); pump(); sendDropOn(wt::dfsTree(ld),ld.dfsRoot(1),f8); pump();
          CHECK(g_answered==1);
          auto has1=[&](QChar dir,const QString& n){ int i=ld.document()->dfs(1).indexOf(dir,n); return i>=0 && ld.document()->dfs(1).files()[i].dir==dir; };
          CHECK(has1('F',"ONE")); CHECK(has1('F',"TWO")); CHECK(has1('g',"THREE"));
          QString err; CHECK(ld.document()->image().commit(&err,false,nullptr)); if(!err.isEmpty()) printf("  commit: %s\n",qPrintable(err));
          delete f8; delete m8; }
      {   // Cancel stops the whole copy: H after it isn't reached (it would raise a "replace?" prompt, caught as unexpected)
          int before0=ld.document()->dfs(0).files().size(); int failsBefore=fails;
          auto* m9=mimeFor(ls,wt::adfsTree(ls),{find(wt::adfsTree(ls),"F"),find(wt::adfsTree(ls),"H")});
          answer({QMessageBox::Cancel});
          ld.tabWidget()->setCurrentWidget(wt::dfsTree(ld)); pump(); sendDrop(wt::dfsTree(ld),m9); pump(); pump();
          CHECK(g_answered==1); CHECK(ld.document()->dfs(0).files().size()==before0); CHECK(fails==failsBefore);
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
      MainWindow dp; dp.show(); CHECK(dp.openFile(dpc2)); pump(); dp.tabWidget()->setCurrentWidget(wt::dosTree(dp)); pump(); sendDrop(wt::dosTree(dp),&mm);
      QVector<dw::fat::DEntry> L; CHECK(dp.document()->dos().listDir(0,&L,&e)); CHECK(L.size()==1);         // only the listed item
      if(L.size()==1){ CHECK(L[0].name=="PIP.COM"); CHECK(L[0].attr&dw::fat::AttrR); }
      // unsafe or broken lists are refused
      QVector<dw::TransferItem> x; QString err;
      for(const QString& bad:{QString("../outside"),QString("/etc/passwd"),QString("a/../../b"),QString("")}){
          QJsonObject b=doc; QJsonObject bi=it; bi["path"]=bad; b["items"]=QJsonArray{bi};
          CHECK(!dw::xfer::readJson(QJsonDocument(b).toJson(),&x,&err)); }
      { QJsonObject b=doc; b["version"]=2; CHECK(!dw::xfer::readJson(QJsonDocument(b).toJson(),&x,&err)); printf("  version 2: %s\n",qPrintable(err)); }
      { QJsonObject b=doc; QJsonObject bi=it; bi["size"]=99; b["items"]=QJsonArray{bi}; CHECK(!dw::xfer::readJson(QJsonDocument(b).toJson(),&x,&err)); }
      CHECK(!dw::xfer::readJson("not json",&x,&err));
      // Stardot access letters: upper case = owner, lower case = public (which this program has nowhere to keep). Read as
      // the window reads a drop (the old test read the list into this program's own item): dropped on an ADFS disc the
      // file gets R and W, and no L (the public "l" is not the owner's lock).
      { QJsonObject b=doc; QJsonObject bi=it; b["source"]=QJsonObject{{"filesystem","adfs"}}; bi["name"]="Pip";
        bi["attributes"]=QJsonObject{{"acorn",QJsonObject{{"load","00001900"},{"exec","00001900"},{"access","WRrl"}}}}; b["items"]=QJsonArray{bi};
        QMimeData ma; ma.setData(RetroFiles::MimeType,QJsonDocument(b).toJson()); ma.setUrls({QUrl::fromLocalFile(base+"/PIP.COM")});
        QString adl2=out+"/access.adl"; CHECK(NewDisc::create(adl2,NewDisc::AdfsL,"ACC",false,&e));
        MainWindow wa; wa.show(); CHECK(wa.openFile(adl2)); pump(); sendDrop(wt::adfsTree(wa),&ma);
        Dir r; QString er; CHECK(wa.document()->image().readDir(RootSector,&r,&er)); int i=r.indexOf("Pip");
        CHECK(i>=0 && r.entries[i].flag(Entry::R) && r.entries[i].flag(Entry::W) && !r.entries[i].flag(Entry::L)); }
      // DFS names are D.NAME, bytes carried one character per byte (a BBC "£" is byte &60, i.e. "`"): dropped on a DFS
      // disc the directory letter and the name come out as the list gives them
      { QJsonObject b=doc; b["source"]=QJsonObject{{"filesystem","dfs"}}; QJsonArray arr;
        for(const char* n:{"$.`5","q.PROG","PLAIN"}){ QJsonObject bi=it; bi["name"]=QString::fromLatin1(n);
            bi["attributes"]=QJsonObject{{"acorn",QJsonObject{{"load","00001900"},{"exec","00001900"},{"access","WR"}}}}; arr.append(bi); }
        b["items"]=arr;
        QMimeData md; md.setData(RetroFiles::MimeType,QJsonDocument(b).toJson()); md.setUrls({QUrl::fromLocalFile(base+"/PIP.COM")});
        QString ssd2=out+"/letters.ssd"; CHECK(NewDisc::create(ssd2,NewDisc::DfsSs40,"",false,&e));
        MainWindow wd; wd.show(); CHECK(wd.openFile(ssd2)); pump(); wd.tabWidget()->setCurrentWidget(wt::dfsTree(wd)); pump();
        sendDrop(wt::dfsTree(wd),&md);
        const auto& fl=wd.document()->dfs(0).files(); CHECK(fl.size()==3);
        auto has=[&](QChar d,const QString& n){ for(const auto& f:fl) if(f.dir==d && f.name==n) return true; return false; };
        CHECK(has('$',"`5")); CHECK(has('q',"PROG")); CHECK(has('$',"PLAIN")); }
    }

    // ---- disc images dropped from the host: opened (an empty window here, others in new windows); a hard disc asks
    printf("== disc images dropped\n");
    { QString hd=out+"/hd.dat"; { QFile f(hd); f.open(QIODevice::WriteOnly|QIODevice::Truncate); f.write(Adfs::makeBlankImage(8192,"HARDDISC")); }
      QString plain=out+"/notes.txt"; { QFile f(plain); f.open(QIODevice::WriteOnly|QIODevice::Truncate); f.write("just a text file\n"); }
      QString fl=out+"/floppy.adl"; CHECK(NewDisc::create(fl,NewDisc::AdfsL,"FLOP",false,&e));
      CHECK(detectDisk(hd).kind==DiskInfo::Adfs); CHECK(detectDisk(plain).kind==DiskInfo::Unknown);
      {   // an empty window: the first file opens here, the next in a new window
          MainWindow w; w.show(); pump(); QList<MainWindow*> before=wt::windows();
          dropPaths(wt::adfsTree(w),{dsd,adl});
          CHECK(w.document()->image().isOpen() && w.document()->info().kind==DiskInfo::Dfs);
          QList<MainWindow*> n=newWindows(before); CHECK(n.size()==1 && n[0]->document()->info().kind==DiskInfo::Adfs);
          closeAll(n); }
      {   // a floppy: the image opens in a new window without asking, the plain file is added
          MainWindow w; w.show(); CHECK(w.openFile(fl)); pump(); QList<MainWindow*> before=wt::windows();
          dropPaths(wt::adfsTree(w),{dsd,plain});
          QList<MainWindow*> n=newWindows(before); CHECK(n.size()==1 && n[0]->document()->info().kind==DiskInfo::Dfs);
          Dir r; CHECK(w.document()->image().readDir(RootSector,&r,&e)); CHECK(r.entries.size()==1);   // only the text file
          closeAll(n); }
      {   // a hard disc asks: Open
          MainWindow w; w.show(); CHECK(w.openFile(hd)); pump(); CHECK(w.isHardDisc());
          QList<MainWindow*> before=wt::windows();
          clickText({"Open it"}); dropPaths(wt::adfsTree(w),{dsd});
          CHECK(g_clickTexts.size()==1 && g_clickTexts[0].contains("disc image"));
          QList<MainWindow*> n=newWindows(before); CHECK(n.size()==1 && n[0]->document()->info().kind==DiskInfo::Dfs);
          Dir r; CHECK(w.document()->image().readDir(RootSector,&r,&e)); CHECK(r.entries.isEmpty());
          closeAll(n);
          // Copy onto this disc: the image becomes a file on the hard disc, the plain file goes with it
          clickText({"Copy onto this disc"}); dropPaths(wt::adfsTree(w),{dsd,plain});
          CHECK(g_answered==1); CHECK(newWindows(before).isEmpty());
          CHECK(w.document()->image().readDir(RootSector,&r,&e)); CHECK(r.entries.size()==2);
          bool big=false; for(const Entry& x:r.entries) if(x.length==quint32(QFileInfo(dsd).size())) big=true; CHECK(big);
          // Cancel: nothing happens, nothing more is added
          clickText({"Cancel"}); dropPaths(wt::adfsTree(w),{adl,plain});
          CHECK(g_answered==1); CHECK(newWindows(before).isEmpty());
          CHECK(w.document()->image().readDir(RootSector,&r,&e)); CHECK(r.entries.size()==2);
          // two images: one question for both
          clickText({"Open them"}); dropPaths(wt::adfsTree(w),{dsd,adl});
          CHECK(g_clickTexts.size()==1 && g_clickTexts[0].startsWith("2 of the dropped files"));
          CHECK(newWindows(before).size()==2); closeAll(newWindows(before));
          // a plain file only: no question, added as before
          QString plain2=out+"/more.txt"; { QFile f(plain2); f.open(QIODevice::WriteOnly|QIODevice::Truncate); f.write("more\n"); }
          int fb=fails; dropPaths(wt::adfsTree(w),{plain2}); CHECK(fails==fb);
          CHECK(w.document()->image().readDir(RootSector,&r,&e)); CHECK(r.entries.size()==3); }
    }

    // ---- read-only image (a damaged free space map opens read-only): dragging out still works
    { QString ro=out+"/ro.adl"; QFile::remove(ro); QFile::copy(adl,ro);
      { QFile f(ro); f.open(QIODevice::ReadWrite); f.seek(0xFF); char c; f.getChar(&c); f.seek(0xFF); f.putChar(char(c^0x55)); }
      MainWindow w; w.show(); QTimer::singleShot(200,[]{ if(QWidget* d=QApplication::activeModalWidget()) d->close(); });   // the "opened read-only" report
      CHECK(w.openFile(ro)); pump(); CHECK(!w.document()->image().editable());
      auto* m=mimeFor(w,wt::adfsTree(w),{find(wt::adfsTree(w),"Elite")}); CHECK(m); if(m){ CHECK(!m->urls().isEmpty()); CHECK(m->state()->errors.isEmpty());
      QJsonObject j=jsonOf(m); CHECK(slurp(QUrl(j["base"].toString()).toLocalFile()+"/Elite")==elite); delete m; } }

    // ---- step 5l: host files whose .inf names a DFS letter, dropped on the window and put by acorndw: the same discs
    printf("== the window and acorndw put place .inf-lettered host files alike\n");
    {   const QString hl=out+"/lettered";
        auto put=[](const QString& p,const QByteArray& b){ QDir().mkpath(QFileInfo(p).absolutePath()); QFile f(p); f.open(QIODevice::WriteOnly); f.write(b); };
        put(hl+"/game",rnd(700,21)); put(hl+"/game.inf","P.GAME 00001900 00008023 000002BC 03\n");
        put(hl+"/low",rnd(90,22)); put(hl+"/low.inf","q.LOW FFFF1900 FFFF8023 0000005A 08\n");
        put(hl+"/hat",rnd(40,23)); put(hl+"/hat.inf","^.HAT 00000E00 00000E00 00000028 03\n");
        put(hl+"/top",rnd(30,24)); put(hl+"/top.inf","$.TOP 00001900 00001900 0000001E 03\n");
        put(hl+"/adfsf",rnd(50,25)); put(hl+"/adfsf.inf","AdfsF 12345678 9ABCDEF0 00000032 0B\n");
        put(hl+"/a.txt","plain: never directory a");
        put(hl+"/Sub/deep",rnd(60,26)); put(hl+"/Sub/deep.inf","P.DEEP 00003000 00003000 0000003C 03\n");
        auto listing=[](Image& im)->QStringList{ QStringList l; QString er;
            std::function<void(quint32,const QString&)> walk=[&](quint32 sec,const QString& at){ Dir d; if(!im.readDir(sec,&d,&er)) { l<<at+" unreadable"; return; }
                for(const Entry& x:d.entries){ const QString p=at+"."+x.name();
                    if(x.isDir()){ l<<p+" D "+x.attrString(); walk(x.sin,p); continue; }
                    QByteArray b; im.readFile(x,&b,&er);
                    l<<QString("%1 %2 %3 %4 %5 %6").arg(p).arg(x.load,8,16,QChar('0')).arg(x.exec,8,16,QChar('0')).arg(x.attrString()).arg(x.length)
                           .arg(QString(QCryptographicHash::hash(b,QCryptographicHash::Md5).toHex())); } };
            walk(RootSector,"$"); return l; };
        for(int flat=0;flat<2;++flat){
            QSettings().setValue("dfsLetterDirs",!flat);
            const QString gw=out+QString("/letters-gui%1.adl").arg(flat), cl=out+QString("/letters-cli%1.adl").arg(flat);
            CHECK(NewDisc::create(gw,NewDisc::AdfsL,"L",false,&e)); QFile::remove(cl); CHECK(QFile::copy(gw,cl));
            { MainWindow w; w.show(); CHECK(w.openFile(gw)); pump(); w.tabWidget()->setCurrentWidget(wt::adfsTree(w)); pump();
              if(!flat) answer({QMessageBox::Ok});                       // the ^.HAT warning ("Some items could not be added")
              dropPaths(wt::adfsTree(w),{hl}); CHECK(flat || g_answered==1); CHECK(w.save()); }
            AcornCli::Backend backend;
            dw::cli::AppInfo info; info.tool="acorndw"; info.program.name="Acorn Disk Wrangler"; info.program.id="acorndiskwrangler";
            info.backend=&backend; info.options=QStringList{"sides","sidecars","title","dosboot","to-sides"};
            QList<QByteArray> args{"acorndw","put",QFile::encodeName(cl),QFile::encodeName(hl),"/",flat?"--letters=flatten":"--letters=dirs","--quiet"};
            QVector<char*> av; for(QByteArray& a:args) av<<a.data();
            CHECK(dw::runCli(av.size(),av.data(),info)==0);
            Image a,b; CHECK(a.open(gw,&e,0,detectDisk(gw).sides)); CHECK(b.open(cl,&e,0,detectDisk(cl).sides));
            const QStringList la=listing(a), lb=listing(b);
            CHECK(la==lb && la.size()>=8);
            if(la!=lb) for(int i=0;i<qMax(la.size(),lb.size());++i) if(la.value(i)!=lb.value(i)) printf("  window [%s]  acorndw [%s]\n",qPrintable(la.value(i)),qPrintable(lb.value(i)));
            const QString j=la.join("\n");
            if(!flat){ CHECK(j.contains("$.lettered.P D") && j.contains("$.lettered.P.GAME 00001900") && j.contains("$.lettered.q D") &&
                             j.contains("$.lettered.q.LOW ffff1900") && j.contains("$.lettered.X_HAT ") && j.contains("$.lettered.TOP ") && j.contains("$.lettered.Sub.P.DEEP")); }
            else { CHECK(j.contains("$.lettered.GAME 00001900") && j.contains("$.lettered.LOW ") && j.contains("$.lettered.HAT ") && !j.contains("$.lettered.P D")); }
            CHECK(!j.contains("$.lettered.a D") && j.contains("$.lettered.AdfsF 12345678 9abcdef0"));   // a.txt is a plain name; no letter, no directory
        }
        QSettings().setValue("dfsLetterDirs",true);
    }

    // ---- stale folders from a crash are removed, current ones kept
    { QString old=RetroFiles::dragDirPrefix()+"testold"; QDir().mkpath(old);
      struct utimbuf t; t.actime=t.modtime=time(nullptr)-3*24*3600; utime(old.toLocal8Bit().constData(),&t);
      QString fresh=RetroFiles::dragDirPrefix()+"testnew"; QDir().mkpath(fresh);
      CHECK(RetroFiles::cleanStale()>=1); CHECK(!QFileInfo::exists(old)); CHECK(QFileInfo::exists(fresh)); QDir(fresh).removeRecursively(); }

    QSettings().clear();
    printf(fails ? "FAILED (%d failures)\n" : "ALL OK (%d failures)\n", fails);
    return fails ? 1 : 0;
}
