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
#include "adfsvolume.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <diskwrangler/device/sideorder.h>

#include <diskwrangler/fs/dfs/dfsvolume.h>
#include <diskwrangler/fs/fat/fatvolume.h>

using dw::fs::AttrSpec;
using dw::fs::AttrValues;
using dw::fs::NodeKind;
using dw::fs::Path;

namespace Adfs {

static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

bool AdfsNameRules::valid(const QString &name, NodeKind kind, QString *why) const {
    if (kind == NodeKind::Namespace) { if (why) *why = "ADFS has no namespaces"; return false; }
    return isValidName(name, why);
}

dw::fs::Caps AdfsVolume::caps() const {
    dw::fs::Caps c;
    c.dirs = true;
    c.title = true;
    c.dates = true;                      // a stamped load/exec address carries one
    c.maxEntriesPerDir = MaxDirEntries;
    return c;
}

QVector<AttrSpec> AdfsVolume::fileAttrs() const {
    QVector<AttrSpec> v;
    for (const auto &f : {std::make_pair("read", "R"), std::make_pair("write", "W"), std::make_pair("locked", "L"),
                          std::make_pair("execute", "E")}) {
        AttrSpec a;
        a.key = f.first;
        a.label = f.second;
        a.type = AttrSpec::Bool;
        a.dirs = a.key == "locked";
        a.sample = "L";
        a.letter = f.second;                     // the command line's attribute letters, in attrLetterOrder()
        a.retroFamily = "acorn";
        a.retroKey = "access";
        v << a;
    }
    AttrSpec load;
    load.key = "load"; load.label = "Load"; load.type = AttrSpec::Hex; load.sample = "FFFF1900";
    load.retroFamily = "acorn"; load.retroKey = "load";
    AttrSpec exec = load;
    exec.key = "exec"; exec.label = "Exec"; exec.retroKey = "exec";
    AttrSpec type;
    type.key = "type"; type.label = "Type"; type.type = AttrSpec::Hex; type.bits = 12; type.editable = false; type.sample = "FFF";
    return v << load << exec << type;
}

// Acorn's access string reads L W R E (the order of the letters in a catalogue listing); the specs keep their own order.
QStringList AdfsVolume::attrLetterOrder() const { return {"locked", "write", "read", "execute"}; }

QVector<AttrSpec> AdfsVolume::volumeAttrs() const {
    AttrSpec title;
    title.key = "title"; title.label = "Title"; title.type = AttrSpec::Text; title.sample = "MMMMMMMMMM";
    AttrSpec boot;
    boot.key = "boot"; boot.label = "Boot option"; boot.type = AttrSpec::Choice;
    for (int i = 0; i < 4; ++i) boot.choices << bootOptionName(i);
    return {title, boot};
}

AttrValues AdfsVolume::volumeValues() const {
    Dir root;
    QString err;
    const QString t = m_img->readDir(RootSector, &root, &err) ? root.title() : QString();
    return {{"title", t}, {"boot", m_img->bootOption()}};
}

dw::fs::VolumeInfo AdfsVolume::info() const {
    dw::fs::VolumeInfo i;
    i.title = volumeValues().value("title").toString();
    i.total = qint64(m_img->totalSectors()) * SectorSize;
    i.free = qint64(m_img->freeSectors()) * SectorSize;
    i.largestFree = qint64(m_img->largestFreeRun()) * SectorSize;
    return i;
}

dw::fs::Entry AdfsVolume::entry(const Entry &e) {
    dw::fs::Entry x;
    x.name = e.name();
    x.kind = e.isDir() ? NodeKind::Dir : NodeKind::File;
    x.size = e.isDir() ? 0 : e.length;
    x.attrs = {{"read", e.flag(Entry::R)}, {"write", e.flag(Entry::W)}, {"locked", e.flag(Entry::L)}, {"execute", e.flag(Entry::E)}};
    if (!e.isDir()) {
        x.attrs["load"] = e.load;
        x.attrs["exec"] = e.exec;
        if (e.hasStamp()) { x.attrs["type"] = e.fileType(); x.modified = e.stamp(); }
    }
    return x;
}

bool AdfsVolume::dirSector(const Path &dir, quint32 *sector, QString *err) const {
    quint32 s = RootSector;
    for (const QString &part : dir.parts) {
        Dir d;
        if (!m_img->readDir(s, &d, err)) return false;
        const int i = d.indexOf(part);
        if (i < 0 || !d.entries[i].isDir()) { *err = "no such directory: " + dir.toString('.'); return false; }
        s = d.entries[i].sin;
    }
    *sector = s;
    return true;
}

bool AdfsVolume::find(const Path &node, quint32 *parent, Entry *e, QString *err) const {
    if (node.isRoot()) { *err = "$ itself"; return false; }
    Dir d;
    if (!dirSector(node.parent(), parent, err) || !m_img->readDir(*parent, &d, err)) return false;
    const int i = d.indexOf(node.name());
    if (i < 0) { *err = "not found: " + node.toString('.'); return false; }
    *e = d.entries[i];
    return true;
}

bool AdfsVolume::list(const Path &dir, QVector<dw::fs::Entry> *out, QString *err) const {
    quint32 s;
    Dir d;
    if (!dirSector(dir, &s, err) || !m_img->readDir(s, &d, err)) return false;
    out->clear();
    for (const Entry &e : d.entries) out->append(entry(e));
    return true;
}

bool AdfsVolume::read(const Path &file, QByteArray *out, QString *err) const {
    quint32 p;
    Entry e;
    if (!find(file, &p, &e, err)) return false;
    if (e.isDir()) { *err = file.toString('.') + " is a directory"; return false; }
    return m_img->readFile(e, out, err);
}

static quint8 flagsOf(const AttrValues &a, quint8 base) {
    auto bit = [&](const char *k, quint8 f) {
        if (a.contains(k)) base = a.value(k).toBool() ? quint8(base | f) : quint8(base & ~f);
    };
    bit("read", FlagR);
    bit("write", FlagW);
    bit("locked", FlagL);
    bit("execute", FlagE);
    return base;
}

static quint8 flagsOf(const Entry &e) {
    return quint8((e.flag(Entry::R) ? FlagR : 0) | (e.flag(Entry::W) ? FlagW : 0) | (e.flag(Entry::L) ? FlagL : 0) |
                  (e.flag(Entry::E) ? FlagE : 0));
}

bool AdfsVolume::write(const Path &dir, const QString &name, const QByteArray &data, const AttrValues &a,
                       const QDateTime &, bool replace, QString *err) {
    quint32 s;
    if (!dirSector(dir, &s, err)) return false;
    return m_img->addFile(s, name, data, a.value("load").toUInt(), a.value("exec").toUInt(), flagsOf(a, FlagR | FlagW),
                          replace, err);
}

bool AdfsVolume::mkdir(const Path &dir, const QString &name, QString *err) {
    quint32 s;
    return dirSector(dir, &s, err) && m_img->makeDir(s, name, err);
}

bool AdfsVolume::remove(const Path &node, QString *err) {
    quint32 p;
    Entry e;
    return find(node, &p, &e, err) && m_img->deleteEntry(p, e.name(), err);
}

bool AdfsVolume::rename(const Path &from, const Path &to, QString *err) {
    if (from.parent() != to.parent()) { *err = "moving between directories is not supported yet"; return false; }
    quint32 p;
    Entry e;
    if (!find(from, &p, &e, err)) return false;
    Entry u = e;
    u.setName(to.name());
    return m_img->replaceEntry(p, e.name(), u, err);
}

bool AdfsVolume::setAttrs(const Path &node, const AttrValues &a, QString *err) {
    quint32 p;
    Entry e;
    if (!find(node, &p, &e, err)) return false;
    Entry u = e;
    const quint8 f = flagsOf(a, flagsOf(e));
    u.setFlag(Entry::R, f & FlagR);
    u.setFlag(Entry::W, f & FlagW);
    u.setFlag(Entry::L, f & FlagL);
    u.setFlag(Entry::E, f & FlagE);
    if (!e.isDir()) {
        u.load = a.value("load", e.load).toUInt();
        u.exec = a.value("exec", e.exec).toUInt();
    }
    return m_img->replaceEntry(p, e.name(), u, err);
}

bool AdfsVolume::setVolumeAttrs(const AttrValues &a, QString *err) {
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (it.key() == "title") { if (!m_img->setDiscTitle(it.value().toString(), err)) return false; }
        else if (it.key() == "boot") { if (!m_img->setBootOption(it.value().toInt(), err)) return false; }
        else { *err = "ADFS discs have no " + it.key(); return false; }
    }
    return true;
}

