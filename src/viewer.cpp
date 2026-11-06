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
#include "viewer.h"
#include "viewtext.h"

#include <diskwrangler/text/viewers.h>

// BBC BASIC (II/IV): the program as a detokenised listing. It suits a file that is a structurally valid tokenised
// program, scores higher than Text so that such a file opens on it, and needs the whole file.
class BasicViewer : public dw::FileViewer {
public:
    QString id() const override { return "basic"; }
    QString name() const override { return "BBC BASIC listing"; }
    int order() const override { return 20; }
    int accepts(const QByteArray &d, const QVariantMap &) const override { return ViewText::looksLikeBasic(d) ? 3 : 0; }
    bool wantsAllData() const override { return true; }
    dw::ViewResult render(const QByteArray &d, const QVariantMap &) const override {
        dw::ViewResult r;
        QString err;
        if (!ViewText::detokenise(d, &r.text, &err)) {
            r.note = "Not a valid tokenised BBC BASIC program: " + err;
            r.text = r.text.isEmpty() ? QString("[%1]\n\nTry the Text or Hex dump view.").arg(r.note)
                                      : r.text + QString("\n[%1]\n").arg(r.note);
        } else {
            r.note = "BBC BASIC (II/IV) program, detokenised";
        }
        return r;
    }
};

static const dw::ViewerRegistry &acornViewers() {
    static dw::ViewerRegistry reg = [] {
        dw::ViewerRegistry r;
        dw::addDefaultViewers(r);
        r.add(std::make_shared<BasicViewer>());
        return r;
    }();
    return reg;
}

ViewerDialog::ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent)
    : dw::ViewerDialog(acornViewers(), title, data, parent) {}
