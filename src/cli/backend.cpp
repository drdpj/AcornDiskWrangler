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
#include "backend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <diskwrangler/device/sideorder.h>
#include <diskwrangler/fs/dfs/dfs.h>

#include "../adfsvolume.h"
#include "../diskformat.h"
#include "../hostpolicy.h"
#include "../newdisc.h"

using dw::cli::OpenOutcome;
using namespace dw::cli;

namespace AcornCli {

static QString kindId(DiskInfo::Kind k) {
    switch (k) {
    case DiskInfo::Adfs: return "adfs";
    case DiskInfo::AdfsDosHybrid: return "adfs-dos-hybrid";
    case DiskInfo::Dfs: return "dfs";
    case DiskInfo::DosPc: return "dos-pc";
    case DiskInfo::DosAcorn800: return "dos-acorn800";
    case DiskInfo::AdfsNewMap: return "adfs-newmap";
    case DiskInfo::AcornCpm: return "acorn-cpm";
    case DiskInfo::TorchCpn: return "torch-cpn";
    case DiskInfo::Unknown: break;
    }
    return "unknown";
}

// ------------------------------------------------------------------------------------------------ the opened image

static QString tempBeside(const QString &file) {
    const QFileInfo fi(file);
    return fi.absoluteDir().filePath(QString(".dwconv-%1-%2").arg(QUuid::createUuid().toString(QUuid::Id128).left(8), fi.fileName()));
}

class Image : public OpenedImage {
public:
    Image(const QString &path) : m_path(path) {}
    Adfs::OpenedDisc od;

    dw::fs::Disc &disc() override { return od.disc(); }
    bool dirty() override { return od.image().dirty(); }
    bool save(bool backup, QString *err) override { return od.save(err, backup); }

    QJsonObject app() const override {
        const DiskInfo &i = od.info();
        QJsonObject sides;
        sides["doubleSided"] = i.sides.doubleSided();
        sides["order"] = i.sides.doubleSided() ? QJsonValue(i.sides.sequential ? "sequential" : "interleaved") : QJsonValue(QJsonValue::Null);
        sides["confirmed"] = i.sidesConfirmed;
        sides["forced"] = i.sidesForced;
        QJsonObject o;
        o["kind"] = kindId(i.kind);
        o["description"] = i.description;
        o["sides"] = sides;
        o["fullSize"] = double(i.fullSize);
        o["notes"] = QJsonArray::fromStringList(i.notes);
        return o;
    }

    bool convertSides(const QString &toSides, QString *err, int *exit) override {
        if (!od.image().setConvertOnSave(toSides == "sequential", err)) { *exit = ExitUnsupported; return false; }
        return true;
    }

    // Opens a written copy the way the program opens any image and requires the same kind of disc and a clean check.
    static bool verifyCopy(const QString &path, DiskInfo::Kind want, int forceSeq, QString *err) {
        DiskInfo unknown;
        const dw::fs::Verdict v = detectCandidates(path, forceSeq, &unknown);
        if (v.action != dw::fs::Verdict::Open && v.action != dw::fs::Verdict::Ask) { *err = "the copy isn't recognised as the same kind of disc"; return false; }
        const DiskInfo info = diskInfo(*v.bestCandidate());
        if (info.kind != want) { *err = "the copy is read as " + info.description + ", not as the original kind of disc"; return false; }
        Adfs::OpenedDisc d;
        if (!d.open(path, info, err)) return false;
        const dw::fs::Report r = d.disc().validateAll();
        if (!r.ok()) { *err = "the copy fails its check: " + r.errors.join("; "); return false; }
        return true;
    }

