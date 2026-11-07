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
// adfsvolume.h - an old-map ADFS disc (Adfs::Image) as a DiskWranglerCore dw::fs::Volume, and the whole opened disc
// as a dw::fs::Disc: ADFS with the DOS Plus partition inside it, DFS side 0 and side 2, or a FAT floppy. These are
// the adapters of step 4 of the move to the core; the main window doesn't use them yet (step 5), the tests and the
// command-line program (step 4b) do.
#pragma once

#include <memory>

#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/volume.h>

#include "adfs.h"
#include "diskformat.h"

namespace Adfs {

class AdfsNameRules : public dw::fs::NameRules {
public:
    bool valid(const QString &name, dw::fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, dw::fs::NodeKind) const override { return sanitizeName(hostName); }
};

// Paths are names from $ ("Games/Elite" is $.Games.Elite). File attributes: R W L E, load, exec (and, read only, the
// file type and date that a stamped load/exec carries). Volume attributes: title and boot option.
class AdfsVolume : public dw::fs::Volume {
public:
    explicit AdfsVolume(Image *img) : m_img(img) {}       // the Image must outlive the volume, and be open
    Image *image() const { return m_img; }
    // DFS files copied in: a letter other than $ becomes a subdirectory of the target (docs/design.md, "DFS
    // directory letters"); flattened, every file goes in the target directory.
    void setFlattenLetters(bool on) { m_flatten = on; }

    QString fsId() const override { return "adfs"; }
    dw::fs::Caps caps() const override;
    QVector<dw::fs::AttrSpec> fileAttrs() const override;
    QVector<dw::fs::AttrSpec> volumeAttrs() const override;
    dw::fs::AttrValues volumeValues() const override;
    const dw::fs::NameRules &names() const override { return m_names; }
    dw::fs::VolumeInfo info() const override;

    bool list(const dw::fs::Path &dir, QVector<dw::fs::Entry> *out, QString *err) const override;
    bool read(const dw::fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const dw::fs::Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool mkdir(const dw::fs::Path &dir, const QString &name, QString *err) override;
    bool remove(const dw::fs::Path &node, QString *err) override;
    bool rename(const dw::fs::Path &from, const dw::fs::Path &to, QString *err) override;   // within one directory
    bool setAttrs(const dw::fs::Path &node, const dw::fs::AttrValues &a, QString *err) override;
    bool setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) override;
    dw::fs::Report check() const override { return m_img->validate(); }
    bool reload(QString *err) override { return m_img->reload(err); }

    // retro-files "acorn": load and exec as 8 hex digits (files only), access as letters "LWRE"
    QMap<QString, QJsonObject> toRetro(const dw::fs::Entry &e) const override;
    dw::fs::AttrValues fromRetro(const QMap<QString, QJsonObject> &families, dw::fs::NodeKind kind) const override;
    dw::fs::Placement placeIncoming(const dw::TransferItem &item, const dw::fs::Path &targetDir) const override;

    bool dirSector(const dw::fs::Path &dir, quint32 *sector, QString *err) const;   // a directory's sector
    static dw::fs::Entry entry(const Entry &e);

private:
    bool find(const dw::fs::Path &node, quint32 *parent, Entry *e, QString *err) const;
    Image *m_img;
    AdfsNameRules m_names;
    bool m_flatten = false;
};

// Everything an opened disc holds, the way the main window shows it: owns the Image and the volumes.
// mounts: ADFS ("ADFS", read-only on a hybrid) and DOS Plus ("DOS", $.DOS.DRIVE_C or the hybrid's FAT); "drive 0"
// and "drive 2" (DFS); "DOS" alone for a FAT floppy.
class OpenedDisc {
public:
    OpenedDisc() = default;
    OpenedDisc(const OpenedDisc &) = delete;
    OpenedDisc &operator=(const OpenedDisc &) = delete;
    // Opens path as info says (detectDisk / diskInfo of a detection candidate).
    bool open(const QString &path, const DiskInfo &info, QString *err);
    dw::fs::Disc &disc() { return m_disc; }
    Image &image() { return m_img; }
    const DiskInfo &info() const { return m_info; }
    bool save(QString *err, bool backup = false);        // the Disc's checked save (every mount checked first)

private:
    Image m_img;
    DiskInfo m_info;
    std::shared_ptr<dw::fat::Volume> m_dos;
    std::shared_ptr<dw::dfs::Disc> m_dfs[2];
    dw::fs::Disc m_disc;
};

} // namespace Adfs
