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
#include "hostpolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <diskwrangler/fs/dfs/dfs.h>
#include <diskwrangler/fs/dfs/inf.h>
#include <diskwrangler/xfer/hostname.h>

// --- .inf sidecar files:  NAME LOAD EXEC [LENGTH] [ATTR|L|Locked]
bool readInf(const QString &hostPath, InfData *out) {
    dw::dfs::Inf i;
    if (!dw::dfs::readInf(hostPath, &i)) return false;
    InfData d;
    d.name = i.name;
    d.dir = i.dir;
    QFile f(dw::dfs::infPathFor(hostPath));
    if (i.dir.isNull() && f.open(QIODevice::ReadOnly)) {
        const QString line = QString::fromLatin1(f.readLine()).trimmed();
        const bool quoted = line.startsWith('"') && line.indexOf('"', 1) > 0;
        const QString whole = quoted ? line.mid(1, line.indexOf('"', 1) - 1) : line.section(QRegularExpression("\\s"), 0, 0);
        QString host = QFileInfo(hostPath).fileName();
        host.remove(QRegularExpression(" \\(\\d+\\)$"));
        if (whole.contains('.') && (quoted || host == whole)) d.name = whole;
    }
    d.load = i.load;
    d.exec = i.exec;
    if (i.hasAccess)
        d.flags = quint8(((i.access & dw::dfs::InfR) ? Adfs::FlagR : 0) | ((i.access & dw::dfs::InfW) ? Adfs::FlagW : 0) |
                         ((i.access & dw::dfs::InfE) ? Adfs::FlagE : 0) | ((i.access & dw::dfs::InfL) ? Adfs::FlagL : 0));
    *out = d;
    return true;
}

bool takeAtomHeaderErrors(QVector<dw::TransferItem> *items, QVector<dw::xfer::HostReadError> *errors) {
    bool ok = true;
    for (int i = 0; i < items->size(); ++i) {
        dw::TransferItem &it = (*items)[i];
        if (it.isDir) { ok = takeAtomHeaderErrors(&it.kids, errors) && ok; continue; }
        const QString why = it.attrs.value("atomhdr").value("error").toString();
        if (why.isEmpty()) continue;
        dw::xfer::HostReadError e;
        e.kind = dw::xfer::HostReadError::CannotRead;
        e.path = it.hostPath;
        e.reason = why;
        if (errors) errors->append(e);
        items->removeAt(i--);
        ok = false;
    }
    return ok;
}

void applyAtomHeaders(QVector<dw::TransferItem> *items, bool apply) {
    for (dw::TransferItem &it : *items) {
        if (it.isDir) { applyAtomHeaders(&it.kids, apply); continue; }
        if (!it.attrs.contains("atomhdr")) continue;
        const QJsonObject m = it.attrs.take("atomhdr");
        if (!apply || m.contains("error")) continue;
        it.data = it.data.mid(m["offset"].toInt(), int(m["length"].toDouble()));
        it.name = m["name"].toString();
        it.sourceFs = "adfs";
        QJsonObject a;
        a["load"] = RetroFiles::hex8(quint32(m["load"].toInt()));
        a["exec"] = RetroFiles::hex8(quint32(m["exec"].toInt()));
        a["access"] = "WR";
        it.attrs["acorn"] = a;
    }
}

bool isSidecar(const QString &p, bool useInf) {
    return useInf && p.endsWith(".inf", Qt::CaseInsensitive) && QFileInfo::exists(p.left(p.size() - 4));
}

// The header of an Atom file (acornHostRead): the mark applyAtomHeaders acts on.
static void markAtomHeader(const QString &p, bool atm, bool atomInPc, dw::TransferItem *it) {
    const bool isAtm = atm && p.endsWith(".atm", Qt::CaseInsensitive);
    if (!isAtm && !atomInPc) return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return;                  // readHost reports it
    const qint64 size = f.size();
    const QByteArray h = f.read(22);
    auto u16 = [&](int o) { return quint32(quint8(h[o])) | quint32(quint8(h[o + 1])) << 8; };
    QJsonObject m;
    if (isAtm) {
        if (h.size() < 22 || qint64(u16(20)) > size - 22) return;   // not one: a plain file
        QByteArray nm = h.left(16);
        const int nul = nm.indexOf('\0');
        if (nul >= 0) nm.truncate(nul);
        QString name = QString::fromLatin1(nm).trimmed();
        if (name.isEmpty()) name = it->name.left(it->name.size() - 4);
        m["offset"] = 22;
        m["length"] = qint64(u16(20));
        m["name"] = name;
        m["load"] = qint64(u16(16));
        m["exec"] = qint64(u16(18));
    } else {
        const quint32 len = h.size() < 16 ? 0 : u16(4) | u16(6) << 16;
        if (h.size() < 16 || qint64(len) > size - 16) {
            m["error"] = h.size() < 16 ? QString("not an AtomInPC file: shorter than its 16-byte header")
                                       : QString("not an AtomInPC file: its header says %1 bytes, but %2 follow").arg(len).arg(size - 16);
        } else {
            m["offset"] = 16;
            m["length"] = len ? qint64(len) : size - 16;
            m["name"] = it->name;
            m["load"] = qint64(u16(0));
            m["exec"] = qint64(u16(2));
        }
    }
    it->attrs["atomhdr"] = m;
}