QMap<QString, QJsonObject> AdfsVolume::toRetro(const dw::fs::Entry &e) const {
    QJsonObject a;
    if (!e.isDir()) {
        a["load"] = hex8(e.attrs.value("load").toUInt());
        a["exec"] = hex8(e.attrs.value("exec").toUInt());
    }
    QString s;                           // as Acorn Disk Wrangler's drag-out writes it: L W R E
    if (e.attrs.value("locked").toBool()) s += 'L';
    if (e.attrs.value("write").toBool()) s += 'W';
    if (e.attrs.value("read").toBool()) s += 'R';
    if (e.attrs.value("execute").toBool()) s += 'E';
    a["access"] = s;
    return {{"acorn", a}};
}

AttrValues AdfsVolume::fromRetro(const QMap<QString, QJsonObject> &families, NodeKind kind) const {
    AttrValues v;
    const QJsonObject a = families.value("acorn");
    if (kind == NodeKind::File)
        for (const char *k : {"load", "exec"}) {
            bool ok = false;
            const quint32 n = a.value(k).toString().toUInt(&ok, 16);
            if (ok) v[k] = n;
        }
    if (a.contains("access")) {          // case-sensitive: lower case letters are the public access
        const QString s = a.value("access").toString();
        v["read"] = s.contains('R');
        v["write"] = s.contains('W');
        v["locked"] = s.contains('L');
        v["execute"] = s.contains('E');
    } else {
        v["read"] = v["write"] = true;
        bool ro = false;                 // any family's "readonly" (fat, cpm)
        for (auto f = families.begin(); f != families.end(); ++f) ro = ro || f.value().value("readonly").toBool();
        v["locked"] = ro;
    }
    return v;
}

