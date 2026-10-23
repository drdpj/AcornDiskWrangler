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

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <map>
#include <vector>

namespace RetroFiles {

const char *const MimeType = "application/x-retro-files+json";
static const char *const UriList = "text/uri-list";

QString filesystemName(int srcPane) {
    if (srcPane == PaneAdfs) return "adfs";
    if (srcPane == PaneDfs0 || srcPane == PaneDfs1) return "dfs";
    return "fat";
}

static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

static QString adfsAccess(quint8 flags, bool dir) {
    QString s = dir ? "D" : "";
    if (flags & Adfs::FlagL) s += 'L';
    if (flags & Adfs::FlagW) s += 'W';
    if (flags & Adfs::FlagR) s += 'R';
    if (flags & Adfs::FlagE) s += 'E';
    return s;
}

// ------------------------------------------------------------------------------ writing to the host

// A name not used yet in this folder (compared ignoring case: Windows and macOS folders are case-insensitive).
static QString unique(const QString &name, QSet<QString> &used, bool keepExtension) {
    QString n = name;
    int dot = keepExtension ? name.lastIndexOf('.') : -1;
    for (int k = 2; used.contains(n.toLower()); ++k)
        n = dot > 0 ? name.left(dot) + "_" + QString::number(k) + name.mid(dot) : name + "_" + QString::number(k);
    used.insert(n.toLower());
    return n;
}

static void writeOne(const XItem &x, const QString &absDir, const QString &relDir, QSet<QString> &used, bool writeInf,
                     QStringList *topLevel, QJsonArray *items, QStringList *errors) {
    bool dfs = x.src == PaneDfs0 || x.src == PaneDfs1;
    QString host = hostSafeName(dfs && x.dfsDir != '$' ? QString(x.dfsDir) + "." + x.name : x.name);
    host = unique(host, used, x.src == PaneDos && !x.isDir);
    QString abs = absDir + "/" + host, rel = relDir.isEmpty() ? host : relDir + "/" + host;
    QString shown = dfs ? QString(x.dfsDir) + "." + x.name : x.name;

    QJsonObject j;
    j["path"] = rel;
    j["type"] = x.isDir ? "directory" : "file";
    j["name"] = shown;                          // a DFS name always carries its directory: "$.BOOT", "A.BOOT"
    QJsonObject attrs;
    if (x.src == PaneAdfs) {
        QJsonObject a;
        if (!x.isDir) { a["load"] = hex8(x.load); a["exec"] = hex8(x.exec); }
        a["access"] = adfsAccess(x.adfsFlags, x.isDir);
        attrs["acorn"] = a;
    } else if (dfs) {
        QJsonObject a;
        a["load"] = hex8(extendDfsAddr(x.load));
        a["exec"] = hex8(extendDfsAddr(x.exec));
        a["access"] = x.locked ? "L" : "WR";
        attrs["acorn"] = a;
    } else {
        QJsonObject f;
        f["readonly"] = (x.dosAttr & Dos::AttrR) != 0;
        f["hidden"] = (x.dosAttr & Dos::AttrH) != 0;
        f["system"] = (x.dosAttr & Dos::AttrS) != 0;
        f["archive"] = (x.dosAttr & Dos::AttrA) != 0;
        attrs["fat"] = f;
    }
    j["attributes"] = attrs;
    if (x.when.isValid()) j["modified"] = x.when.toString("yyyy-MM-ddTHH:mm:ss");

    if (x.isDir) {
        if (!QDir().mkpath(abs)) { errors->append(QString("%1: cannot create folder %2").arg(shown, abs)); return; }
        items->append(j);
        if (topLevel) topLevel->append(rel);
        QSet<QString> inner;
        for (const XItem &k : x.kids) writeOne(k, abs, rel, inner, writeInf, nullptr, items, errors);
        return;
    }
    QFile f(abs);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(x.data) != x.data.size()) {
        errors->append(QString("%1: %2").arg(shown, f.errorString()));
        return;
    }
    f.close();
    if (x.when.isValid()) {                       // the file's date, as the disc records it
        QFile t(abs);
        if (t.open(QIODevice::Append)) t.setFileTime(x.when, QFileDevice::FileModificationTime);
    }
    j["size"] = double(x.data.size());
    items->append(j);
    if (topLevel) topLevel->append(rel);

