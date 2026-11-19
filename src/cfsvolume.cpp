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
#include "cfsvolume.h"

#include <QJsonObject>
#include <QSet>

using dw::fs::AttrSpec;
using dw::fs::Entry;
using dw::fs::NodeKind;
using dw::fs::Path;

namespace Acorn {


bool CfsNameRules::valid(const QString &name, NodeKind kind, QString *why) const {
    auto no = [&](const QString &w) { if (why) *why = w; return false; };
    if (kind != NodeKind::File) return no("a tape has no directories");
    if (name.isEmpty()) return no("a name needs at least one character");
    if (name.size() > 10) return no("a tape file's name is at most 10 characters");
    for (QChar c : name)
        if (c.unicode() < 0x20 || c.unicode() > 0xFF || c.unicode() == 0x7F) return no("a name can't have that character");
    return true;
}

QString CfsNameRules::fromHost(const QString &hostName, NodeKind) const {
    QString n;
    for (QChar c : hostName) {
        if (n.size() == 10) break;
        if (c.unicode() >= 0x20 && c.unicode() <= 0xFF && c.unicode() != 0x7F) n += c;
    }
    return n.isEmpty() ? QString("FILE") : n;
}

CfsVolume::CfsVolume(dw::tape::TapeFile *tape) : m_tape(tape) {
    QString e;
    reload(&e);
}

dw::fs::Caps CfsVolume::caps() const {
    dw::fs::Caps c;
    c.rename = true;
    c.ordered = true;
    return c;
}

QVector<AttrSpec> CfsVolume::fileAttrs() const {
    auto spec = [](const QString &key, const QString &label, AttrSpec::Type type, bool editable = false) {
        AttrSpec a;
        a.key = key;
        a.label = label;
        a.type = type;
        a.editable = editable;
        return a;
    };
    AttrSpec load = spec("load", "Load", AttrSpec::Hex, true);
    load.bits = 32;
    load.sample = "FFFFFFFF";
    load.retroFamily = "acorn";
    load.retroKey = "load";
    AttrSpec exec = load;
    exec.key = "exec";
    exec.label = "Exec";
    exec.retroKey = "exec";
    AttrSpec prot = spec("protected", "Protected", AttrSpec::Bool, true);
    prot.letter = "P";
    AttrSpec blocks = spec("blocks", "Blocks", AttrSpec::Int);
    blocks.sample = "000";
    AttrSpec pos = spec("position", "Position", AttrSpec::Int);
    pos.column = false;
    return {load, exec, prot, blocks, pos};
}

dw::fs::VolumeInfo CfsVolume::info() const {
    dw::fs::VolumeInfo v;
    for (const Cfs::Item &it : m_read.items)
        v.total += it.type == Cfs::Item::FileItem ? it.file.data.size() : it.bytes;
    return v;
}

// The chunks a "uef" namespace carries (unrecognised data from another tape), packed; empty if none or not valid.
QByteArray CfsVolume::rawChunks(const QMap<QString, QJsonObject> &families) {
    const QByteArray b = QByteArray::fromBase64(families.value("uef").value("chunks").toString().toLatin1());
    QVector<dw::tape::Chunk> c;
    return Cfs::unpackChunks(b, &c) && !c.isEmpty() ? b : QByteArray();
}

QString CfsVolume::unrecognisedLabel(const Cfs::Item &it) {
    return it.first == it.last ? QString("unrecognised data (chunk %1)").arg(it.first)
                               : QString("unrecognised data (chunks %1-%2)").arg(it.first).arg(it.last);
}

bool CfsVolume::reload(QString *) {
    m_read = Cfs::read(dw::tape::pieces(*m_tape));
    m_names.clear();
    QSet<QString> used;                                  // case-insensitive, as the MOS matches names
    for (const Cfs::Item &it : m_read.items) {
        QString base = it.type == Cfs::Item::FileItem ? Cfs::nameText(it.file.name) : unrecognisedLabel(it);
        if (base.isEmpty()) base = "(no name)";
        QString n = base;
        for (int k = 2; used.contains(n.toUpper()); ++k) n = QString("%1 (%2)").arg(base).arg(k);
        used.insert(n.toUpper());
        m_names << n;
    }
    return true;
}

int CfsVolume::itemIndex(const QString &name) const { return m_names.indexOf(name); }

Entry CfsVolume::entry(int i) const {
    const Cfs::Item &it = m_read.items[i];
    Entry e;
    e.name = m_names[i];
    if (it.type == Cfs::Item::FileItem) {
        const Cfs::File &f = it.file;
        const QString real = Cfs::nameText(f.name);
        if (real != e.name) e.label = real;
        e.size = f.data.size();
        int pos = 0;
        for (int k = 0; k <= i; ++k) if (m_read.items[k].type == Cfs::Item::FileItem) ++pos;
        e.attrs = {{"load", f.load}, {"exec", f.exec}, {"protected", f.isProtected()}, {"blocks", f.blocks}, {"position", pos}};
        e.problem = f.problems.join(", ");
    } else {
        e.size = it.bytes;
        e.problem = "data the cassette filing system can't read";
    }
    return e;
}

bool CfsVolume::parsePath(const QString &text, Path *out, QString *err) const {
    if (text.isEmpty() || text == "/") { *out = Path(); return true; }
    if (text.startsWith('#') && text.size() > 1) {
        bool ok = false;
        const int n = text.mid(1).toInt(&ok);
        if (ok) {
            int pos = 0;
            for (int i = 0; i < m_read.items.size(); ++i)
                if (m_read.items[i].type == Cfs::Item::FileItem && ++pos == n) { *out = Path(QStringList{m_names[i]}); return true; }
            if (err) *err = QString("the tape has %1 file%2").arg(pos).arg(pos == 1 ? "" : "s");
            return false;
        }
    }
    *out = Path(QStringList{text});
    return true;
}

QString CfsVolume::displayPath(const Path &p) const { return p.isRoot() ? QString() : p.name(); }

bool CfsVolume::find(const Path &p, int *index, QString *err) const {
    if (p.parts.size() != 1) { if (err) *err = "a tape has no directories"; return false; }
    const QString n = p.parts[0];
    int i = m_names.indexOf(n);
    // a bare name that isn't an entry's own: the first file of that name, as the MOS would find it
    for (int k = 0; i < 0 && k < m_read.items.size(); ++k)
        if (m_read.items[k].type == Cfs::Item::FileItem && Cfs::nameText(m_read.items[k].file.name) == n) i = k;
    for (int k = 0; i < 0 && k < m_names.size(); ++k)
        if (m_names[k].compare(n, Qt::CaseInsensitive) == 0) i = k;
    if (i < 0) { if (err) *err = n + ": not found"; return false; }
    *index = i;
    return true;
}

bool CfsVolume::list(const Path &dir, QVector<Entry> *out, QString *err) const {
    if (!dir.isRoot()) { if (err) *err = "a tape has no directories"; return false; }
    out->clear();
    for (int i = 0; i < m_read.items.size(); ++i) out->append(entry(i));
    return true;
}

bool CfsVolume::stat(const Path &node, Entry *out, QString *err) const {
    if (node.isRoot()) {
        Entry e;
        e.kind = NodeKind::Dir;
        *out = e;
        return true;
    }
    int i;
    if (!find(node, &i, err)) return false;
    *out = entry(i);
    return true;
}

bool CfsVolume::read(const Path &file, QByteArray *out, QString *err) const {
    int i;
    if (!find(file, &i, err)) return false;
    const Cfs::Item &it = m_read.items[i];
    if (it.type == Cfs::Item::FileItem) { *out = it.file.data; return true; }
    // unrecognised data: the bytes of its chunks no block took, as they are on the tape
    QByteArray b;
    for (const dw::tape::Piece &p : dw::tape::pieces(*m_tape))
        if (p.type == dw::tape::Piece::Data && p.last >= it.first && p.first <= it.last)
            for (int k = 0; k < p.bytes.size(); ++k) {
                const int c = p.chunkOf(k);
                if (c >= it.first && c <= it.last) b.append(p.bytes[k]);
            }
    *out = b;
    return true;
}

bool CfsVolume::apply(const QVector<dw::tape::Chunk> &chunks, QString *err) {
    QString e;
    if (!m_tape->setChunks(chunks, &e)) { if (err) *err = e; return false; }
    return reload(err);
}

bool CfsVolume::changeable(int i, QString *err) const {
    const Cfs::Item &it = m_read.items[i];
    if (it.type != Cfs::Item::FileItem) { if (err) *err = "data the cassette filing system can't read is kept as it is"; return false; }
    if (!it.file.problems.isEmpty()) { if (err) *err = "a damaged file (" + it.file.problems.join(", ") + ") is kept as it is"; return false; }
    return true;
}

QVector<QByteArray> CfsVolume::reblock(int i, const QByteArray &name, quint32 load, quint32 exec, bool prot) const {
    const Cfs::File &f = m_read.items[i].file;
    QVector<QByteArray> out;
    for (int k = 0; k < f.blocks; ++k) {
        const Cfs::Block &b = m_read.blocks[f.firstBlock + k];
        const quint8 flag = quint8((b.flag & ~0x01) | (prot ? 0x01 : 0));
        out << Cfs::encodeBlock(name, load, exec, b.number, b.data, flag, b.spare);
    }
    return out;
}

bool CfsVolume::write(const Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs, const QDateTime &,
                      bool replace, QString *err) {
    if (!dir.isRoot()) { if (err) *err = "a tape has no directories"; return false; }
    if (attrs.contains("uefChunks")) {                  // another tape's unrecognised data, at the end as it was
        QVector<dw::tape::Chunk> raw, out;
        if (replace || !Cfs::unpackChunks(attrs.value("uefChunks").toByteArray(), &raw) || raw.isEmpty()) {
            if (err) *err = replace ? "unrecognised data is added, never put in place of a file" : "the unrecognised data's chunks are damaged";
            return false;
        }
        if (!Cfs::Edit(m_tape->chunks(), m_read).appendChunks(raw, &out, err)) return false;
        return apply(out, err);
    }
    // a new file placed under the unique name it will have (placeIncoming): its real name is the one it came with
    const QString realName = !replace && !m_placedAs.isEmpty() && name == m_placedAs ? m_placedReal : name;
    m_placedAs.clear();
    m_placedReal.clear();
    QString why;
    if (!m_rules.valid(realName, NodeKind::File, &why)) { if (err) *err = realName + ": " + why; return false; }
    const Cfs::Edit edit(m_tape->chunks(), m_read);
    QVector<dw::tape::Chunk> out;
    int i = -1;
    if (replace && !find(Path({name}), &i, err)) return false;
    QByteArray real = Cfs::nameBytes(realName);
    quint32 load = 0, exec = 0;
    bool prot = false;
    if (i >= 0) {                                        // what isn't given stays as it was
        if (!changeable(i, err)) return false;
        const Cfs::File &f = m_read.items[i].file;
        real = f.name;
        load = f.load;
        exec = f.exec;
        prot = f.isProtected();
    }
    load = attrs.contains("load") ? attrs.value("load").toUInt() : load;
    exec = attrs.contains("exec") ? attrs.value("exec").toUInt() : exec;
    prot = attrs.contains("protected") ? attrs.value("protected").toBool() : prot;
    const QVector<QByteArray> blocks = Cfs::encodeFile(real, load, exec, data, prot);
    if (!(i >= 0 ? edit.replace(i, blocks, &out, err) : edit.append(blocks, &out, err))) return false;
    return apply(out, err);
}

bool CfsVolume::remove(const Path &node, QString *err) {
    int i;
    if (!find(node, &i, err) || !changeable(i, err)) return false;
    QVector<dw::tape::Chunk> out;
    if (!Cfs::Edit(m_tape->chunks(), m_read).remove(i, &out, err)) return false;
    return apply(out, err);
}

bool CfsVolume::rename(const Path &from, const Path &to, QString *err) {
    int i;
    if (!find(from, &i, err) || !changeable(i, err)) return false;
    if (to.parts.size() != 1) { if (err) *err = "a tape has no directories"; return false; }
    QString why;
    if (!m_rules.valid(to.name(), NodeKind::File, &why)) { if (err) *err = to.name() + ": " + why; return false; }
    const Cfs::File &f = m_read.items[i].file;
    QVector<dw::tape::Chunk> out;
    if (!Cfs::Edit(m_tape->chunks(), m_read).replace(i, reblock(i, Cfs::nameBytes(to.name()), f.load, f.exec, f.isProtected()), &out, err))
        return false;
    return apply(out, err);
}

bool CfsVolume::setAttrs(const Path &node, const dw::fs::AttrValues &a, QString *err) {
    int i;
    if (!find(node, &i, err) || !changeable(i, err)) return false;
    for (auto k = a.begin(); k != a.end(); ++k)
        if (k.key() != "load" && k.key() != "exec" && k.key() != "protected") {
            if (err) *err = QString("%1 can't be changed").arg(k.key());
            return false;
        }
    const Cfs::File &f = m_read.items[i].file;
    const quint32 load = a.contains("load") ? a.value("load").toUInt() : f.load;
    const quint32 exec = a.contains("exec") ? a.value("exec").toUInt() : f.exec;
    const bool prot = a.contains("protected") ? a.value("protected").toBool() : f.isProtected();
    QVector<dw::tape::Chunk> out;
    if (!Cfs::Edit(m_tape->chunks(), m_read).replace(i, reblock(i, f.name, load, exec, prot), &out, err)) return false;
    return apply(out, err);
}

bool CfsVolume::move(const QVector<Path> &nodes, const Path &before, QString *err) {
    QVector<int> items;
    for (const Path &p : nodes) {
        int i;
        if (!find(p, &i, err)) return false;
        items << i;
    }
    int at = m_read.items.size();
    if (!before.isRoot() && !find(before, &at, err)) return false;
    QVector<dw::tape::Chunk> out;
    if (!Cfs::Edit(m_tape->chunks(), m_read).move(items, at, &out, err)) return false;
    if (out == m_tape->chunks()) return true;            // where they were already
    return apply(out, err);
}

dw::fs::Report CfsVolume::check() const {
    dw::fs::Report r;
    for (int i = 0; i < m_read.items.size(); ++i) {
        const Cfs::Item &it = m_read.items[i];
        if (it.type == Cfs::Item::FileItem) {
            for (const QString &p : it.file.problems) r.warnings << QString("%1: %2").arg(m_names[i], p);
        } else {
            r.info << QString("%1: %2 bytes the cassette filing system can't read").arg(m_names[i]).arg(it.bytes);
        }
    }
    r.info << QString("%1 files, %2 blocks").arg(m_read.files()).arg(m_read.blocks.size());
    return r;
}

dw::fs::AttrValues CfsVolume::fromRetro(const QMap<QString, QJsonObject> &families, NodeKind) const {
    dw::fs::AttrValues v;
    if (const QByteArray c = rawChunks(families); !c.isEmpty()) {
        v["uefChunks"] = c;
        return v;
    }
    const QJsonObject a = families.value("acorn");
    bool ok = false;
    const quint32 load = a["load"].toString().toUInt(&ok, 16);
    if (ok) v["load"] = load;
    const quint32 exec = a["exec"].toString().toUInt(&ok, 16);
    if (ok) v["exec"] = exec;
    if (a.contains("access")) {
        const QString access = a["access"].toString();
        v["protected"] = access.contains('E') && !access.contains('R');
    }
    return v;
}

dw::fs::Placement CfsVolume::placeIncoming(const dw::TransferItem &item, const Path &targetDir) const {
    dw::fs::Placement p;
    p.dir = targetDir;
    if (!item.isDir && !rawChunks(item.attrs).isEmpty()) {
        // another tape's unrecognised data or damaged file: added as it is, under a name no entry can have (what
        // it is called comes from its own chunks)
        p.name = item.attrs.contains("acorn") ? item.name + " (damaged)" : QString("unrecognised data");
        return p;
    }
    p.name = incomingName(item, &p.warning);
    if (item.isDir || m_sameName == SameName::Replace) return p;
    int i;
    if (!find(Path({p.name}), &i, nullptr)) return p;   // a name not on the tape yet
    if (m_sameName == SameName::AddUnlessIdentical && (i = identicalTo(item)) >= 0) {
        p.name = m_names[i];                             // the same file: a clash with it
        return p;
    }
    // another file of a name already on the tape: added, under the unique name it will have there
    m_placedReal = p.name;
    m_placedAs = p.name = nameWhenAdded(p.name);
    return p;
}

// The name an item arrives under: a DFS file in $ without its "$.", then the name rules.
QString CfsVolume::incomingName(const dw::TransferItem &item, QString *warning) const {
    QString n = item.name;
    if (item.sourceFs == "dfs" && n.startsWith("$.")) n = n.mid(2);
    const QString name = m_rules.fromHost(n, item.isDir ? NodeKind::Dir : NodeKind::File);
    if (warning && name != n) *warning = QString("%1 is called %2 on the tape").arg(n, name);
    return name;
}

int CfsVolume::identicalTo(const dw::TransferItem &item) const {
    if (item.isDir || !rawChunks(item.attrs).isEmpty()) return -1;
    const QString name = incomingName(item, nullptr);
    const dw::fs::AttrValues a = fromRetro(item.attrs, NodeKind::File);
    for (int i = 0; i < m_read.items.size(); ++i) {
        const Cfs::Item &it = m_read.items[i];
        if (it.type != Cfs::Item::FileItem || !it.file.problems.isEmpty()) continue;
        const Cfs::File &f = it.file;
        if (Cfs::nameText(f.name) != name || f.data != item.data) continue;
        if (a.contains("load") && a.value("load").toUInt() != f.load) continue;
        if (a.contains("exec") && a.value("exec").toUInt() != f.exec) continue;
        return i;
    }
    return -1;
}

// As reload() names an item added at the end: the first of NAME, "NAME (2)", ... no entry has (in any case), and
// (so that the Importer doesn't take it for a file there) no file's real name.
QString CfsVolume::nameWhenAdded(const QString &realName) const {
    QSet<QString> used;
    for (const QString &n : m_names) used.insert(n.toUpper());
    QString n = realName;
    int i;
    for (int k = 2; used.contains(n.toUpper()) || find(Path({n}), &i, nullptr); ++k) n = QString("%1 (%2)").arg(realName).arg(k);
    return n;
}

QMap<QString, QJsonObject> CfsVolume::toRetro(const Entry &e) const {
    QMap<QString, QJsonObject> m;
    if (e.isDir()) return m;
    // unrecognised data and a damaged file take their chunks along, for another tape to keep them as they are
    const int i = itemIndex(e.name);
    const bool asItIs = i >= 0 && (m_read.items[i].type == Cfs::Item::Unrecognised || !m_read.items[i].file.problems.isEmpty());
    if (asItIs && m_tape->kind() == dw::tape::Kind::Uef) {
        const QVector<dw::tape::Chunk> c = Cfs::Edit(m_tape->chunks(), m_read).itemChunks(i);
        if (!c.isEmpty()) {
            QJsonObject u;
            u["chunks"] = QString::fromLatin1(Cfs::packChunks(c).toBase64());
            m["uef"] = u;
        }
    }
    if (!e.attrs.contains("load")) return m;
    QJsonObject a;
    a["load"] = QString("%1").arg(e.attrs.value("load").toUInt(), 8, 16, QChar('0')).toUpper();
    a["exec"] = QString("%1").arg(e.attrs.value("exec").toUInt(), 8, 16, QChar('0')).toUpper();
    a["access"] = e.attrs.value("protected").toBool() ? "E" : "WR";
    m["acorn"] = a;
    return m;
}

} // namespace Acorn
