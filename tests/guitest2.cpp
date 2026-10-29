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
#include "mainwindow.h"
#include "viewer.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QClipboard>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTest>
#include <QToolBar>
#include <QUrl>
#include <QFile>
#include <cstdio>
using namespace Adfs;
static int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);++fails;} }while(0)

static QTreeWidgetItem* findItem(QTreeWidgetItem* it,const QString& name){
    if(it->data(0,Qt::UserRole+1).toString()==name) return it;
    for(int i=0;i<it->childCount();++i) if(auto*r=findItem(it->child(i),name)) return r; return nullptr; }
static QTreeWidgetItem* find(QTreeWidget* t,const QString& name){ for(int i=0;i<t->topLevelItemCount();++i) if(auto*r=findItem(t->topLevelItem(i),name)) return r; return nullptr; }
static void pump(){ for(int i=0;i<5;++i){ QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);} }
static void sendDrop(QTreeWidget* tree,QMimeData* mime,QTreeWidgetItem* target){
    if(target) tree->scrollToItem(target);
    QPoint pt = target ? tree->visualItemRect(target).center() : QPoint(5,tree->viewport()->height()-5);
    QDragEnterEvent en(pt,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&en);
    QDragMoveEvent mv(pt,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&mv);
    QDropEvent ev(QPointF(pt),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(tree->viewport(),&ev);
    pump();
}
static void dropFiles(MainWindow& w,QTreeWidget* tree,const QStringList& paths,QTreeWidgetItem* target=nullptr){
    w.tabs()->setCurrentWidget(tree); QMimeData m; QList<QUrl> u; for(auto&p:paths) u<<QUrl::fromLocalFile(p); m.setUrls(u); sendDrop(tree,&m,target); }
// drag the named items from srcTree and drop them onto dstTree (optionally onto a named row)
static void drag(MainWindow& src,DropTree* srcTree,const QStringList& names,MainWindow& dst,DropTree* dstTree,const QString& onto=QString(),QTreeWidgetItem* ontoItem=nullptr){
    src.tabs()->setCurrentWidget(srcTree); srcTree->clearSelection(); QList<QTreeWidgetItem*> items;
    for(auto&n:names){ auto*it=find(srcTree,n); if(!it){ printf("  (missing source item %s)\n",qPrintable(n)); ++fails; continue;} it->setSelected(true); items<<it; }
    QMimeData* m=srcTree->makeMime(items); if(!m){ printf("  (no mime)\n"); ++fails; return; }
    dst.tabs()->setCurrentWidget(dstTree);
    QTreeWidgetItem* tgt = ontoItem?ontoItem:onto.isEmpty()?nullptr:find(dstTree,onto);
    sendDrop(dstTree,m,tgt); delete m; }
static void autoYes(int ms=150){ QTimer::singleShot(ms,[]{ auto* mb=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(mb){ auto*b=mb->button(QMessageBox::Yes); if(b) b->click(); else printf("  (no Yes button)\n"); } else printf("  (expected a prompt)\n"); }); }
static MainWindow* openWin(const QString& path){ auto* w=new MainWindow; w->resize(900,600); w->show(); if(!w->openImage(path)){ printf("open failed %s\n",qPrintable(path)); ++fails; } pump(); return w; }
static QStringList tabTexts(MainWindow& w){ QStringList l; for(int i=0;i<w.tabs()->count();++i) l<<w.tabs()->tabText(i); return l; }

int main(int argc,char**argv){
    QApplication app(argc,argv);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);   // Qt's own file dialog, which a test can fill in
    QApplication::setOrganizationName("acorndiskwrangler-test"); QApplication::setApplicationName("t2");
    QString d=argv[1];       // directory holding the working copies
    // watchdog: any modal dialog that nobody is expecting is a bug in the app or the test - report it and dismiss it, never hang
    QTimer guard; guard.setInterval(2000);
    QObject::connect(&guard,&QTimer::timeout,[]{ if(QWidget* m=QApplication::activeModalWidget()){ QString t; if(auto* mb=qobject_cast<QMessageBox*>(m)) t=mb->windowTitle()+": "+mb->text()+" | "+mb->detailedText(); else t=m->windowTitle();
        printf("  !! unexpected modal dialog [%s]\n",qPrintable(t.replace('\n',' '))); ++fails; m->close(); } });
    guard.start();
    // ---------------------------------------------------------------- open everything
    MainWindow* A=openWin(d+"/a.dsd");  MainWindow* B=openWin(d+"/b.adl"); MainWindow* C=openWin(d+"/c.img");
    MainWindow* D=openWin(d+"/d.dat");  MainWindow* E=openWin(d+"/e.ssd"); MainWindow* F=openWin(d+"/f.hybrid"); MainWindow* G=openWin(d+"/g.a800");
    printf("tabs: A=%s B=%s C=%s D=%s E=%s F=%s G=%s\n",qPrintable(tabTexts(*A).join("|")),qPrintable(tabTexts(*B).join("|")),qPrintable(tabTexts(*C).join("|")),qPrintable(tabTexts(*D).join("|")),qPrintable(tabTexts(*E).join("|")),qPrintable(tabTexts(*F).join("|")),qPrintable(tabTexts(*G).join("|")));
    CHECK(MainWindow::windows().size()==7);
    CHECK(tabTexts(*A)==QStringList({"DFS"}));
    // both sides in one tree, a root row each (as CP/M Disk Wrangler shows a disc with a filesystem per side)
    CHECK(A->dfsTree()->topLevelItemCount()==2 && A->dfsRoot(0) && A->dfsRoot(1));
    if(A->dfsRoot(1)){ CHECK(A->dfsRoot(0)->text(0).startsWith("Drive 0")); CHECK(A->dfsRoot(1)->text(0).startsWith("Drive 2")); }
    CHECK(E->dfsTree()->topLevelItemCount()==1); CHECK(tabTexts(*B)==QStringList({"ADFS"})); CHECK(tabTexts(*C)==QStringList({"PC FAT"}));
    CHECK(tabTexts(*D)==QStringList({"ADFS","DOS Plus (DRIVE_C)"})); CHECK(tabTexts(*E)==QStringList({"DFS"}));
    CHECK(tabTexts(*F)==QStringList({"ADFS (read-only)","DOS Plus (640K hybrid)"})); CHECK(tabTexts(*G)==QStringList({"DOS Plus (800K)"}));
    CHECK(A->info().kind==DiskInfo::Dfs && A->info().dfs.doubleSided && B->info().kind==DiskInfo::Adfs && C->info().kind==DiskInfo::DosPc);
    CHECK(E->image().needsPadding()); CHECK(!A->image().needsPadding());
    CHECK(A->paneEditable(PaneDfs0)&&A->paneEditable(PaneDfs1)&&B->paneEditable(PaneAdfs)&&C->paneEditable(PaneDos)&&D->paneEditable(PaneAdfs)&&D->paneEditable(PaneDos));
    CHECK(!F->paneEditable(PaneAdfs)); CHECK(F->paneEditable(PaneDos)); CHECK(G->paneEditable(PaneDos)); CHECK(!G->image().isAdfs());

    // ---------------------------------------------------------------- host files onto a DFS side (+ .inf, folder flattened)
    dropFiles(*A,A->dfsTree(),{"/tmp/drop/proj/hello.txt","/tmp/drop/proj/hello.txt.inf","/tmp/drop/proj/prog.bas","/tmp/drop/proj/prog.bas.inf","/tmp/drop/proj/test.bas","/tmp/drop/proj/sub"});
    printf("A side0 files:"); for(auto&f:A->dfs(0).files()) printf(" %s",qPrintable(f.fullName())); printf("\n");
    CHECK(A->dfs(0).files().size()==7);
    { int i=A->dfs(0).indexOf('$',"hello"); CHECK(i>=0); if(i>=0){ auto f=A->dfs(0).files()[i]; CHECK(f.load==0x31900 && f.exec==0x38023 && f.locked); }
      i=A->dfs(0).indexOf('$',"prog"); CHECK(i>=0); if(i>=0){ auto f=A->dfs(0).files()[i]; CHECK(f.load==0x1900 && f.exec==0x1900 && !f.locked && f.length==5000); } }
    CHECK(A->image().dirty());

    // ---------------------------------------------------------------- DFS drive 0 -> drive 2 (two sides of one disc, one window)
    drag(*A,A->dfsTree(),{"$.hello","$.prog"},*A,A->dfsTree(),QString(),A->dfsRoot(1));
    CHECK(A->dfs(1).files().size()==2); { int i=A->dfs(1).indexOf('$',"hello"); CHECK(i>=0 && A->dfs(1).files()[i].locked && A->dfs(1).files()[i].load==0x31900); }
    CHECK(A->dfs(0).files().size()==7);           // copy, not move
    // onto its own side: nothing happens (no "replace?" question either - the watchdog would report one)
    drag(*A,A->dfsTree(),{"$.hello"},*A,A->dfsTree(),"$.prog");
    CHECK(A->dfs(0).files().size()==7 && A->dfs(1).files().size()==2);
    // the selected or current row says which side Add files, Paste, Boot option... use
    A->tabs()->setCurrentWidget(A->dfsTree()); A->dfsTree()->clearSelection();
    A->dfsTree()->setCurrentItem(find(A->dfsTree(),"$.hello"),0,QItemSelectionModel::NoUpdate); CHECK(A->dfsSide()==0);
    A->dfsTree()->setCurrentItem(A->dfsRoot(1),0,QItemSelectionModel::NoUpdate); CHECK(A->dfsSide()==1);

    // ---------------------------------------------------------------- DFS -> ADFS floppy (attributes extended, locked kept)
    drag(*A,A->dfsTree(),{"$.hello","$.d_bin","$.test_ba"},*B,B->tree());
    { Dir r; QString e; CHECK(B->image().readDir(RootSector,&r,&e)); int i=r.indexOf("hello"); CHECK(i>=0 && r.indexOf("d_bin")>=0 && r.indexOf("test_ba")>=0);
      if(i>=0){ auto en=r.entries[i]; CHECK(en.load==0xFFFF1900 && en.exec==0xFFFF8023 && en.flag(Entry::L) && en.flag(Entry::R) && en.flag(Entry::W)); } }
    // second copy of the same file: conflict prompt -> Replace
    autoYes(); drag(*A,A->dfsTree(),{"$.hello"},*B,B->tree()); CHECK(B->image().dirty());

    // ---------------------------------------------------------------- ADFS floppy -> PC FAT floppy
    drag(*B,B->tree(),{"hello","d_bin"},*C,C->dosTree());
    { QVector<Dos::DEntry> L; QString e; CHECK(C->dos().listDir(0,&L,&e)); bool h=false,dd=false; for(auto&x:L){ if(x.name=="HELLO"){ h=true; CHECK(x.attr&Dos::AttrR); CHECK(x.size==20);} if(x.name=="D_BIN"){ dd=true; CHECK(x.size==70000);} } CHECK(h&&dd); }

    // ---------------------------------------------------------------- hard disc -> floppies: attributes preserved exactly, directory tree copied
    drag(*D,D->tree(),{"!Config","BeebSCSI","DOSBOOT"},*B,B->tree());
    { Dir src,dst; QString e; D->image().readDir(RootSector,&src,&e); B->image().readDir(RootSector,&dst,&e);
      auto s=src.entries[src.indexOf("!Config")]; int j=dst.indexOf("!Config"); CHECK(j>=0);
      if(j>=0){ auto t=dst.entries[j]; CHECK(t.load==s.load && t.exec==s.exec && t.length==s.length && t.attrString()==s.attrString()); }
      auto sd=src.entries[src.indexOf("BeebSCSI")]; int k=dst.indexOf("BeebSCSI"); CHECK(k>=0 && dst.entries[k].isDir());
      Dir sc,dc; D->image().readDir(sd.sin,&sc,&e); if(k>=0) B->image().readDir(dst.entries[k].sin,&dc,&e); CHECK(sc.entries.size()==dc.entries.size() && sc.entries.size()==2);
      for(auto&x:sc.entries){ int q=dc.indexOf(x.name()); CHECK(q>=0); if(q>=0){ QByteArray a,b; D->image().readFile(x,&a,&e); B->image().readFile(dc.entries[q],&b,&e); CHECK(a==b && dc.entries[q].load==x.load); } } }
    // stamped RISC OS style file -> DFS: narrowed to 18 bits like any address, never zeroed on a guess
    drag(*D,D->tree(),{"!Config"},*E,E->dfsTree());
    { Dir src; QString e; D->image().readDir(RootSector,&src,&e); auto s=src.entries[src.indexOf("!Config")];
      int i=E->dfs(0).indexOf('$',"!Config"); CHECK(i>=0 && E->dfs(0).files()[i].load==(s.load&0x3FFFF) && E->dfs(0).files()[i].exec==(s.exec&0x3FFFF) && E->dfs(0).files()[i].length==128); }
    // DOS partition inside the hard disc -> DFS side 1 and ADFS floppy
    drag(*D,D->dosTree(),{"ALARM.DAT"},*A,A->dfsTree(),QString(),A->dfsRoot(1)); CHECK(A->dfs(1).indexOf('$',"ALARM_D")>=0);
    drag(*D,D->dosTree(),{"ALARM.DAT"},*B,B->tree()); { Dir r; QString e; B->image().readDir(RootSector,&r,&e); CHECK(r.indexOf("ALARM_DAT")>=0); }
    // a DOS directory tree -> DFS (flattened) : build one on the PC floppy first
    { quint32 sub=0; QString e; CHECK(C->dos().makeDir(0,"SUB",&e,&sub)); CHECK(C->dos().addFile(sub,"X.TXT",QByteArray("xx"),false,&e)); CHECK(C->dos().addFile(sub,"Y.TXT",QByteArray("yyy"),false,&e)); C->refreshTree(); }
    drag(*C,C->dosTree(),{"SUB"},*E,E->dfsTree()); CHECK(E->dfs(0).indexOf('$',"X_TXT")>=0 && E->dfs(0).indexOf('$',"Y_TXT")>=0);
    // ... and the same directory into the ADFS floppy as a real directory
    drag(*C,C->dosTree(),{"SUB"},*B,B->tree()); { Dir r; QString e; B->image().readDir(RootSector,&r,&e); int i=r.indexOf("SUB"); CHECK(i>=0 && r.entries[i].isDir()); }
    // DFS -> DOS keeps nothing but the name: 7 chars -> 8.3, locked -> read-only
    drag(*A,A->dfsTree(),{"$.hello"},*G,G->dosTree()); { QVector<Dos::DEntry> L; QString e; G->dos().listDir(0,&L,&e); bool h=false; for(auto&x:L) if(x.name=="HELLO"){ h=true; CHECK(x.attr&Dos::AttrR);} CHECK(h); }
    // read-only target: the ADFS side of the hybrid refuses (checked via paneEditable, no dialog needed)
    drag(*A,A->dfsTree(),{"$.hello"},*F,F->dosTree()); { QVector<Dos::DEntry> L; QString e; F->dos().listDir(0,&L,&e); bool h=false; for(auto&x:L) if(x.name=="HELLO") h=true; CHECK(h); }

    // ---------------------------------------------------------------- host drops on the other kinds of disc
    dropFiles(*G,G->dosTree(),{"/tmp/drop/proj/sub"}); { QVector<Dos::DEntry> L; QString e; G->dos().listDir(0,&L,&e); bool s=false; for(auto&x:L) if(x.name=="SUB"&&x.isDir()) s=true; CHECK(s); }
    // DFS rename / properties / delete through the real slots
    { auto*it=find(A->dfsTree(),"$.prog"); A->tabs()->setCurrentWidget(A->dfsTree()); A->dfsTree()->clearSelection(); it->setSelected(true);
      QTimer::singleShot(150,[]{ auto* dlg=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); if(dlg){ dlg->setTextValue("prog2"); dlg->accept(); } else printf("  (no rename dialog)\n"); });
      QMetaObject::invokeMethod(A,"renameSelected",Qt::DirectConnection); CHECK(A->dfs(0).indexOf('$',"prog2")>=0 && A->dfs(0).indexOf('$',"prog")<0); }
    { A->tabs()->setCurrentWidget(A->dfsTree()); QTreeWidgetItem* root=A->dfsTree()->topLevelItem(0); A->dfsTree()->clearSelection(); root->setSelected(true);
      QTimer::singleShot(150,[]{ auto* dlg=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); if(dlg){ dlg->setTextValue("SIDE ZERO"); dlg->accept(); } else printf("  (no title dialog)\n"); });
      QMetaObject::invokeMethod(A,"renameSelected",Qt::DirectConnection); CHECK(A->dfs(0).title()=="SIDE ZERO"); }


    // ---------------------------------------------------------------- menus and buttons: the same order as CP/M Disk Wrangler's
    {   // (this program's extra items sit in between; with them taken out, both programs list the same things in the same order)
        QStringList edit, buttons, common;
        for(QAction* m : A->menuBar()->actions()) if(m->menu() && m->text()=="&Edit")
            for(QAction* a : m->menu()->actions()) edit << (a->isSeparator()?QString("-"):a->text().remove('&'));
        for(QToolBar* tb : A->findChildren<QToolBar*>()) for(QPushButton* b : tb->findChildren<QPushButton*>()) buttons << b->text();
        printf("  Edit: %s\n  buttons: %s\n",qPrintable(edit.join(" | ")),qPrintable(buttons.join(" | ")));
        CHECK(edit==QStringList({"View (text / hex / BASIC)","Extract to folder...","-","Add files...","Add folder...","New directory...","-",
                                 "Copy","Paste","-","Rename...","Properties / attributes...","Delete","-","Rename disc / volume...","Boot option..."}));
        CHECK(buttons==QStringList({"View","Extract","Add files","Add folder","New dir","Rename","Properties","Delete"}));
        for(const QString& b : buttons) if(b!="Add folder" && b!="New dir") common << b;
        CHECK(common==QStringList({"View","Extract","Add files","Rename","Properties","Delete"}));   // = CP/M Disk Wrangler's, before its Format
        QPushButton* nd=nullptr; for(QPushButton* b : A->findChildren<QPushButton*>()) if(b->text()=="New dir") nd=b;
        CHECK(nd && nd->toolTip().contains("ADFS") && nd->toolTip().contains("DFS discs have no directories"));
        A->tabs()->setCurrentWidget(A->dfsTree()); pump(); CHECK(nd && !nd->isEnabled());      // a DFS disc: greyed out, the tip says why
        CHECK(A->windowTitle()=="a.dsd * - Acorn Disk Wrangler");
        MainWindow empty; CHECK(empty.windowTitle()=="no disc - Acorn Disk Wrangler");
        QLabel* st=nullptr; for(QLabel* l : empty.statusBar()->findChildren<QLabel*>()) if(l->text().contains("drop one here")) st=l;
        CHECK(st!=nullptr);
    }

    // ---------------------------------------------------------------- copy and paste, with the platform's keys
    {
        auto action=[](MainWindow* w,const QString& text)->QAction*{ for(QAction* a : w->findChildren<QAction*>()) if(a->text()==text) return a; return nullptr; };
        auto key=[](MainWindow* w,QWidget* tree,QKeySequence::StandardKey k){ w->activateWindow(); QTest::qWaitForWindowActive(w); tree->setFocus(); pump();
            QTest::keySequence(tree,QKeySequence(k)); pump(); };
        QClipboard* cb=QApplication::clipboard();
        // copy from DFS drive 0 with Ctrl+C (the tree's own Ctrl+C, which copies a row's text, must not get it)
        A->tabs()->setCurrentWidget(A->dfsTree()); A->dfsTree()->clearSelection(); find(A->dfsTree(),"$.test_ba")->setSelected(true);
        key(A,A->dfsTree(),QKeySequence::Copy);
        const QMimeData* cm=cb->mimeData();
        CHECK(cm && cm->hasFormat(WRANGLER_MIME_CLIP) && cm->hasFormat(RetroFiles::MimeType) && cm->hasUrls() && cm->hasFormat("x-special/gnome-copied-files"));
        // paste onto drive 2 of the same disc with Ctrl+V
        A->dfsTree()->clearSelection(); A->dfsTree()->setCurrentItem(A->dfsRoot(1)); CHECK(A->dfsSide()==1);
        CHECK(action(A,"&Paste") && action(A,"&Paste")->isEnabled());
        key(A,A->dfsTree(),QKeySequence::Paste);
        { int i=A->dfs(0).indexOf('$',"test_ba"), j=A->dfs(1).indexOf('$',"test_ba"); CHECK(i>=0 && j>=0);
          if(i>=0&&j>=0){ QByteArray a,b; QString e; A->dfs(0).readFile(A->dfs(0).files()[i],&a,&e); A->dfs(1).readFile(A->dfs(1).files()[j],&b,&e);
            CHECK(a==b && A->dfs(0).files()[i].load==A->dfs(1).files()[j].load); } }
        // what was copied is kept: the file can go from the source disc and still be pasted elsewhere (here the PC floppy)
        { QString e; CHECK(A->dfs(0).remove('$',"test_ba",&e)); A->refreshDfs(); }
        C->tabs()->setCurrentWidget(C->dosTree()); C->dosTree()->setCurrentItem(C->dosTree()->topLevelItem(0)); pump();
        QMetaObject::invokeMethod(C,"paste",Qt::DirectConnection);
        { QVector<Dos::DEntry> L; QString e; C->dos().listDir(0,&L,&e); bool t=false; for(auto&x:L) if(x.name=="TEST_BA") t=true; CHECK(t); }
        // files copied in a file manager: a URL list (with an .inf beside it) ...
        { auto* m=new QMimeData; m->setUrls({QUrl::fromLocalFile("/tmp/drop/proj/hello.txt")}); cb->setMimeData(m); pump();
          E->tabs()->setCurrentWidget(E->dfsTree()); E->dfsTree()->setCurrentItem(E->dfsRoot(0)); pump();
          key(E,E->dfsTree(),QKeySequence::Paste); CHECK(E->dfs(0).indexOf('$',"hello")>=0); }
        // ... or GNOME Files' own list
        { auto* m=new QMimeData; m->setData("x-special/gnome-copied-files","copy\nfile:///tmp/drop/proj/test.bas"); cb->setMimeData(m); pump();
          G->tabs()->setCurrentWidget(G->dosTree()); pump();
          QMetaObject::invokeMethod(G,"paste",Qt::DirectConnection);
          QVector<Dos::DEntry> L; QString e; G->dos().listDir(0,&L,&e); bool t=false; for(auto&x:L) if(x.name=="TEST.BAS") t=true; CHECK(t); }
        // copied here, pasted in a file manager: the files are written when it asks, as for a drag
        A->tabs()->setCurrentWidget(A->dfsTree()); A->dfsTree()->clearSelection(); find(A->dfsTree(),"$.prog")->setSelected(true);   // drive 2's
        QMetaObject::invokeMethod(A,"copySelected",Qt::DirectConnection);
        { const QMimeData* m=cb->mimeData(); QList<QUrl> u=m?m->urls():QList<QUrl>(); QStringList names;
          for(const QUrl& x : u) names << QFileInfo(x.toLocalFile()).fileName();
          CHECK(names==QStringList({"prog","prog.inf"}));             // the file and its .inf, as Extract and a drag write them
          if(!u.isEmpty()){ QFile f(u[0].toLocalFile()); CHECK(f.open(QIODevice::ReadOnly)); int i=A->dfs(1).indexOf('$',"prog"); QByteArray want; QString e;
            if(i>=0) A->dfs(1).readFile(A->dfs(1).files()[i],&want,&e); CHECK(i>=0 && f.readAll()==want); }
          CHECK(m && m->data("x-special/gnome-copied-files").startsWith("copy\nfile://")); }
        // nothing to paste into a read-only disc (the ADFS side of the 640K hybrid)
        F->tabs()->setCurrentIndex(0); pump(); CHECK(action(F,"&Paste") && !action(F,"&Paste")->isEnabled());
    }

    // ---------------------------------------------------------------- optional: a REAL Acorn 640K hybrid disc (argv[2], modified: pass a copy)
    if(argc>2){
        QByteArray before; { QFile f(argv[2]); f.open(QIODevice::ReadOnly); before=f.readAll(); }
        MainWindow* H=openWin(argv[2]);
        printf("real hybrid tabs: %s\n",qPrintable(tabTexts(*H).join("|")));
        CHECK(tabTexts(*H)==QStringList({"ADFS (read-only)","DOS Plus (640K hybrid)"})); CHECK(H->info().kind==DiskInfo::AdfsDosHybrid);
        CHECK(!H->image().needsPadding()); CHECK(H->image().editable()); CHECK(!H->paneEditable(PaneAdfs)); CHECK(H->paneEditable(PaneDos));
        CHECK(H->dos().label()=="M512 Disk 1"); CHECK(H->dos().totalClusters()==316);
        { QVector<Dos::DEntry> L; QString e; CHECK(H->dos().listDir(0,&L,&e));   // real CONTENT, not just structure: ISSUE.BAT must be a readable batch file
          bool seen=false; for(auto&x:L) if(x.name=="ISSUE.BAT"){ seen=true; QByteArray b; CHECK(H->dos().readFile(x,&b,&e)); CHECK(b.startsWith("echo off\r\n")); } CHECK(seen); }
        CHECK(!H->image().dirty());
        dropFiles(*H,H->dosTree(),{"/tmp/drop/proj/hello.txt","/tmp/drop/proj/sub"});
        CHECK(H->image().dirty()); CHECK(H->save());
        CHECK(QFile(argv[2]).size()==655360);
        QByteArray after; { QFile f(argv[2]); f.open(QIODevice::ReadOnly); after=f.readAll(); }
        CHECK(after.left(16*256)==before.left(16*256));
        // the real ADFS side can be browsed and copied FROM (here to the ADFS floppy window B), but not into
        // (B already holds a 3584-byte DOSBOOT from the hard disc, so this asks before replacing it with the hybrid's 2303-byte one)
        autoYes(); drag(*H,H->tree(),{"dosboot"},*B,B->tree());
        { Dir r; QString e; B->image().readDir(RootSector,&r,&e); int i=r.indexOf("dosboot"); CHECK(i>=0 && r.entries[i].length==2303); }
        // DOS files copy out of the real disc into a DFS side
        autoYes(); drag(*H,H->dosTree(),{"ALARM.DAT"},*A,A->dfsTree(),QString(),A->dfsRoot(1)); CHECK(A->dfs(1).indexOf('$',"ALARM_D")>=0);
        CHECK(B->save()); CHECK(A->save());
    }

    // ---------------------------------------------------------------- Save as: B (the ADFS L floppy, with unsaved changes) to a new file
    {
        QString orig=B->image().path(), copy=QFileInfo(d+"/b-copy.adl").absoluteFilePath();
        QFile::remove(copy);
        QByteArray before; { QFile f(orig); f.open(QIODevice::ReadOnly); before=f.readAll(); }
        B->tabs()->setCurrentWidget(B->tree()); CHECK(B->setBootOption(2));   // an unsaved change for the copy to carry
        CHECK(B->image().dirty());
        QTimer::singleShot(150,[copy]{ auto* fd=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if(fd){ fd->selectFile(copy); QMetaObject::invokeMethod(fd,"accept"); } else printf("  (no Save as dialog)\n"); });
        QMetaObject::invokeMethod(B,"saveAsDialog",Qt::DirectConnection); pump();
        CHECK(B->image().path()==copy);                       // the window carries on with the copy
        CHECK(!B->image().dirty());                           // the changes went into it
        CHECK(QFileInfo(copy).size()==655360);
        CHECK(B->windowTitle().startsWith("b-copy.adl - "));
        QByteArray after; { QFile f(orig); f.open(QIODevice::ReadOnly); after=f.readAll(); }
        CHECK(after==before);                                 // the original file is untouched
        MainWindow re; CHECK(re.openImage(copy));             // the copy reopens with the files in it
        Dir r; QString e; CHECK(re.image().readDir(RootSector,&r,&e)); CHECK(r.indexOf("hello")>=0 && r.indexOf("BeebSCSI")>=0);
        CHECK(re.bootOption()==2);                            // ... and the change made before Save as
    }


    // ---------------------------------------------------------------- save everything, check the files
    for(MainWindow* w : {A,B,C,D,E,F,G}){ CHECK(w->save()); }
    CHECK(QFile(d+"/e.ssd").size()==102400);      // the truncated image was padded to a full disc
    CHECK(QFile(d+"/a.dsd").size()==204800);
    for(MainWindow* w : {A,B,C,D,E,F,G}) CHECK(!w->image().dirty() && !w->image().needsPadding());
    printf("\n%s (%d failures)\n",fails?"FAILED":"ALL OK",fails);
    return fails?1:0;
}
