// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/settings/CrashReportingRow.h"

#include "UI/Theme.h"
#include "architecture/Observable.h"
#include "source/store/CrashReportingStore.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace dish::ui {

namespace {

// Holds the store-subscription for the row's lifetime (parented to the row, so
// the RAII subscription tears down when the row is destroyed — before the store
// could call back into a freed checkbox).
class CrashRowBinding : public QObject {
  public:
    CrashRowBinding(QObject* parent, source::CrashReportingStore* store, QCheckBox* check)
        : QObject(parent) {
        // Observe-then-bind: reflect the store first (no preference re-write on
        // the first frame), then wire the user toggle.
        subscription_ = store->state().subscribe(
            [check](const bool& enabled) {
                if (check->isChecked() != enabled) {
                    const QSignalBlocker blocker(check);
                    check->setChecked(enabled);
                }
            },
            /*emitCurrent=*/true);
        QObject::connect(check, &QCheckBox::toggled, this,
                         [store](bool on) { store->setEnabled(on); });
    }

  private:
    dish::arch::Observable<bool>::Subscription subscription_;
};

} // namespace

QWidget* makeCrashReportingRow(source::CrashReportingStore* store, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(12);

    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);
    auto* title = new QLabel(QObject::tr("Share crash reports"), card);
    title->setStyleSheet(QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* detail = new QLabel(
        QObject::tr("On by default — turn off to opt out. Shares anonymized crash logs and stack "
                    "traces to help fix bugs; no gameplay or controller input is included. (No "
                    "crash backend is wired yet.)"),
        card);
    detail->setWordWrap(true);
    detail->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
    textColumn->addWidget(title);
    textColumn->addWidget(detail);

    auto* check = new QCheckBox(card);
    // Bind to the store (observe-then-bind) for the row's lifetime.
    new CrashRowBinding(card, store, check);

    row->addLayout(textColumn, 1);
    row->addWidget(check, 0, Qt::AlignVCenter);
    return card;
}

} // namespace dish::ui