dw::xfer::HostReadOptions acornHostRead(bool useInf, bool atm, bool atomInPc) {
    dw::xfer::HostReadOptions o;
    o.fileHook = [useInf, atm, atomInPc](const QString &p, bool, dw::TransferItem *it) {
        if (isSidecar(p, useInf)) return false;
        InfData inf;
        if (!useInf || !readInf(p, &inf)) { markAtomHeader(p, atm, atomInPc, it); return true; }
        if (!inf.name.isEmpty()) it->name = inf.name;
        if (!inf.dir.isNull() && dw::dfs::validDir(inf.dir)) { it->name = QString(inf.dir) + "." + it->name; it->sourceFs = "dfs"; }
        else it->sourceFs = "adfs";
        QJsonObject a;
        a["load"] = RetroFiles::hex8(it->sourceFs == "dfs" ? dw::dfs::extendAddr(inf.load & 0x3FFFF) : inf.load);
        a["exec"] = RetroFiles::hex8(it->sourceFs == "dfs" ? dw::dfs::extendAddr(inf.exec & 0x3FFFF) : inf.exec);
        QString access;
        if (inf.flags & Adfs::FlagL) access += 'L';
        if (inf.flags & Adfs::FlagW) access += 'W';
        if (inf.flags & Adfs::FlagR) access += 'R';
        if (inf.flags & Adfs::FlagE) access += 'E';
        a["access"] = access;
        it->attrs["acorn"] = a;
        return true;
    };
    return o;
}

namespace RetroFiles {

QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

bool parseHex(const QJsonValue &v, quint32 *out) {
    bool ok = false;
    quint32 n = v.toString().toUInt(&ok, 16);
    if (ok) *out = n;
    return ok && v.isString();
}

QString AcornPolicy::hostName(const dw::TransferItem &it) const {
    if (!it.hostName.isEmpty()) return dw::hostSafeName(it.hostName);    // a tape's second GAME: "GAME (2)"
    const bool dfs = it.sourceFs == "dfs";
    return dw::hostSafeName(dfs && it.name.startsWith("$.") ? it.name.mid(2) : it.name);
}

void AcornPolicy::afterFile(const dw::TransferItem &it, const QString &absDir, const QString &relDir, const QString &host,
                            dw::xfer::UsedNames &used, QStringList *topLevel, QStringList *errors) const {
    const bool dfs = it.sourceFs == "dfs";
    const bool tape = it.sourceFs == "cfs";
    if (!m_inf || !(dfs || tape || it.sourceFs == "adfs")) return;
    if (tape && !it.attrs.contains("acorn")) return;     // a tape's unrecognised data: no file, so no .inf
    const QJsonObject acorn = it.attrs.value("acorn");
    const QString access = acorn["access"].toString();
    quint32 load = 0, exec = 0;
    parseHex(acorn["load"], &load);
    parseHex(acorn["exec"], &exec);
    QString line;
    if (dfs)
        line = dw::dfs::infLineDfs(it.name[0], it.name.mid(2), load, exec, quint32(it.data.size()), access.contains('L'));
    else {
        const quint8 attr = quint8((access.contains('R') ? dw::dfs::InfR : 0) | (access.contains('W') ? dw::dfs::InfW : 0) |
                                   (access.contains('E') ? dw::dfs::InfE : 0) | (access.contains('L') ? dw::dfs::InfL : 0));
        line = dw::dfs::infLineAdfs(it.name, load, exec, quint32(it.data.size()), attr);
        // a tape file's name may hold a space, which would end the name, or a '.', which a reader takes for ADFS's
        // levels: such a name is quoted, as the .inf format allows (readInf keeps a quoted name whole)
        if (tape && it.name.contains(QRegularExpression("[\\s.]")))
            line = QString::asprintf("\"%s\" %08X %08X %08X %02X\n", qPrintable(it.name), load, exec, unsigned(it.data.size()), unsigned(attr));
    }
    QString infName = used.unique(host + ".inf", false);
    QFile inf(absDir + "/" + infName);
    if (!inf.open(QIODevice::WriteOnly | QIODevice::Truncate) || inf.write(line.toLatin1()) < 0)
        errors->append(QString("%1: %2").arg(infName, inf.errorString()));
    else if (topLevel)
        topLevel->append(relDir.isEmpty() ? infName : relDir + "/" + infName);
}

} // namespace RetroFiles