dw::fs::Placement AdfsVolume::placeIncoming(const dw::TransferItem &item, const Path &targetDir) const {
    dw::fs::Placement p;
    p.dir = targetDir;
    if (item.sourceFs != "dfs" || item.isDir) {
        p.name = sanitizeName(item.name);
        return p;
    }
    QChar letter;
    QString name;
    dw::dfs::splitName(item.name, &letter, &name);
    const dw::dfs::LetterPlace lp =
        dw::dfs::placeLetter(letter, name, m_flatten, [](QChar ch) { return isValidNameChar(ch); });
    if (lp.how == dw::dfs::LetterPlace::Subdir) p.dir = targetDir.child(lp.subdir);
    p.name = sanitizeName(lp.name);
    p.warning = lp.warning;
    return p;
}

bool AdfsVolume::parsePath(const QString &text, Path *out, QString *err) const {
    QString t = text.trimmed();
    *out = Path();
    if (t.isEmpty() || t == "/" || t == "$") return true;
    if (t.startsWith("$.")) t = t.mid(2);
    else if (t.startsWith('$')) { *err = "a path starts $. or is just the name: " + text; return false; }
    const QStringList parts = t.split('.');
    for (const QString &p : parts)
        if (p.isEmpty()) { *err = "empty name in the path: " + text; return false; }
    out->parts = parts;
    return true;
}

QString AdfsVolume::displayPath(const Path &p) const { return p.isRoot() ? QString("$") : "$." + p.parts.join('.'); }

// ------------------------------------------------------------------ OpenedDisc

