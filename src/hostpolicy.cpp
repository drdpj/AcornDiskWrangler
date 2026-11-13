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
    d.load = i.load;
    d.exec = i.exec;
    if (i.hasAccess)
        d.flags = quint8(((i.access & dw::dfs::InfR) ? Adfs::FlagR : 0) | ((i.access & dw::dfs::InfW) ? Adfs::FlagW : 0) |
                         ((i.access & dw::dfs::InfE) ? Adfs::FlagE : 0) | ((i.access & dw::dfs::InfL) ? Adfs::FlagL : 0));
    *out = d;
    return true;
}

bool isSidecar(const QString &p, bool useInf) {
    return useInf && p.endsWith(".inf", Qt::CaseInsensitive) && QFileInfo::exists(p.left(p.size() - 4));
}

dw::xfer::HostReadOptions acornHostRead(bool useInf) {
    dw::xfer::HostReadOptions o;
    o.fileHook = [useInf](const QString &p, bool, dw::TransferItem *it) {
        if (isSidecar(p, useInf)) return false;
        if (!useInf) return true;
        InfData inf;
        if (!readInf(p, &inf)) return true;
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
    const bool dfs = it.sourceFs == "dfs";
    return dw::hostSafeName(dfs && it.name.startsWith("$.") ? it.name.mid(2) : it.name);
}

void AcornPolicy::afterFile(const dw::TransferItem &it, const QString &absDir, const QString &relDir, const QString &host,
                            dw::xfer::UsedNames &used, QStringList *topLevel, QStringList *errors) const {
    const bool dfs = it.sourceFs == "dfs";
    if (!m_inf || !(dfs || it.sourceFs == "adfs")) return;
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
    }
    QString infName = used.unique(host + ".inf", false);
    QFile inf(absDir + "/" + infName);
    if (!inf.open(QIODevice::WriteOnly | QIODevice::Truncate) || inf.write(line.toLatin1()) < 0)
        errors->append(QString("%1: %2").arg(infName, inf.errorString()));
    else if (topLevel)
        topLevel->append(relDir.isEmpty() ? infName : relDir + "/" + infName);
}

} // namespace RetroFiles