    // Acorn .inf sidecar, exactly as Extract to host writes it (for Explorer users and Acorn tools)
    if (writeInf && (x.src == PaneAdfs || dfs)) {
        QString line;
        if (dfs)
            line = QString::asprintf("%c.%-7s %08X %08X %08X %02X\n", x.dfsDir.toLatin1(), qPrintable(x.name),
                                     extendDfsAddr(x.load), extendDfsAddr(x.exec), unsigned(x.data.size()), x.locked ? 8 : 3);
        else {
            unsigned attr = ((x.adfsFlags & Adfs::FlagR) ? 1 : 0) | ((x.adfsFlags & Adfs::FlagW) ? 2 : 0) |
                            ((x.adfsFlags & Adfs::FlagE) ? 4 : 0) | ((x.adfsFlags & Adfs::FlagL) ? 8 : 0);
            line = QString::asprintf("%-10s %08X %08X %08X %02X\n", qPrintable(x.name), x.load, x.exec,
                                     unsigned(x.data.size()), attr);
        }
        QString infName = unique(host + ".inf", used, false);
        QFile inf(absDir + "/" + infName);
        if (!inf.open(QIODevice::WriteOnly | QIODevice::Truncate) || inf.write(line.toLatin1()) < 0)
            errors->append(QString("%1: %2").arg(infName, inf.errorString()));
        else if (topLevel)
            topLevel->append(relDir.isEmpty() ? infName : relDir + "/" + infName);
    }
}

void writeHost(const QVector<XItem> &items, const QString &hostDir, bool writeInf, QStringList *topLevel,
               QJsonArray *jsonItems, QStringList *errors) {
    QSet<QString> used;
    for (const XItem &x : items) writeOne(x, hostDir, QString(), used, writeInf, topLevel, jsonItems, errors);
}

QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName, int srcPane) {
    QJsonObject src;
    src["filesystem"] = filesystemName(srcPane);
    if (!imageName.isEmpty()) src["image"] = imageName;
    QJsonObject o;
    o["format"] = "retro-files";
    o["version"] = 1;
    o["generator"] = "Acorn Disk Wrangler";
    o["source"] = src;
    o["base"] = QUrl::fromLocalFile(hostDir).toString();
    o["items"] = items;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

// ------------------------------------------------------------------------------ reading a drop

static bool parseHex(const QJsonValue &v, quint32 *out) {
    bool ok = false;
    quint32 n = v.toString().toUInt(&ok, 16);
    if (ok) *out = n;
    return ok && v.isString();
}

bool readJson(const QByteArray &json, QVector<XItem> *out, QString *err) {
    out->clear();
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) { *err = "the dropped file list is not valid JSON: " + pe.errorString(); return false; }
    QJsonObject o = doc.object();
    if (o["format"].toString() != "retro-files") { *err = "the dropped data is not a retro-files list"; return false; }
    if (o["version"].toInt() != 1) { *err = QString("retro-files version %1 is not supported").arg(o["version"].toInt()); return false; }
    QString fs = o["source"].toObject()["filesystem"].toString();
    int src = fs == "adfs" ? PaneAdfs : fs == "dfs" ? PaneDfs0 : PaneDos;      // anything else: plain host files
    QString base = QUrl(o["base"].toString()).toLocalFile();
    if (base.isEmpty() || !QFileInfo(base).isDir()) { *err = "the dropped files' folder is missing"; return false; }
    QString canonBase = QFileInfo(base).canonicalFilePath();

    struct Node { XItem x; std::vector<int> kids; };
    std::vector<Node> nodes;
    std::vector<int> roots;
    std::map<QString, int> byPath;
    for (const QJsonValue &v : o["items"].toArray()) {
        QJsonObject it = v.toObject();
        QString path = it["path"].toString();
        QStringList parts = path.split('/');
        if (path.isEmpty() || path.startsWith('/') || parts.contains("..") || parts.contains(".") || parts.contains("")) {
            *err = QString("'%1': not a path inside the drop folder").arg(path);
            return false;
        }
        Node n;
        XItem &x = n.x;
        x.isDir = it["type"].toString() == "directory";
        x.name = it["name"].toString().isEmpty() ? parts.last() : it["name"].toString();
        x.src = src;
        QJsonObject attrs = it["attributes"].toObject(), acorn = attrs["acorn"].toObject();
        QString access = acorn["access"].toString();
        if (src == PaneAdfs || src == PaneDfs0) {
            x.hasAddr = !x.isDir && parseHex(acorn["load"], &x.load) && parseHex(acorn["exec"], &x.exec);
            x.locked = access.contains('L');
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
            QJsonObject fat = attrs["fat"].toObject();
            x.locked = fat["readonly"].toBool() || access.contains('L');
            for (auto ns = attrs.begin(); ns != attrs.end(); ++ns)          // any namespace's "readonly" (e.g. cpm)
                if (ns.value().toObject()["readonly"].toBool()) x.locked = true;
            x.dosAttr = quint8((x.locked ? Dos::AttrR : 0) | (fat["hidden"].toBool() ? Dos::AttrH : 0) |
                               (fat["system"].toBool() ? Dos::AttrS : 0) | (fat["archive"].toBool() ? Dos::AttrA : 0));
        }
        QString abs = QDir(base).filePath(path);
        QString canon = QFileInfo(abs).canonicalFilePath();
        if (canon.isEmpty() || !canon.startsWith(canonBase + "/")) {
            *err = QString("'%1': the dropped file is missing or outside the drop folder").arg(path);
            return false;
        }
        if (!x.isDir) {
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) { *err = QString("%1: %2").arg(path, f.errorString()); return false; }
            x.data = f.readAll();
            if (it.contains("size") && qint64(it["size"].toDouble()) != x.data.size()) {
                *err = QString("'%1': the file is not the size the list says").arg(path);
                return false;
            }
        }
        int idx = int(nodes.size());
        nodes.push_back(n);
        QString parent = parts.size() > 1 ? path.left(path.lastIndexOf('/')) : QString();
        auto p = byPath.find(parent);
        if (!parent.isEmpty() && p != byPath.end() && nodes[size_t(p->second)].x.isDir) nodes[size_t(p->second)].kids.push_back(idx);
        else roots.push_back(idx);             // a parent that isn't listed: take it as a top-level item
        byPath[path] = idx;
    }
    std::function<XItem(int)> build = [&](int i) {
        XItem x = nodes[size_t(i)].x;
        for (int k : nodes[size_t(i)].kids) x.kids.append(build(k));
        return x;
    };
    for (int r : roots) out->append(build(r));
    if (out->isEmpty()) { *err = "the dropped list has no files"; return false; }
    return true;
}

