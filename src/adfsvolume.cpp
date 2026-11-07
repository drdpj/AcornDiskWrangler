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

// ------------------------------------------------------------------ OpenedDisc

bool OpenedDisc::open(const QString &path, const DiskInfo &info, QString *err) {
    m_info = info;
    switch (info.kind) {
    case DiskInfo::Unknown: case DiskInfo::AdfsNewMap: case DiskInfo::AcornCpm: case DiskInfo::TorchCpn:
        *err = info.description.isEmpty() ? QString("not a disc this program opens") : info.description + ": not opened here";
        return false;
    default: break;
    }
    const bool raw = info.kind == DiskInfo::Dfs || info.kind == DiskInfo::DosPc || info.kind == DiskInfo::DosAcorn800;
    if (!(raw ? m_img.openRaw(path, info.fullSize, err, info.sides)
              : m_img.open(path, err, info.kind == DiskInfo::AdfsDosHybrid ? info.fullSize : 0, info.sides)))
        return false;
    m_disc.setContainer(&m_img.container());
    const quint32 len = quint32(m_img.fullSize());
    auto mountDos = [&](bool ok) {
        if (ok) m_disc.addMount({"DOS", std::make_shared<dw::fat::FsVolume>(m_dos), false, QString(), "DOS"});
        else m_dos.reset();
    };
    QString e;
    m_dos = std::make_shared<dw::fat::Volume>();
    switch (info.kind) {
    case DiskInfo::Adfs: {
        m_disc.addMount({"ADFS", std::make_shared<AdfsVolume>(&m_img), !m_img.editable(),
                         m_img.editable() ? QString() : QString("read-only"), "ADFS"});
        Dir root, dosDir;                // the DOS Plus partition, $.DOS.DRIVE_C
        const int i = m_img.readDir(RootSector, &root, &e) ? root.indexOf("DOS") : -1;
        const int j = i >= 0 && root.entries[i].isDir() && m_img.readDir(root.entries[i].sin, &dosDir, &e)
                          ? dosDir.indexOf("DRIVE_C") : -1;
        mountDos(j >= 0 && !dosDir.entries[j].isDir() &&
                 m_dos->attach(&m_img, dosDir.entries[j].sin, dosDir.entries[j].length, &e));
        break;
    }
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
        m_dos.reset();
        for (int s = 0; s < (info.dfs.doubleSided ? 2 : 1); ++s) {
            m_dfs[s] = std::make_shared<dw::dfs::Disc>();
            if (!m_dfs[s]->attach(&m_img, s, info.dfs, &e)) { *err = QString("drive %1: %2").arg(s * 2).arg(e); return false; }
            m_disc.addMount({QString("drive %1").arg(s * 2), std::make_shared<dw::dfs::Volume>(m_dfs[s]), false, QString(), "DFS"});
        }
        break;
    default: break;
    }
    if (m_disc.mounts().isEmpty()) { *err = "nothing on the disc could be read: " + e; return false; }
    return true;
}

bool OpenedDisc::save(QString *err, bool backup) {
    // Adfs::Image keeps its own saving sequence (the map checks, padding, the side-order conversion, the
    // changed-on-disc refusal); every mount is checked first, as the main window does
    const dw::fs::Report r = m_disc.validateAll();
    if (!r.ok()) { *err = "The disc failed its check, so NOTHING was written.\n\n" + r.errors.join("\n"); return false; }
    Report post;
    const bool ok = m_img.commit(err, backup, &post);
    QString re;
    for (const auto &m : m_disc.mounts())
        if (m.vol) m.vol->reload(&re);
    return ok;
}

} // namespace Adfs