    bool convertTo(const QString &out, const ConvertOptions &o, QJsonObject *result, QString *err, int *exit) override {
        using dw::device::Kind;
        const DiskInfo &info = od.info();
        const Kind from = od.image().container().kind();
        Kind to = from;
        if (o.container == "raw") to = Kind::Raw;
        else if (o.container == "imd") to = Kind::Imd;
        else if (o.container == "dsk") to = Kind::Dsk;
        else if (o.container == "edsk") to = Kind::Edsk;
        else if (o.container == "td0") to = Kind::Td0;
        if (to == Kind::Td0) { *err = "TeleDisk files can be read but not written: name --container=raw, imd, dsk or edsk"; *exit = ExitUnsupported; return false; }
        if (!o.toSides.isEmpty() && !info.sides.doubleSided()) { *err = "only a double-sided floppy has a side order"; *exit = ExitUnsupported; return false; }
        if (!o.toSides.isEmpty() && to != Kind::Raw) { *err = "a side order belongs to a raw image: the other containers record where every track is"; *exit = ExitUnsupported; return false; }
        const bool sequentialOut = o.toSides == "sequential";
        const int forceSeq = to == Kind::Raw && info.sides.doubleSided() ? (o.toSides.isEmpty() ? 0 : sequentialOut ? 1 : 0) : -1;
        dw::device::Container x = od.image().container();
        x.discardPending();
        QString why;
        if (to != Kind::Raw) {
            if (from == Kind::Raw) {                                     // give the raw bytes their tracks
                dw::device::RawLayout l;
                if (!rawLayoutFor(info, &l, &why)) { *err = why; *exit = ExitUnsupported; return false; }
                x.setRawPadding(qint64(info.fullSize), char(0));
                if (!x.setRawLayout(l, err)) { *exit = ExitFailed; return false; }
            }
            const DiskInfo::Kind want = info.kind;
            if (!x.saveAs(out, to, err, [&](dw::device::Container &w, QString *e) { return verifyCopy(w.path(), want, -1, e); })) { *exit = ExitSave; return false; }
        } else {
            // the stored bytes (interleaved), padded to the disc's full size, in the side order asked for
            const qint64 n = x.storedSize(&why);
            if (n <= 0) { *err = "this container can't be written as a raw image: " + why; *exit = ExitUnsupported; return false; }
            QByteArray bytes;
            if (!x.readStored(0, n, &bytes, err)) { *exit = ExitFailed; return false; }
            if (quint64(bytes.size()) < info.fullSize) bytes.append(QByteArray(int(info.fullSize - quint64(bytes.size())), char(0)));
            if (from == Kind::Raw && info.sides.sequential) {            // the raw file is sequential: the stored bytes are too
                if (!sequentialOut && !o.toSides.isEmpty()) bytes = dw::device::reorderSides(bytes, info.sides, false);
                else if (o.toSides.isEmpty()) { /* keep as it is */ }
            } else if (info.sides.doubleSided() && sequentialOut) {
                bytes = dw::device::reorderSides(bytes, info.sides, true);
            }
            const QString tmp = tempBeside(out);
            QFile f(tmp);
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) { *err = QString("cannot write %1: %2").arg(out, f.errorString()); *exit = ExitSave; QFile::remove(tmp); return false; }
            f.close();
            if (!verifyCopy(tmp, info.kind, forceSeq, err)) { QFile::remove(tmp); *exit = ExitSave; return false; }
            if (QFile::exists(out)) QFile::remove(out);
            if (!QFile::rename(tmp, out)) { *err = "cannot write " + out; QFile::remove(tmp); *exit = ExitSave; return false; }
        }
        QJsonObject r;
        r["container"] = dw::device::kindName(to);
        r["size"] = double(QFileInfo(out).size());
        if (info.sides.doubleSided() && to == Kind::Raw) r["order"] = sequentialOut ? "sequential" : "interleaved";
        *result = r;
        return true;
    }

private:
    QString m_path;
};

// ------------------------------------------------------------------------------------------------ the Backend

dw::fs::OtherApp Backend::self() const { return dw::fs::acornDiskWrangler(); }

OpenOutcome Backend::open(const QString &path, const OpenOptions &o) {
    OpenOutcome out;
    int force = -1;
    if (o.sides == "interleaved") force = 0;
    else if (o.sides == "sequential") force = 1;
    dw::device::Container c;
    QString err;
    if (!c.open(path, &err)) { out.status = OpenOutcome::Failed; out.message = path + ": " + err; return out; }
    out.container = containerInfo(path, c);
    DiskInfo unknown;
    const dw::fs::Verdict v = detectCandidates(path, force, &unknown);
    resolveVerdict(v, o.fs, &out);
    if (out.status != OpenOutcome::Ok) return out;
    const dw::fs::Candidate &cand = v.ranked[out.chosen];
    if (!cand.supported) { out.status = OpenOutcome::Unsupported; out.message = cand.description + ": this program can't open it"; return out; }
    DiskInfo info = diskInfo(cand);
    if (force >= 0 && info.sides.doubleSided()) info.sidesForced = true;
    std::unique_ptr<Image> img(new Image(path));
    if (!img->od.open(path, info, &err)) { out.status = OpenOutcome::Failed; out.message = err; return out; }
    out.image = std::move(img);
    return out;
}