OpenedDisc::OpenedDisc() : m_dos(std::make_shared<dw::fat::Volume>()) {
    m_dfs[0] = std::make_shared<dw::dfs::Disc>();
    m_dfs[1] = std::make_shared<dw::dfs::Disc>();
    // The checks the main window ran before a save (step 5i: moved here, so the command line has them too). Image's
    // commit checks ADFS itself; a DOS Plus or DFS volume the changes touch vetoes the save with its own errors ...
    m_img.setExtraValidator([this]() -> QStringList {
        QStringList out;
        QVector<dw::fs::Report> per;
        m_disc.validateTouched(&per);
        for (int i = 0; i < m_disc.mounts().size() && i < per.size(); ++i) {
            const QString g = m_disc.mounts()[i].group;
            const QString p = g == "DOS" ? QString("DOS partition: ")
                            : g == "DFS" ? QString("DFS drive %1: ").arg(i == dfsMount(1) ? 2 : 0) : QString();
            if (!p.isEmpty()) for (const QString &e : per[i].errors) out << p + e;
        }
        return out;
    });
    // ... and the copy about to replace the file is checked the same way, its volumes read from the copy itself
    m_img.setReopenValidator([this](Image &saved) -> QStringList {
        QStringList out;
        QString err;
        const int dm = dosMount();
        if (dm >= 0 && m_disc.mounts()[dm].touched()) {
            dw::fat::Volume v;
            bool ok;
            switch (m_info.kind) {
            case DiskInfo::AdfsDosHybrid: ok = v.attachFormat(&saved, m_dos->sin(), m_dos->length(), dw::fat::FmtAcorn640, &err); break;
            case DiskInfo::DosAcorn800: ok = v.attachFormat(&saved, m_dos->sin(), m_dos->length(), dw::fat::FmtAcorn800, &err); break;
            case DiskInfo::DosPc:
                ok = m_info.dosFromTable ? v.attachFormat(&saved, m_dos->sin(), m_dos->length(), m_info.dosFormat, &err)
                                         : v.attach(&saved, m_dos->sin(), m_dos->length(), &err);
                break;
            default: ok = v.attach(&saved, m_dos->sin(), m_dos->length(), &err);
            }
            if (!ok) out << "DOS partition: " + err;
            else for (const QString &e : v.check().errors) out << "DOS partition: " + e;
        }
        for (int s = 0; s < 2; ++s) {
            const int m = dfsMount(s);
            if (m < 0 || !m_disc.mounts()[m].touched()) continue;
            dw::dfs::Disc d;
            if (!d.attach(&saved, s, m_info.dfs, &err)) out << QString("DFS drive %1: %2").arg(s * 2).arg(err);
            else for (const QString &e : d.check().errors) out << QString("DFS drive %1: %2").arg(s * 2).arg(e);
        }
        return out;
    });
}

void OpenedDisc::close() {
    m_disc.clearMounts();
    m_disc.setContainer(nullptr);
    m_dos->detach();
    m_dfs[0]->detach();
    m_dfs[1]->detach();
    m_dosOk = m_dfsOk[0] = m_dfsOk[1] = false;
    m_info = DiskInfo();
    m_img.close();
}

int OpenedDisc::adfsMount() const {
    for (int i = 0; i < m_disc.mounts().size(); ++i) if (m_disc.mounts()[i].group == "ADFS") return i;
    return -1;
}

int OpenedDisc::dosMount() const {
    for (int i = 0; i < m_disc.mounts().size(); ++i) if (m_disc.mounts()[i].group == "DOS") return i;
    return -1;
}

int OpenedDisc::dfsMount(int side) const {
    const QString label = QString("drive %1").arg(side ? 2 : 0);
    for (int i = 0; i < m_disc.mounts().size(); ++i)
        if (m_disc.mounts()[i].group == "DFS" && m_disc.mounts()[i].label == label) return i;
    return -1;
}

// $.DOS.DRIVE_C, attached as the "DOS" mount (an ADFS disc's DOS Plus partition)
bool OpenedDisc::attachDriveC(QString *err) {
    Dir root, dosDir;
    const int i = m_img.readDir(RootSector, &root, err) ? root.indexOf("DOS") : -1;
    const int j = i >= 0 && root.entries[i].isDir() && m_img.readDir(root.entries[i].sin, &dosDir, err)
                      ? dosDir.indexOf("DRIVE_C") : -1;
    return j >= 0 && !dosDir.entries[j].isDir() && m_dos->attach(&m_img, dosDir.entries[j].sin, dosDir.entries[j].length, err);
}

