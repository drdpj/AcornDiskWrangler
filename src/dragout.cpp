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
#include "dragout.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <diskwrangler/xfer/hostname.h>
#include <diskwrangler/xfer/retrofiles.h>

namespace RetroFiles {

const char *const MimeType = "application/x-retro-files+json";
static const char *const Generator = "Acorn Disk Wrangler";

QString filesystemName(int srcPane) {
    if (srcPane == PaneAdfs) return "adfs";
    if (srcPane == PaneDfs0 || srcPane == PaneDfs1) return "dfs";
    return "fat";
}

static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

// Access letters as in the Stardot .inf format (owner upper case; this program has no public bits to give).
static QString adfsAccess(quint8 flags) {
    QString s;
    if (flags & Adfs::FlagL) s += 'L';
    if (flags & Adfs::FlagW) s += 'W';
    if (flags & Adfs::FlagR) s += 'R';
    if (flags & Adfs::FlagE) s += 'E';
    return s;
}

static bool parseHex(const QJsonValue &v, quint32 *out) {
    bool ok = false;
    quint32 n = v.toString().toUInt(&ok, 16);
    if (ok) *out = n;
    return ok && v.isString();
}

// ------------------------------------------------------------------------------ XItem <-> dw::TransferItem

dw::TransferItem toTransfer(const XItem &x) {
    dw::TransferItem t;
    const bool dfs = x.src == PaneDfs0 || x.src == PaneDfs1;
    t.name = dfs ? QString(x.dfsDir) + "." + x.name : x.name;       // a DFS name always carries its directory: "$.BOOT", "A.BOOT"
    t.isDir = x.isDir;
    t.data = x.data;
    t.when = x.when;
    t.sourceFs = filesystemName(x.src);
    if (x.src == PaneAdfs) {
        QJsonObject a;
        if (!x.isDir) { a["load"] = hex8(x.load); a["exec"] = hex8(x.exec); }
        a["access"] = adfsAccess(x.adfsFlags);
        t.attrs["acorn"] = a;
    } else if (dfs) {
        QJsonObject a;
        a["load"] = hex8(extendDfsAddr(x.load));
        a["exec"] = hex8(extendDfsAddr(x.exec));
        a["access"] = x.locked ? "L" : "WR";
        t.attrs["acorn"] = a;
    } else {
        QJsonObject f;
        f["readonly"] = (x.dosAttr & Dos::AttrR) != 0;
        f["hidden"] = (x.dosAttr & Dos::AttrH) != 0;
        f["system"] = (x.dosAttr & Dos::AttrS) != 0;
        f["archive"] = (x.dosAttr & Dos::AttrA) != 0;
        t.attrs["fat"] = f;
    }
    for (const XItem &k : x.kids) t.kids.append(toTransfer(k));
    return t;
}

XItem fromTransfer(const dw::TransferItem &t) {
    XItem x;
    x.isDir = t.isDir;
    x.name = t.name;
    x.data = t.data;
    const QString &fs = t.sourceFs;
    const int src = fs == "adfs" ? PaneAdfs : fs == "dfs" ? PaneDfs0 : PaneDos;      // anything else: plain host files
    x.src = src;
    const QJsonObject acorn = t.attrs.value("acorn");
    const QString access = acorn["access"].toString();
    if (src == PaneAdfs || src == PaneDfs0) {
        x.hasAddr = !x.isDir && parseHex(acorn["load"], &x.load) && parseHex(acorn["exec"], &x.exec);
        x.locked = access.contains('L');       // case-sensitive: lower case letters are public access
        x.adfsFlags = quint8((access.contains('R') ? Adfs::FlagR : 0) | (access.contains('W') ? Adfs::FlagW : 0) |
                             (access.contains('L') ? Adfs::FlagL : 0) | (access.contains('E') ? Adfs::FlagE : 0));
        if (src == PaneDfs0) {                  // "D.NAME": the directory is part of a DFS name
            if (x.name.size() >= 3 && x.name[1] == '.' && Dfs::validDir(x.name[0])) {
                x.dfsDir = x.name[0];
                x.name = x.name.mid(2);
            } else {
                x.dfsDir = '$';
            }
        }
    } else {
        const QJsonObject fat = t.attrs.value("fat");
        x.locked = fat["readonly"].toBool() || access.contains('L');
        for (auto ns = t.attrs.begin(); ns != t.attrs.end(); ++ns)          // any namespace's "readonly" (e.g. cpm)
            if (ns.value()["readonly"].toBool()) x.locked = true;
        x.dosAttr = quint8((x.locked ? Dos::AttrR : 0) | (fat["hidden"].toBool() ? Dos::AttrH : 0) |
                           (fat["system"].toBool() ? Dos::AttrS : 0) | (fat["archive"].toBool() ? Dos::AttrA : 0));
    }
    for (const dw::TransferItem &k : t.kids) x.kids.append(fromTransfer(k));
    return x;
}

// ------------------------------------------------------------------------------ writing to the host

// How Acorn items become host files: DFS files named NAME or D.NAME, FAT names keep their extension apart, and
// (when asked) an Acorn .inf sidecar beside every ADFS and DFS file, exactly as Extract to host writes it.
class AcornPolicy : public dw::xfer::HostPolicy {
public:
    explicit AcornPolicy(bool writeInf) : m_inf(writeInf) {}
    QString hostName(const dw::TransferItem &it) const override {
        const bool dfs = it.sourceFs == "dfs";
        return dw::hostSafeName(dfs && it.name.startsWith("$.") ? it.name.mid(2) : it.name);
    }
    bool keepExtension(const dw::TransferItem &it) const override { return it.sourceFs == "fat"; }
    void afterFile(const dw::TransferItem &it, const QString &absDir, const QString &relDir, const QString &host,
                   dw::xfer::UsedNames &used, QStringList *topLevel, QStringList *errors) const override {
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
private:
    bool m_inf;
};

void writeHost(const QVector<XItem> &items, const QString &hostDir, bool writeInf, QStringList *topLevel,
               QJsonArray *jsonItems, QStringList *errors) {
    QVector<dw::TransferItem> t;
    for (const XItem &x : items) t.append(toTransfer(x));
    dw::xfer::writeHost(t, hostDir, AcornPolicy(writeInf), topLevel, jsonItems, errors);
}

QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName, int srcPane) {
    return dw::xfer::makeJson(items, hostDir, imageName, filesystemName(srcPane), Generator);
}

// ------------------------------------------------------------------------------ reading a drop

bool readJson(const QByteArray &json, QVector<XItem> *out, QString *err) {
    out->clear();
    QVector<dw::TransferItem> t;
    if (!dw::xfer::readJson(json, &t, err)) return false;
    for (const dw::TransferItem &x : t) out->append(fromTransfer(x));
    return true;
}

// ------------------------------------------------------------------------------ temporary folders

QString dragDirPrefix() { return QDir::temp().filePath("acorndiskwrangler-drag-"); }

QString newDragDir(QString *err) { return dw::xfer::newDragDir(dragDirPrefix(), err); }

int cleanStale(qint64 maxAgeSecs) { return dw::xfer::cleanStale(dragDirPrefix(), maxAgeSecs); }

// ------------------------------------------------------------------------------ the drag data

DragOutMime::DragOutMime(Exporter exporter, bool writeInf, const QString &imageName, int srcPane)
    : dw::DragOutMime(
          [exporter](QVector<dw::TransferItem> *out, QString *err) {
              QVector<XItem> items;
              if (!exporter(&items, err)) return false;
              for (const XItem &x : items) out->append(toTransfer(x));
              return true;
          },
          [&] {
              dw::DragSpec s;
              s.policy = std::make_shared<AcornPolicy>(writeInf);
              s.filesystem = filesystemName(srcPane);
              s.generator = Generator;
              s.imageName = imageName;
              s.tempPrefix = dragDirPrefix();
              return s;
          }()) {}

} // namespace RetroFiles
