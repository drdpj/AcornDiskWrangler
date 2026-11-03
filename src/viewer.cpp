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

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

static const int kMaxShown = 1024 * 1024;     // keep the window responsive for very large files

ViewerDialog::ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent)
    : QDialog(parent), m_title(title), m_data(data) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("View - " + title);
    resize(860, 620);

    m_mode = new QComboBox;
    m_mode->addItems({"Text", "Hex dump", "BBC BASIC listing"});
    m_wrap = new QCheckBox("Wrap lines");
    auto *copy = new QPushButton("Copy all");
    auto *save = new QPushButton("Save as text...");
    m_info = new QLabel;
    m_edit = new QPlainTextEdit;
    m_edit->setReadOnly(true);
    m_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("View as:"));
    top->addWidget(m_mode);
    top->addWidget(m_wrap);
    top->addStretch();
    top->addWidget(copy);
    top->addWidget(save);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_info);

    // pick the most useful view first
    int auto_ = ViewText::looksLikeBasic(data) ? Basic : (ViewText::looksLikeText(data) ? Text : Hex);
    m_mode->setCurrentIndex(auto_);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewerDialog::render);
    connect(m_wrap, &QCheckBox::toggled, this, [this](bool on) {
        m_edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    connect(copy, &QPushButton::clicked, this, &ViewerDialog::copyAll);
    connect(save, &QPushButton::clicked, this, &ViewerDialog::saveText);
    render();
}

ViewerDialog::Mode ViewerDialog::mode() const { return Mode(m_mode->currentIndex()); }
QString ViewerDialog::shownText() const { return m_edit->toPlainText(); }

void ViewerDialog::render() {
    QString text, note;
    bool truncated = m_data.size() > kMaxShown;
    QByteArray d = truncated ? m_data.left(kMaxShown) : m_data;
    switch (mode()) {
    case Text:
        text = ViewText::plain(d);
        break;
    case Hex:
        text = ViewText::hexDump(d);
        break;
    case Basic: {
        QString err;
        if (!ViewText::detokenise(m_data, &text, &err)) {
            note = "Not a valid tokenised BBC BASIC program: " + err;
            text = text.isEmpty() ? QString("[%1]\n\nTry the Text or Hex dump view.").arg(note)
                                  : text + QString("\n[%1]\n").arg(note);
        } else note = "BBC BASIC (II/IV) program, detokenised";
        break;
    }
    }
    if (truncated && mode() != Basic) note += (note.isEmpty() ? "" : " - ") + QString("showing the first 1 MiB only");
    m_edit->setPlainText(text);
    m_info->setText(QString("%1 bytes%2").arg(m_data.size()).arg(note.isEmpty() ? QString() : "  -  " + note));
}

void ViewerDialog::copyAll() {
    QApplication::clipboard()->setText(m_edit->toPlainText());
}

void ViewerDialog::saveText() {
    QString p = QFileDialog::getSaveFileName(this, "Save as text", m_title + (mode() == Hex ? ".hex.txt" : ".txt"));
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(m_edit->toPlainText().toUtf8()) < 0)
        QMessageBox::warning(this, "Save", f.errorString());
}