bool OpenedDisc::open(const QString &path, const DiskInfo &info, QString *err) {
    close();
    m_info = info;
    DiskInfo::Kind kind = info.kind;
    if (kind == DiskInfo::Unknown && m_window) kind = DiskInfo::Adfs;      // opened as ADFS, so its problems are shown
    switch (kind) {
    case DiskInfo::Unknown: case DiskInfo::AdfsNewMap: case DiskInfo::AcornCpm: case DiskInfo::TorchCpn:
        *err = info.description.isEmpty() ? QString("not a disc this program opens") : info.description + ": not opened here";
        m_info = DiskInfo();
        return false;
    default: break;
    }
    const bool raw = kind == DiskInfo::Dfs || kind == DiskInfo::DosPc || kind == DiskInfo::DosAcorn800;
    if (!(raw ? m_img.openRaw(path, info.fullSize, err, info.sides)
              : m_img.open(path, err, kind == DiskInfo::AdfsDosHybrid ? info.fullSize : 0, info.sides))) {
        close();
        return false;
    }
    m_disc.setContainer(&m_img.container());
    const quint32 len = quint32(m_img.fullSize());
    auto mountDos = [&](bool ok) {
        if (!ok) { m_dos->detach(); return; }
        dw::fs::Disc::Mount m("DOS", std::make_shared<dw::fat::FsVolume>(m_dos), false, QString(), "DOS");
        m.touched = [this] { return m_img.pendingTouches(m_dos->sin(), sectorsFor(m_dos->length())); };
        m_disc.addMount(m);
    };
    QString e;
    switch (kind) {
    case DiskInfo::Adfs:
        m_disc.addMount({"ADFS", std::make_shared<AdfsVolume>(&m_img), !m_img.editable(),
                         m_img.editable() ? QString() : QString("read-only"), "ADFS"});
        mountDos(attachDriveC(&e));     // the DOS Plus partition, $.DOS.DRIVE_C
        break;
    case DiskInfo::AdfsDosHybrid:
        m_img.setAdfsLocked(true);
        m_disc.addMount({"ADFS", std::make_shared<AdfsVolume>(&m_img), true, "the ADFS side of a hybrid disc", "ADFS"});
        mountDos(m_dos->attachFormat(&m_img, 0, len, dw::fat::FmtAcorn640, &e));
        break;
    case DiskInfo::DosAcorn800:
        mountDos(m_dos->attachFormat(&m_img, 0, len, dw::fat::FmtAcorn800, &e));
        break;
    case DiskInfo::DosPc:
        mountDos(info.dosFromTable ? m_dos->attachFormat(&m_img, 0, len, info.dosFormat, &e) : m_dos->attach(&m_img, 0, len, &e));
        break;
    case DiskInfo::Dfs:
        for (int s = 0; s < (info.dfs.doubleSided ? 2 : 1); ++s) {
            if (!m_dfs[s]->attach(&m_img, s, info.dfs, &e)) {
                m_dfs[s]->detach();
                if (m_window) continue;              // the window shows the side that can be read
                *err = QString("drive %1: %2").arg(s * 2).arg(e);
                close();
                return false;
            }
            dw::fs::Disc::Mount m(QString("drive %1").arg(s * 2), std::make_shared<dw::dfs::Volume>(m_dfs[s]), false, QString(), "DFS");
            m.touched = [this] { return m_img.pendingTouches(0, quint32(m_img.fullSize() / SectorSize)); };
            m_disc.addMount(m);
        }
        break;
    default: break;
    }
    if (m_disc.mounts().isEmpty() && !m_window) { *err = "nothing on the disc could be read: " + e; close(); return false; }
    return true;
}