// ------------------------------------------------------------------------------ temporary folders

QString dragDirPrefix() { return QDir::temp().filePath("acorndiskwrangler-drag-"); }

static std::vector<std::unique_ptr<QTemporaryDir>> &dragDirs() {
    static std::vector<std::unique_ptr<QTemporaryDir>> dirs;    // removed when the program exits
    return dirs;
}

QString newDragDir(QString *err) {
    std::unique_ptr<QTemporaryDir> d(new QTemporaryDir(dragDirPrefix() + "XXXXXX"));
    if (!d->isValid()) { *err = "cannot create a temporary folder: " + d->errorString(); return QString(); }
    QString p = d->path();
    dragDirs().push_back(std::move(d));
    return p;
}

int cleanStale(qint64 maxAgeSecs) {
    int n = 0;
    QFileInfo pre(dragDirPrefix());
    QDir tmp(pre.absolutePath());
    for (const QFileInfo &fi : tmp.entryInfoList({pre.fileName() + "*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.lastModified().secsTo(QDateTime::currentDateTime()) < maxAgeSecs) continue;
        if (QDir(fi.absoluteFilePath()).removeRecursively()) ++n;
    }
    return n;
}

// ------------------------------------------------------------------------------ the drag data

DragOutMime::DragOutMime(Exporter exporter, bool writeInf, const QString &imageName, int srcPane)
    : m_exporter(exporter), m_writeInf(writeInf), m_image(imageName), m_src(srcPane), m_state(new DragState) {}

QStringList DragOutMime::formats() const {
    QStringList f = QMimeData::formats();
    for (const char *m : {UriList, MimeType})
        if (!f.contains(m)) f << m;
    return f;
}

bool DragOutMime::hasFormat(const QString &mimeType) const { return formats().contains(mimeType); }

QVariant DragOutMime::retrieveData(const QString &mimeType, QVariant::Type type) const {
    if (mimeType == UriList) {
        extract();
        if (type == QVariant::ByteArray || type == QVariant::String) {
            QByteArray b;
            for (const QUrl &u : m_urls) b += u.toEncoded() + "\r\n";
            return type == QVariant::String ? QVariant(QString::fromUtf8(b)) : QVariant(b);
        }
        QVariantList l;
        for (const QUrl &u : m_urls) l << u;
        return l;
    }
    if (mimeType == MimeType) {
        extract();
        return m_json;
    }
    return QMimeData::retrieveData(mimeType, type);
}

void DragOutMime::extract() const {
    if (m_state->extracted) return;
    m_state->extracted = true;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QVector<XItem> items;
    QString err;
    QString dir = m_exporter(&items, &err) ? newDragDir(&err) : QString();
    if (dir.isEmpty()) {
        m_state->errors << err;
    } else {
        QStringList top;
        QJsonArray j;
        writeHost(items, dir, m_writeInf, &top, &j, &m_state->errors);
        for (const QString &t : top) m_urls << QUrl::fromLocalFile(dir + "/" + t);
        m_json = makeJson(j, dir, m_image, m_src);
        m_state->dir = dir;
        for (const QJsonValue &v : j) (v.toObject()["type"].toString() == "directory" ? m_state->dirs : m_state->files)++;
    }
    QGuiApplication::restoreOverrideCursor();
}

} // namespace RetroFiles
