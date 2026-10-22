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
#include <QApplication>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QTimer>
#include <QDialog>
#include <QCheckBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QFile>
#include <cstdio>
#include "mainwindow.h"
#include "viewer.h"
#include "viewtext.h"
#include <QComboBox>
#include <QInputDialog>
#include <functional>
using namespace Adfs;
static int fails=0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while(0)
static QTreeWidgetItem* findItem(QTreeWidgetItem* it, const QString& name){
    if (it->data(0,Qt::UserRole+1).toString()==name) return it;
    for(int i=0;i<it->childCount();++i) if(auto*r=findItem(it->child(i),name)) return r;
    return nullptr;
}
static void dropOn(QTreeWidget* tree, const QStringList& paths, QTreeWidgetItem* target){
    auto* mime=new QMimeData; QList<QUrl> urls; for(auto&p:paths) urls<<QUrl::fromLocalFile(p); mime->setUrls(urls);
    if (target) tree->scrollToItem(target);
    QPoint pt = target ? tree->visualItemRect(target).center() : QPoint(5, tree->viewport()->height()-5);
    QDragEnterEvent en(pt, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &en);
    QDragMoveEvent mv(pt, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &mv);
    QDropEvent ev(QPointF(pt), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &ev);
    delete mime;
}
static void drop(MainWindow& w, const QStringList& paths, QTreeWidgetItem* target){ dropOn(w.tree(),paths,target); }
int main(int argc,char**argv){
    QApplication app(argc,argv);
    QApplication::setOrganizationName("acorndiskwrangler-test"); QApplication::setApplicationName("t");
    MainWindow w; w.resize(1000,650); w.show();
    CHECK(w.openImage(argv[1]));
    CHECK(w.image().editable());
    QTreeWidgetItem* root=w.tree()->topLevelItem(0);
    CHECK(root && root->childCount()==12);

    // 1. drop a folder + files onto empty space (-> root)
    drop(w,{"/tmp/drop/proj/sub","/tmp/drop/proj/hello.txt","/tmp/drop/proj/hello.txt.inf","/tmp/drop/proj/prog.bas","/tmp/drop/proj/prog.bas.inf",
            "/tmp/drop/proj/long_filename_example1.dat","/tmp/drop/proj/long_filename_example2.dat"}, nullptr);
    CHECK(w.image().dirty());
    // 2. drop onto an existing directory row (Library)
    QTreeWidgetItem* lib=findItem(w.tree()->topLevelItem(0),"Library");
    CHECK(lib);
    drop(w,{"/tmp/drop/proj/hello.txt"}, lib);
    // 3. drop onto a FILE row -> goes into that file's parent directory
    QTreeWidgetItem* cfg=findItem(w.tree()->topLevelItem(0),"!Config"); CHECK(cfg);
    drop(w,{"/tmp/drop/proj/sub/a.bin"}, cfg);

    w.grab().save("/tmp/gui_before_save.png");
    Report r=w.image().validate(); CHECK(r.ok());
    for(auto&s:r.errors) printf("ERR %s\n",qPrintable(s));
    CHECK(w.save());
    CHECK(!w.image().dirty());
    w.grab().save("/tmp/gui_after_save.png");

    // verify through the fs layer after a fresh open
    Image img; QString err; CHECK(img.open(argv[1],&err)); CHECK(img.openReport().ok()); CHECK(img.editable());
    Dir d; CHECK(img.readDir(RootSector,&d,&err));
    for(auto&e:d.entries) printf("  %-10s %-5s load=%08X exec=%08X len=%u\n",qPrintable(e.name()),qPrintable(e.attrString()),e.load,e.exec,e.length);
    int ih=d.indexOf("hello"); CHECK(ih>=0);
    if(ih>=0){ CHECK(d.entries[ih].load==0xFFFF1900 && d.entries[ih].exec==0xFFFF8023); CHECK(d.entries[ih].flag(Entry::L)&&d.entries[ih].flag(Entry::R)&&d.entries[ih].flag(Entry::W));
        QByteArray b; img.readFile(d.entries[ih],&b,&err); CHECK(b=="Hello from the host\n"); }
    int ip=d.indexOf("prog"); CHECK(ip>=0); if(ip>=0){ CHECK(d.entries[ip].load==0x1900); QByteArray b; img.readFile(d.entries[ip],&b,&err); QFile f("/tmp/drop/proj/prog.bas"); f.open(QIODevice::ReadOnly); CHECK(b==f.readAll()); }
    CHECK(d.indexOf("long_filen")>=0 && d.indexOf("long_fil~1")>=0);   // truncation clash made unique
    int is=d.indexOf("sub"); CHECK(is>=0 && d.entries[is].isDir());
    Dir sd; CHECK(img.readDir(d.entries[is].sin,&sd,&err)); CHECK(sd.entries.size()==3);
    Dir nd; CHECK(img.readDir(sd.entries[sd.indexOf("nested")].sin,&nd,&err)); CHECK(nd.entries.size()==2);
    QByteArray big; img.readFile(nd.entries[nd.indexOf("d_bin")],&big,&err); QFile f2("/tmp/drop/proj/sub/nested/d.bin"); f2.open(QIODevice::ReadOnly); CHECK(big==f2.readAll());
    Dir ld; CHECK(img.readDir(d.entries[d.indexOf("Library")].sin,&ld,&err)); CHECK(ld.indexOf("hello.txt")>=0 || ld.indexOf("hello")>=0);
    int iL=d.indexOf("Library"); (void)iL;

    // ---- Properties dialog: rename, clear L, set E, change load/exec on "hello"
    {
        QTreeWidgetItem* h=findItem(w.tree()->topLevelItem(0),"hello"); CHECK(h);
        w.tree()->setCurrentItem(h); h->setSelected(true);
        QTimer::singleShot(200,[&]{
            QDialog* dlg=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dlg){printf("no dialog\n");return;}
            auto edits=dlg->findChildren<QLineEdit*>(); auto boxes=dlg->findChildren<QCheckBox*>();
            // order: name, load, exec ; boxes: R W L E (findChildren returns creation order)
            edits[0]->setText("Hello2"); edits[1]->setText("1900"); edits[2]->setText("DEADBEEF");
            boxes[2]->setChecked(false);  // L off
            boxes[3]->setChecked(true);   // E on
            dlg->accept();
        });
        QMetaObject::invokeMethod(&w,"editProperties",Qt::DirectConnection);
        CHECK(w.image().dirty());
        Dir d2; QString e2; w.image().readDir(RootSector,&d2,&e2);
        int i=d2.indexOf("Hello2"); CHECK(i>=0 && d2.indexOf("hello")<0);
        if(i>=0){ CHECK(!d2.entries[i].flag(Entry::L)); CHECK(d2.entries[i].flag(Entry::E)); CHECK(d2.entries[i].load==0x1900 && d2.entries[i].exec==0xDEADBEEF); }
    }
    // ---- Delete dialog: remove the whole "sub" tree and check space is returned
    {
        quint32 freeBefore=w.image().freeSectors();
        QTreeWidgetItem* s=findItem(w.tree()->topLevelItem(0),"sub"); CHECK(s);
        w.tree()->clearSelection(); s->setSelected(true); w.tree()->setCurrentItem(s);
        QTimer::singleShot(200,[&]{ auto* mb=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(mb){ printf("  delete prompt: %s\n",qPrintable(mb->text().replace('\n',' '))); mb->button(QMessageBox::Yes)->click(); } else printf("no msgbox\n"); });
        QMetaObject::invokeMethod(&w,"deleteSelected",Qt::DirectConnection);
        CHECK(w.image().freeSectors()>freeBefore);
        CHECK(findItem(w.tree()->topLevelItem(0),"sub")==nullptr);
        CHECK(w.image().validate().ok());
    }
    CHECK(w.save());
    { Image i2; QString e3; CHECK(i2.open(argv[1],&e3)); CHECK(i2.openReport().ok()); Dir d3; i2.readDir(RootSector,&d3,&e3); CHECK(d3.indexOf("Hello2")>=0 && d3.indexOf("sub")<0);
      printf("  final: %s\n",qPrintable(i2.openReport().info.join(" "))); }
    w.grab().save("/tmp/gui_final.png");

    // ======================= DOS partition, viewer =======================
    CHECK(w.tabs()->count()==2); CHECK(w.dos().attached()); CHECK(w.dosEditable());
    printf("  DOS tab: '%s' label='%s'\n",qPrintable(w.tabs()->tabText(1)),qPrintable(w.dos().label()));
    { QTreeWidgetItem* dr=w.dosTree()->topLevelItem(0); CHECK(dr && dr->childCount()==69); }
    // find real BASIC programs anywhere on the disc
    { int nb=0; QString shown; std::function<void(quint32,QString)> walk=[&](quint32 sec,QString p){ Dir d; QString e; if(!w.image().readDir(sec,&d,&e)) return;
        for(auto&x:d.entries){ if(x.isDir()) walk(x.sin,p+"."+x.name()); else if(x.length>8 && x.length<65536){ QByteArray b; w.image().readFile(x,&b,&e); if(ViewText::looksLikeBasic(b)){ ++nb; if(shown.isEmpty()){ QString t; ViewText::detokenise(b,&t,&e); shown=p+"."+x.name()+":\n"+t.left(300);} } } } };
      walk(RootSector,"$"); printf("  BASIC-looking ADFS files on the image: %d\n",nb); if(nb) printf("  first one: %s\n",qPrintable(shown)); }
    // ---- drop a tokenised BASIC program onto the ADFS side and view it
    { QFile f("/tmp/drop/proj/test.bas"); f.open(QIODevice::WriteOnly);
      QByteArray p = QByteArray::fromHex("0D000A0DF1202248454C4C4F22"); p+=QByteArray::fromHex("0D00140AE5208D544A4A40"); p=QByteArray::fromHex("0D000A0DF1202248454C4C4F22")+QByteArray::fromHex("0D0014 0A E5 20 8D 54 4A 40")+QByteArray::fromHex("0DFF"); f.write(p); f.close(); }
    drop(w,{"/tmp/drop/proj/test.bas"},nullptr);
    { QTreeWidgetItem* it=findItem(w.tree()->topLevelItem(0),"test_bas"); CHECK(it); w.tree()->clearSelection(); it->setSelected(true);
      QMetaObject::invokeMethod(&w,"viewSelected",Qt::DirectConnection);
      auto vs=w.findChildren<ViewerDialog*>(); CHECK(vs.size()==1);
      if(vs.size()==1){ CHECK(vs[0]->mode()==ViewerDialog::Basic); CHECK(vs[0]->shownText().contains("   10 PRINT \"HELLO\"")); CHECK(vs[0]->shownText().contains("   20 GOTO 10"));
        vs[0]->grab().save("/tmp/gui_basic.png");
        auto* combo=vs[0]->findChild<QComboBox*>(); combo->setCurrentIndex(ViewerDialog::Hex); CHECK(vs[0]->shownText().startsWith("00000000  0D 00 0A 0D F1"));
        combo->setCurrentIndex(ViewerDialog::Text); CHECK(!vs[0]->shownText().isEmpty());
        vs[0]->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); } }
    // ---- text and hex auto-detection on other files
    { QTreeWidgetItem* it=findItem(w.tree()->topLevelItem(0),"Hello2"); CHECK(it); w.tree()->clearSelection(); it->setSelected(true);
      QMetaObject::invokeMethod(&w,"viewSelected",Qt::DirectConnection); auto vs=w.findChildren<ViewerDialog*>(); CHECK(vs.size()==1);
      if(vs.size()==1){ CHECK(vs[0]->mode()==ViewerDialog::Text); CHECK(vs[0]->shownText()=="Hello from the host\n"); vs[0]->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
      QTreeWidgetItem* pr=findItem(w.tree()->topLevelItem(0),"prog"); CHECK(pr); w.tree()->clearSelection(); pr->setSelected(true);
      QMetaObject::invokeMethod(&w,"viewSelected",Qt::DirectConnection); vs=w.findChildren<ViewerDialog*>(); CHECK(vs.size()==1);
      if(vs.size()==1){ CHECK(vs[0]->mode()==ViewerDialog::Hex); vs[0]->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); } }
    qApp->processEvents();

    // ---- DOS tab: drag and drop a folder and files
    w.tabs()->setCurrentIndex(1);
    CHECK(w.dosActive());
    dropOn(w.dosTree(),{"/tmp/drop/proj/sub","/tmp/drop/proj/hello.txt","/tmp/drop/proj/long_filename_example1.dat","/tmp/drop/proj/long_filename_example2.dat"},nullptr);
    { QTreeWidgetItem* r=w.dosTree()->topLevelItem(0);
      CHECK(findItem(r,"SUB")); CHECK(findItem(r,"HELLO.TXT")); CHECK(findItem(r,"LONG_FIL.DAT")); CHECK(findItem(r,"LONG_F~1.DAT")); CHECK(findItem(r,"D_BIN")==nullptr && findItem(r,"D.BIN")==nullptr?false:true);
      // drop onto a DOS directory row (BACKUP? use SUB that we just made) and onto a file row (-> parent)
      QTreeWidgetItem* sub=findItem(r,"SUB"); quint32 subCl=sub->data(0,Qt::UserRole+3).toUInt(); dropOn(w.dosTree(),{"/tmp/drop/proj/prog.bas"},sub);
      Dir dummy; QVector<Dos::DEntry> L; QString e; w.dos().listDir(subCl,&L,&e); bool have=false; for(auto&x:L) if(x.name=="PROG.BAS") have=true; CHECK(have);
      QTreeWidgetItem* h=findItem(w.dosTree()->topLevelItem(0),"HELLO.TXT"); dropOn(w.dosTree(),{"/tmp/drop/proj/test.bas"},h);
      CHECK(findItem(w.dosTree()->topLevelItem(0),"TEST.BAS")); }
    // ---- DOS view
    { QTreeWidgetItem* h=findItem(w.dosTree()->topLevelItem(0),"HELLO.TXT"); w.dosTree()->clearSelection(); h->setSelected(true);
      QMetaObject::invokeMethod(&w,"viewSelected",Qt::DirectConnection); auto vs=w.findChildren<ViewerDialog*>(); CHECK(vs.size()==1);
      if(vs.size()==1){ CHECK(vs[0]->shownText()=="Hello from the host\n"); vs[0]->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
      QTreeWidgetItem* c=findItem(w.dosTree()->topLevelItem(0),"COMMAND.COM"); CHECK(c); }
    // ---- DOS properties dialog: rename + attributes
    { QTreeWidgetItem* it=findItem(w.dosTree()->topLevelItem(0),"LONG_FIL.DAT"); w.dosTree()->clearSelection(); it->setSelected(true); w.dosTree()->setCurrentItem(it);
      QTimer::singleShot(200,[&]{ QDialog* dlg=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dlg){printf("no dos dialog\n");return;}
          auto edits=dlg->findChildren<QLineEdit*>(); auto boxes=dlg->findChildren<QCheckBox*>(); edits[0]->setText("renamed.dat"); boxes[0]->setChecked(true); boxes[1]->setChecked(true); dlg->accept(); });
      QMetaObject::invokeMethod(&w,"editProperties",Qt::DirectConnection);
      auto* r=w.dosTree()->topLevelItem(0); QTreeWidgetItem* n=findItem(r,"RENAMED.DAT"); CHECK(n && findItem(r,"LONG_FIL.DAT")==nullptr); if(n) CHECK(n->text(1).contains('R') && n->text(1).contains('H')); }
    // ---- DOS new directory + delete dialog
    { QTimer::singleShot(200,[&]{ auto* d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); if(d){ d->setTextValue("NEWDIR"); d->accept(); } else printf("no input dialog\n"); });
      w.dosTree()->setCurrentItem(w.dosTree()->topLevelItem(0));
      QMetaObject::invokeMethod(&w,"newDirectory",Qt::DirectConnection); CHECK(findItem(w.dosTree()->topLevelItem(0),"NEWDIR")); }
    { quint32 f0=w.dos().freeClusters(); QTreeWidgetItem* s=findItem(w.dosTree()->topLevelItem(0),"SUB"); w.dosTree()->clearSelection(); s->setSelected(true);
      QTimer::singleShot(200,[&]{ auto* mb=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if(mb){ printf("  dos delete prompt: %s\n",qPrintable(mb->text().replace('\n',' '))); mb->button(QMessageBox::Yes)->click(); } else printf("no msgbox\n"); });
      QMetaObject::invokeMethod(&w,"deleteSelected",Qt::DirectConnection);
      CHECK(w.dos().freeClusters()>f0); CHECK(findItem(w.dosTree()->topLevelItem(0),"SUB")==nullptr); }
    CHECK(w.dos().check().ok());
    w.grab().save("/tmp/gui_dos.png");
    CHECK(w.save());
    { Image i2; QString e3; CHECK(i2.open(argv[1],&e3)); CHECK(i2.openReport().ok());
      Dir rt,dd2; i2.readDir(RootSector,&rt,&e3); i2.readDir(rt.entries[rt.indexOf("DOS")].sin,&dd2,&e3); Entry dc=dd2.entries[dd2.indexOf("DRIVE_C")];
      Dos::Volume v; CHECK(v.attach(&i2,dc.sin,dc.length,&e3)); Report r=v.check(); CHECK(r.ok()); for(auto&x:r.errors) printf("ERR %s\n",qPrintable(x)); printf("  %s\n",qPrintable(r.info.join(" ")));
      QVector<Dos::DEntry> L; v.listDir(0,&L,&e3); bool a=false,b=false,c=false,d=false; for(auto&x:L){ if(x.name=="HELLO.TXT"){ QByteArray t; v.readFile(x,&t,&e3); a=(t=="Hello from the host\n"); } if(x.name=="RENAMED.DAT"){ b=(x.attr&Dos::AttrR)&&(x.attr&Dos::AttrH); } if(x.name=="TEST.BAS") c=true; if(x.name=="NEWDIR"&&x.isDir()) d=true; if(x.name=="SUB") printf("SUB still there!\n"); }
      CHECK(a&&b&&c&&d); }

    // ======================= rename (files and volumes, both tabs) =======================
    auto answer=[&](const QString& text){ QTimer::singleShot(200,[text]{ auto* d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); if(d){ printf("  rename prompt: %s | initial='%s'\n",qPrintable(d->labelText()),qPrintable(d->textValue())); d->setTextValue(text); d->accept(); } else printf("no input dialog\n"); }); };
    w.tabs()->setCurrentIndex(0);
    { QTreeWidgetItem* it=findItem(w.tree()->topLevelItem(0),"Hello2"); CHECK(it); w.tree()->clearSelection(); it->setSelected(true); w.tree()->setCurrentItem(it);
      answer("Renamed1"); QMetaObject::invokeMethod(&w,"renameSelected",Qt::DirectConnection);
      CHECK(findItem(w.tree()->topLevelItem(0),"Renamed1")); CHECK(!findItem(w.tree()->topLevelItem(0),"Hello2")); }
    { QTreeWidgetItem* rt=w.tree()->topLevelItem(0); w.tree()->clearSelection(); rt->setSelected(true);          // rename with the root selected = rename the disc
      answer("MyDisc"); QMetaObject::invokeMethod(&w,"renameSelected",Qt::DirectConnection);
      CHECK(w.tree()->topLevelItem(0)->text(0).contains("MyDisc")); }
    w.tabs()->setCurrentIndex(1);
    { QTreeWidgetItem* it=findItem(w.dosTree()->topLevelItem(0),"HELLO.TXT"); CHECK(it); w.dosTree()->clearSelection(); it->setSelected(true); w.dosTree()->setCurrentItem(it);
      answer("greet.txt"); QMetaObject::invokeMethod(&w,"renameSelected",Qt::DirectConnection);
      CHECK(findItem(w.dosTree()->topLevelItem(0),"GREET.TXT")); CHECK(!findItem(w.dosTree()->topLevelItem(0),"HELLO.TXT")); }
    { w.dosTree()->clearSelection(); answer("NEWVOL"); QMetaObject::invokeMethod(&w,"renameVolume",Qt::DirectConnection);
      CHECK(w.dos().label()=="NEWVOL"); CHECK(w.dosTree()->topLevelItem(0)->text(0).contains("NEWVOL")); }
    CHECK(w.save());
    { Image i2; QString e3; CHECK(i2.open(argv[1],&e3)); CHECK(i2.openReport().ok());
      Dir rt,dd2; i2.readDir(RootSector,&rt,&e3); CHECK(rt.title()=="MyDisc"); CHECK(rt.indexOf("Renamed1")>=0 && rt.indexOf("Hello2")<0);
      i2.readDir(rt.entries[rt.indexOf("DOS")].sin,&dd2,&e3); Entry dc=dd2.entries[dd2.indexOf("DRIVE_C")];
      Dos::Volume v; CHECK(v.attach(&i2,dc.sin,dc.length,&e3)); CHECK(v.label()=="NEWVOL"); CHECK(v.check().ok());
      QVector<Dos::DEntry> L; v.listDir(0,&L,&e3); bool g=false,h=false; for(auto&x:L){ if(x.name=="GREET.TXT") g=true; if(x.name=="HELLO.TXT") h=true; } CHECK(g&&!h); }
    w.grab().save("/tmp/gui_renamed.png");
    printf("\n%s (%d failures)\n",fails?"FAILED":"ALL OK",fails);
    return fails?1:0;
}
