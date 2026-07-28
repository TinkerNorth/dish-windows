// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Non-color design tokens for the QML chrome — type scale, spacing, radii and
// shell metrics — as a `Tokens` singleton beside the color `Theme` singleton.
// Values mirror the design-system token sheet (tokens/typography.css,
// tokens/spacing.css) and the flows shell spec (win.jsx), which themselves were
// read out of this app's layout code; centralizing them here ends the raw
// pixelSize/radius literals scattered through the pages.
//
// All CONSTANT: metrics do not change with the palette. QML/Quick-only;
// DISH_QML build exclusively. Registered BY INSTANCE in QmlEntryPoint — the
// declarative QML_SINGLETON registration is stripped under this target's LTCG
// (same quirk as ThemeBridge).

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace dish::chrome {

class TokensBridge : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Tokens)
    QML_SINGLETON

    // ── Type scale (px) ──────────────────────────────────────────────────────
    Q_PROPERTY(int textStatus READ textStatus CONSTANT)   // 17 — header status line
    Q_PROPERTY(int textTitle READ textTitle CONSTANT)     // 20 — page title (design header)
    Q_PROPERTY(int textHeading READ textHeading CONSTANT) // 16 — dialog heading
    Q_PROPERTY(int textBase READ textBase CONSTANT)       // 13 — global base
    Q_PROPERTY(int textSummary READ textSummary CONSTANT) // 12 — muted helper text
    Q_PROPERTY(int textMeta READ textMeta CONSTANT)       // 11 — bound-to line, section label
    Q_PROPERTY(int textChip READ textChip CONSTANT)       // 10 — pills, telemetry footer
    // Tracked-out mono treatment for section labels / eyebrows.
    Q_PROPERTY(qreal sectionLetterSpacing READ sectionLetterSpacing CONSTANT) // 1.5
    // The platform-generic monospace the telemetry/Hz readouts render in.
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)

    // ── Spacing scale (px) ───────────────────────────────────────────────────
    Q_PROPERTY(int s1 READ s1 CONSTANT)                   // 2
    Q_PROPERTY(int s2 READ s2 CONSTANT)                   // 4
    Q_PROPERTY(int s3 READ s3 CONSTANT)                   // 6
    Q_PROPERTY(int s4 READ s4 CONSTANT)                   // 8
    Q_PROPERTY(int s5 READ s5 CONSTANT)                   // 10
    Q_PROPERTY(int s6 READ s6 CONSTANT)                   // 12
    Q_PROPERTY(int s7 READ s7 CONSTANT)                   // 14
    Q_PROPERTY(int s8 READ s8 CONSTANT)                   // 16
    Q_PROPERTY(int s9 READ s9 CONSTANT)                   // 20
    Q_PROPERTY(int pagePadding READ pagePadding CONSTANT) // 24 — content side margins

    // ── Radii (px) ───────────────────────────────────────────────────────────
    Q_PROPERTY(int radiusChip READ radiusChip CONSTANT)     // 5
    Q_PROPERTY(int radiusButton READ radiusButton CONSTANT) // 6
    Q_PROPERTY(int radiusCard READ radiusCard CONSTANT)     // 8
    Q_PROPERTY(int radiusBar READ radiusBar CONSTANT)       // 2

    // ── Shell metrics (the V1 Fluent blended-titlebar + collapsible rail) ────
    Q_PROPERTY(int titleBarHeight READ titleBarHeight CONSTANT)         // 44
    Q_PROPERTY(int captionButtonWidth READ captionButtonWidth CONSTANT) // 46
    Q_PROPERTY(int railCompact READ railCompact CONSTANT)               // 48 (== hamburger cell)
    Q_PROPERTY(int railExpanded READ railExpanded CONSTANT)             // 236
    Q_PROPERTY(int navItemHeight READ navItemHeight CONSTANT)           // 40
    Q_PROPERTY(int hitRow READ hitRow CONSTANT)                         // 44 — comfortable row
    Q_PROPERTY(int dotSize READ dotSize CONSTANT)                       // 8 — status dot

    // The canonical "not actionable" treatment: the whole control at 0.4.
    Q_PROPERTY(qreal disabledOpacity READ disabledOpacity CONSTANT)

  public:
    explicit TokensBridge(QObject* parent = nullptr);

    int textStatus() const { return 17; }
    int textTitle() const { return 20; }
    int textHeading() const { return 16; }
    int textBase() const { return 13; }
    int textSummary() const { return 12; }
    int textMeta() const { return 11; }
    int textChip() const { return 10; }
    qreal sectionLetterSpacing() const { return 1.5; }
    QString monoFamily() const;

    int s1() const { return 2; }
    int s2() const { return 4; }
    int s3() const { return 6; }
    int s4() const { return 8; }
    int s5() const { return 10; }
    int s6() const { return 12; }
    int s7() const { return 14; }
    int s8() const { return 16; }
    int s9() const { return 20; }
    int pagePadding() const { return 24; }

    int radiusChip() const { return 5; }
    int radiusButton() const { return 6; }
    int radiusCard() const { return 8; }
    int radiusBar() const { return 2; }

    int titleBarHeight() const { return 44; }
    int captionButtonWidth() const { return 46; }
    int railCompact() const { return 48; }
    int railExpanded() const { return 236; }
    int navItemHeight() const { return 40; }
    int hitRow() const { return 44; }
    int dotSize() const { return 8; }

    qreal disabledOpacity() const { return 0.4; }
};

} // namespace dish::chrome