bool OpenedDisc::followDriveC() {
    if (!m_img.isOpen() || !m_img.isAdfs() || m_info.kind == DiskInfo::AdfsDosHybrid) return false;
    QString e;
    const bool have = attachDriveC(&e);           // read again, as the window always has
    const int m = dosMount();
    if (have == (m >= 0)) return false;
    if (have) {
        dw::fs::Disc::Mount d("DOS", std::make_shared<dw::fat::FsVolume>(m_dos), false, QString(), "DOS");
        d.touched = [this] { return m_img.pendingTouches(m_dos->sin(), sectorsFor(m_dos->length())); };
        m_disc.addMount(d);
    } else {
        m_dos->detach();
        m_disc.mounts().remove(m);
    }
    return true;
}

void OpenedDisc::reloadMounts() {
    QString re;
    for (const auto &m : m_disc.mounts())
        if (m.vol) m.vol->reload(&re);
}

bool OpenedDisc::save(QString *err, bool backup) {
    // Adfs::Image keeps its own saving sequence (the map checks, padding, the side-order conversion, the
    // changed-on-disc refusal, and the checks above); the command line checks every mount first
    if (!m_window) {
        const dw::fs::Report r = m_disc.validateAll();
        if (!r.ok()) { *err = "The disc failed its check, so NOTHING was written.\n\n" + r.errors.join("\n"); return false; }
    }
    Report post;
    const bool converting = m_img.convertPending();
    const bool ok = m_img.commit(err, backup, &post);
    if (converting && !m_img.convertPending()) m_info.sides = m_img.sideLayout();   // the file's side order changed
    reloadMounts();
    return ok;
}

bool OpenedDisc::saveAs(const QString &path, dw::device::Kind kind, QString *err) {
    if (kind == dw::device::Kind::None || kind == m_img.containerKind()) return m_img.moveToCopy(path, err) && save(err, false);
    if (!writeCopy(path, kind, QString(), true, err)) return false;
    DiskInfo unknown;
    const dw::fs::Verdict v = detectCandidates(path, -1, &unknown);
    if (!open(path, v.bestCandidate() ? diskInfo(*v.bestCandidate()) : unknown, err)) return false;
    m_dosOk = m_dos->attached() && m_dos->check().ok();
    for (int s = 0; s < 2; ++s) m_dfsOk[s] = m_dfs[s]->attached() && m_dfs[s]->check().ok();
    return true;
}

bool OpenedDisc::convertible(QString *why) const {
    dw::device::RawLayout l;
    QString w;
    if (!m_img.isOpen()) w = "no disc";
    else if (rawLayoutFor(m_info, &l, &w)) return true;
    if (why) *why = w;
    return false;
}

bool OpenedDisc::verifyCopy(const QString &path, DiskInfo::Kind want, int forceSeq, QString *err) {
    DiskInfo unknown;
    const dw::fs::Verdict v = detectCandidates(path, forceSeq, &unknown);
    if (v.action != dw::fs::Verdict::Open && v.action != dw::fs::Verdict::Ask) { *err = "the copy isn't recognised as the same kind of disc"; return false; }
    const DiskInfo info = diskInfo(*v.bestCandidate());
    if (info.kind != want) { *err = "the copy is read as " + info.description + ", not as the original kind of disc"; return false; }
    OpenedDisc d;
    if (!d.open(path, info, err)) return false;
    const dw::fs::Report r = d.disc().validateAll();
    if (!r.ok()) { *err = "the copy fails its check: " + r.errors.join("; "); return false; }
    return true;
}

static QString tempBeside(const QString &file) {
    const QFileInfo fi(file);
    return fi.absoluteDir().filePath(QString(".dwconv-%1-%2").arg(QUuid::createUuid().toString(QUuid::Id128).left(8), fi.fileName()));
}

