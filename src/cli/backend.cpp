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

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    // The conversion is the window's Save As's too (Adfs::OpenedDisc::writeCopy): the same bytes either way.
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
        const bool sequentialOut = o.toSides == "sequential";
        Adfs::OpenedDisc::CopyFailure why;
        if (!od.writeCopy(out, to, o.toSides, false, err, &why)) {
            *exit = why == Adfs::OpenedDisc::CopyFailure::Unsupported ? ExitUnsupported
                    : why == Adfs::OpenedDisc::CopyFailure::Save ? ExitSave : ExitFailed;
            return false;
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

QJsonArray Backend::formats() const {
    QJsonArray a;
    for (const NewDisc::TypeInfo &t : NewDisc::types()) {          // the presets in the registry, in this program's order
        QJsonObject o;
        o["id"] = t.id;
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
    const NewDisc::TypeInfo *pr = nullptr;
    for (const NewDisc::TypeInfo &t : NewDisc::types()) if (n.format == t.id) pr = &t;
    if (!pr) { *err = QString("unknown format '%1' (acorndw new --list)").arg(n.format); *exit = ExitUsage; return false; }
    if (!n.container.isEmpty() && n.container != "raw") { *err = "acorndw makes raw images only: make one, then 'convert --container=" + n.container + "'"; *exit = ExitUnsupported; return false; }
    const NewDisc::TypeInfo &t = *pr;
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

bool Backend::readHost(const QStringList &paths, const PutOptions &o, QVector<dw::TransferItem> *items, QStringList *, QString *err, int *exit) {
    if (o.sidecars != "inf" && o.sidecars != "none" && !o.sidecars.isEmpty()) { *err = "--sidecars is inf or none"; *exit = ExitUsage; return false; }
    QVector<dw::xfer::HostReadError> errors;
    const QVector<dw::TransferItem> got = dw::xfer::readHost(paths, acornHostRead(o.sidecars != "none"), &errors);
    if (hostReadFailed(errors, err, exit)) return false;      // the first file that can't be read, or isn't there
    *items += got;
    return true;
}

void Backend::configureTarget(dw::fs::Volume *v, const ImportOptions &o) {
    if (auto *a = dynamic_cast<Adfs::AdfsVolume *>(v)) a->setFlattenLetters(o.letters == "flatten");
}

} // namespace AcornCli