struct Preset { const char *id; NewDisc::Type type; };
static const Preset presets[] = {
    {"adfs-s", NewDisc::AdfsS}, {"adfs-m", NewDisc::AdfsM}, {"adfs-l", NewDisc::AdfsL},
    {"dfs-ss40", NewDisc::DfsSs40}, {"dfs-ss80", NewDisc::DfsSs80}, {"dfs-ds40", NewDisc::DfsDs40}, {"dfs-ds80", NewDisc::DfsDs80},
    {"wat-ss40", NewDisc::WatSs40}, {"wat-ss80", NewDisc::WatSs80}, {"wat-ds40", NewDisc::WatDs40}, {"wat-ds80", NewDisc::WatDs80},
    {"dos-800", NewDisc::Acorn800}, {"dos-640", NewDisc::Acorn640},
    {"pc-360", NewDisc::Pc360}, {"pc-720", NewDisc::Pc720}, {"pc-1200", NewDisc::Pc1200}, {"pc-1440", NewDisc::Pc1440}};

QJsonArray Backend::formats() const {
    QJsonArray a;
    for (const Preset &p : presets) {
        const NewDisc::TypeInfo &t = NewDisc::info(p.type);
        QJsonObject o;
        o["id"] = p.id;
        o["name"] = t.name;
        o["suffix"] = t.suffix;
        o["doubleSided"] = t.doubleSided;
        o["titleMax"] = t.titleMax;
        o["titleWhat"] = t.titleWhat;
        a.append(o);
    }
    return a;
}

bool Backend::create(const QString &file, const NewOptions &n, QString *err, int *exit) {
    const Preset *pr = nullptr;
    for (const Preset &p : presets) if (n.format == p.id) pr = &p;
    if (!pr) { *err = QString("unknown format '%1' (acorndw new --list)").arg(n.format); *exit = ExitUsage; return false; }
    if (!n.container.isEmpty() && n.container != "raw") { *err = "acorndw makes raw images only: make one, then 'convert --container=" + n.container + "'"; *exit = ExitUnsupported; return false; }
    const NewDisc::TypeInfo &t = NewDisc::info(pr->type);
    if (n.sides == "sequential" && !t.doubleSided) { *err = "--sides is for the double-sided formats"; *exit = ExitUsage; return false; }
    if (n.title.size() > t.titleMax) { *err = QString("a %1 is at most %2 characters").arg(t.titleWhat).arg(t.titleMax); *exit = ExitUsage; return false; }
    if (pr->type == NewDisc::Acorn640 && n.dosboot.isEmpty()) { *err = "dos-640 needs --dosboot=FILE (Acorn's boot code can't be built in)"; *exit = ExitUsage; return false; }
    if (!n.dosboot.isEmpty() && !QFileInfo::exists(n.dosboot)) { *err = n.dosboot + ": no such file"; *exit = ExitNotFound; return false; }
    if (!NewDisc::create(file, pr->type, n.title, n.sides == "sequential", err, n.dosboot)) { *exit = ExitFailed; return false; }
    return true;
}

std::shared_ptr<dw::xfer::HostPolicy> Backend::hostPolicy(const GetOptions &o, QString *err, int *exit) {
    if (o.sidecars != "inf" && o.sidecars != "none" && !o.sidecars.isEmpty()) { *err = "--sidecars is inf or none"; *exit = ExitUsage; return nullptr; }
    return std::make_shared<RetroFiles::AcornPolicy>(o.sidecars != "none");
}

static bool readHostItems(const QStringList &paths, bool useInf, QVector<dw::TransferItem> *items, QString *err, int *exit) {
    auto isSidecar = [&](const QString &p) {
        return useInf && p.endsWith(".inf", Qt::CaseInsensitive) && QFileInfo::exists(p.left(p.size() - 4));
    };
    const HostFileFilter accept = [&](const QString &p, dw::TransferItem *it) {
        if (isSidecar(p)) return false;
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
    return readHostTree(paths, accept, items, err, exit);
}


bool Backend::readHost(const QStringList &paths, const PutOptions &o, QVector<dw::TransferItem> *items, QStringList *, QString *err, int *exit) {
    if (o.sidecars != "inf" && o.sidecars != "none" && !o.sidecars.isEmpty()) { *err = "--sidecars is inf or none"; *exit = ExitUsage; return false; }
    return readHostItems(paths, o.sidecars != "none", items, err, exit);
}

void Backend::configureTarget(dw::fs::Volume *v, const ImportOptions &o) {
    if (auto *a = dynamic_cast<Adfs::AdfsVolume *>(v)) a->setFlattenLetters(o.letters == "flatten");
}

} // namespace AcornCli