// Moved here from acorndw's convert (src/cli/backend.cpp) in step 5j, so that the window's Save As makes the same bytes.
bool OpenedDisc::writeCopy(const QString &out, dw::device::Kind to, const QString &toSides, bool withStaged, QString *err,
                           CopyFailure *whyFail) {
    using dw::device::Kind;
    CopyFailure dummy;
    CopyFailure &fail = whyFail ? *whyFail : dummy;
    fail = CopyFailure::None;
    auto no = [&](CopyFailure f, const QString &e) { fail = f; *err = e; return false; };
    const DiskInfo &info = m_info;
    const Kind from = m_img.container().kind();
    if (!m_img.isOpen()) return no(CopyFailure::Failed, "no disc");
    if (to == Kind::None) to = from;
    if (to == Kind::Td0) return no(CopyFailure::Unsupported, "TeleDisk files can be read but not written");
    if (!toSides.isEmpty() && !info.sides.doubleSided()) return no(CopyFailure::Unsupported, "only a double-sided floppy has a side order");
    if (!toSides.isEmpty() && to != Kind::Raw) return no(CopyFailure::Unsupported, "a side order belongs to a raw image: the other containers record where every track is");
    const bool sequentialOut = toSides == "sequential";
    const int forceSeq = to == Kind::Raw && info.sides.doubleSided() ? (toSides.isEmpty() ? 0 : sequentialOut ? 1 : 0) : -1;
    dw::device::Container x = m_img.container();
    if (!withStaged) x.discardPending();
    QString why;
    if (to != Kind::Raw) {
        if (from == Kind::Raw) {                                     // give the raw bytes their tracks
            dw::device::RawLayout l;
            if (!rawLayoutFor(info, &l, &why)) return no(CopyFailure::Unsupported, why);
            x.setRawPadding(qint64(info.fullSize), char(0));
            if (!x.setRawLayout(l, err)) { fail = CopyFailure::Failed; return false; }
        }
        const DiskInfo::Kind want = info.kind;
        if (!x.saveAs(out, to, err, [&](dw::device::Container &w, QString *e) { return verifyCopy(w.path(), want, -1, e); })) {
            fail = CopyFailure::Save;
            return false;
        }
        return true;
    }
    // the stored bytes (interleaved), padded to the disc's full size, in the side order asked for
    const qint64 n = x.storedSize(&why);
    if (n <= 0) return no(CopyFailure::Unsupported, "this container can't be written as a raw image: " + why);
    QByteArray bytes;
    if (!x.readStored(0, n, &bytes, err)) { fail = CopyFailure::Failed; return false; }
    if (quint64(bytes.size()) < info.fullSize) bytes.append(QByteArray(int(info.fullSize - quint64(bytes.size())), char(0)));
    if (from == Kind::Raw && info.sides.sequential) {            // the raw file is sequential: the stored bytes are too
        if (!sequentialOut && !toSides.isEmpty()) bytes = dw::device::reorderSides(bytes, info.sides, false);
    } else if (info.sides.doubleSided() && sequentialOut) {
        bytes = dw::device::reorderSides(bytes, info.sides, true);
    }
    const QString tmp = tempBeside(out);
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        *err = QString("cannot write %1: %2").arg(out, f.errorString());
        QFile::remove(tmp);
        fail = CopyFailure::Save;
        return false;
    }
    f.close();
    if (!verifyCopy(tmp, info.kind, forceSeq, err)) { QFile::remove(tmp); fail = CopyFailure::Save; return false; }
    if (QFile::exists(out)) QFile::remove(out);
    if (!QFile::rename(tmp, out)) { QFile::remove(tmp); return no(CopyFailure::Save, "cannot write " + out); }
    return true;
}

void OpenedDisc::revert() {
    m_img.revert();
    reloadMounts();
}

QString OpenedDisc::readOnlyWhy(int mount) const {
    if (!m_img.isOpen()) return "no disc";
    if (!m_img.editable())
        return m_img.isAdfs() && !m_img.openReport().ok() ? "the free space map or directories are damaged (Tools > Check disc)"
               : m_img.containerKind() == dw::device::Kind::Td0 ? "TeleDisk images can only be read"
                                                                : "the image file can't be written";
    if (mount >= 0 && mount == dosMount()) return m_dosOk ? QString() : "the DOS volume has errors (Tools > Check disc)";
    for (int s = 0; s < 2; ++s)
        if (mount >= 0 && mount == dfsMount(s))
            return m_dfsOk[s] ? QString() : QString("drive %1's catalogue has errors (Tools > Check disc)").arg(s * 2);
    if (m_img.adfsLocked()) return "the ADFS side of a DOS Plus disc is kept read-only, so the DOS files stay safe";
    return QString();
}

} // namespace Adfs
