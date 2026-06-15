// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "EmulatePicker.h"

#include "Theme.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace dish::ui {

EmulatePicker::EmulatePicker(const QList<composer::PickableType>& types, const QString& slotName,
                             int currentType, QWidget* parent)
    : QDialog(parent), chosenType_(currentType) {
    setWindowTitle(tr("Emulate"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* header = new QLabel(tr("EMULATE"), this);
    header->setStyleSheet(sectionHeaderQss());
    layout->addWidget(header);

    auto* intro = new QLabel(tr("Choose what %1 appears as on the host.").arg(slotName), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // The selectable type rows live in a scroll area so a long catalog (a newer
    // server may publish more types than the app knows) never blows up the
    // dialog height.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* listHost = new QWidget(scroll);
    auto* listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);

    group_ = new QButtonGroup(this);

    if (types.isEmpty()) {
        // Catalog not loaded yet (offline before first reach) — the picker still
        // opens but offers nothing; the player keeps the current type.
        auto* empty = new QLabel(tr("No controller types available yet."), listHost);
        empty->setStyleSheet(QStringLiteral("color: %1;").arg(hex(Theme::muted)));
        listLayout->addWidget(empty);
    }

    bool anySelected = false;
    for (const auto& t : types) {
        auto* card = new QFrame(listHost);
        card->setObjectName(QStringLiteral("card"));
        card->setStyleSheet(QStringLiteral("QFrame#card { border: 1px solid %1; border-radius: "
                                           "8px; background: %2; }")
                                .arg(hex(Theme::outline), hex(Theme::surface)));
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(10);

        auto* radio = new QRadioButton(card);
        group_->addButton(radio, t.type);
        if (t.type == currentType) {
            radio->setChecked(true);
            anySelected = true;
        }
        cardLayout->addWidget(radio, 0, Qt::AlignVCenter);

        auto* textLayout = new QVBoxLayout;
        textLayout->setSpacing(2);
        // Prefer the full localized name; fall back to shortName if a server-only
        // type sent only the short form.
        const QString title = t.name.isEmpty() ? t.shortName : t.name;
        auto* nameLabel = new QLabel(title, card);
        nameLabel->setStyleSheet(
            QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
        textLayout->addWidget(nameLabel);
        if (!t.description.isEmpty()) {
            auto* desc = new QLabel(t.description, card);
            desc->setWordWrap(true);
            desc->setStyleSheet(
                QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
            textLayout->addWidget(desc);
        }
        cardLayout->addLayout(textLayout, 1);

        // Make the whole card click-to-select, not just the radio dot.
        QObject::connect(radio, &QRadioButton::clicked, this, [radio] { radio->setChecked(true); });

        listLayout->addWidget(card);
    }
    listLayout->addStretch(1);
    scroll->setWidget(listHost);
    layout->addWidget(scroll, 1);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    applyDisabledOpacityEffect(cancel);
    auto* ok = new QPushButton(tr("Apply"), this);
    ok->setObjectName(QStringLiteral("primary"));
    applyDisabledOpacityEffect(ok);
    // Nothing to apply if no type is offered or none is selected.
    ok->setEnabled(!types.isEmpty() && (anySelected || group_->checkedId() != -1));
    row->addWidget(cancel);
    row->addWidget(ok);
    layout->addLayout(row);

    QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, this, &EmulatePicker::onAccept);
    // Enable Apply once the user picks any row.
    QObject::connect(group_, &QButtonGroup::idClicked, this, [ok](int) { ok->setEnabled(true); });
}

void EmulatePicker::onAccept() {
    const int id = group_->checkedId();
    if (id != -1) { chosenType_ = id; }
    emit typeChosen(chosenType_);
    accept();
}

} // namespace dish::ui
