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
    Q_PROPERTY(int textDisplay READ textDisplay CONSTANT) // 26 — mono display counts
    Q_PROPERTY(int textHero READ textHero CONSTANT)       // 21 — onboarding / donate headline
    Q_PROPERTY(int textStatus READ textStatus CONSTANT)   // 17 — header status line
    Q_PROPERTY(int textTitle READ textTitle CONSTANT)     // 20 — page title (design header)
    Q_PROPERTY(int textHeading READ textHeading CONSTANT) // 16 — dialog heading
    Q_PROPERTY(int textBase READ textBase CONSTANT)       // 13 — global base
    Q_PROPERTY(int textSummary READ textSummary CONSTANT) // 12 — muted helper text
    Q_PROPERTY(int textMeta READ textMeta CONSTANT)       // 11 — bound-to line, section label
    Q_PROPERTY(int textChip READ textChip CONSTANT)       // 10 — pills, telemetry footer
    // Tracked-out mono treatment for section labels / eyebrows.
    Q_PROPERTY(qreal sectionLetterSpacing READ sectionLetterSpacing CONSTANT) // 1.5
    // The two resolved families (ui::FontStacks probes them explicitly rather
    // than trusting the platform generic, which can hand back Courier New).
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)
    Q_PROPERTY(QString sansFamily READ sansFamily CONSTANT)

    // ── Spacing scale (px) ───────────────────────────────────────────────────
    // s0 is FLUSH, not "unset": a seam the design draws with a divider or a
    // colour change instead of a gap (the rail against the content column, a
    // card's stacked label rows, a control whose contentItem is positioned in
    // absolute cell coordinates). Named so those sites read as a decision and
    // the literal scanner has a token to point them at.
    Q_PROPERTY(int s0 READ s0 CONSTANT)                   // 0
    Q_PROPERTY(int s1 READ s1 CONSTANT)                   // 2
    Q_PROPERTY(int s2 READ s2 CONSTANT)                   // 4
    Q_PROPERTY(int s3 READ s3 CONSTANT)                   // 6
    Q_PROPERTY(int s4 READ s4 CONSTANT)                   // 8
    Q_PROPERTY(int s5 READ s5 CONSTANT)                   // 10
    Q_PROPERTY(int s6 READ s6 CONSTANT)                   // 12
    Q_PROPERTY(int s7 READ s7 CONSTANT)                   // 14
    Q_PROPERTY(int s8 READ s8 CONSTANT)                   // 16
    Q_PROPERTY(int s9 READ s9 CONSTANT)                   // 20
    Q_PROPERTY(int s10 READ s10 CONSTANT)                 // 24 — == pagePadding
    Q_PROPERTY(int s11 READ s11 CONSTANT)                 // 32
    Q_PROPERTY(int pagePadding READ pagePadding CONSTANT) // 24 — content side margins

    // ── Radii (px) ───────────────────────────────────────────────────────────
    Q_PROPERTY(int radiusChip READ radiusChip CONSTANT)     // 5
    Q_PROPERTY(int radiusButton READ radiusButton CONSTANT) // 6
    Q_PROPERTY(int radiusCard READ radiusCard CONSTANT)     // 8
    Q_PROPERTY(int radiusBar READ radiusBar CONSTANT)       // 2
    Q_PROPERTY(int radiusDialog READ radiusDialog CONSTANT) // 10

    // ── Glyph sizes (px) — the BrandGlyph rungs ──────────────────────────────
    Q_PROPERTY(int glyphSm READ glyphSm CONSTANT)     // 16
    Q_PROPERTY(int glyphMd READ glyphMd CONSTANT)     // 20
    Q_PROPERTY(int glyphLg READ glyphLg CONSTANT)     // 28
    Q_PROPERTY(int glyphXl READ glyphXl CONSTANT)     // 40
    Q_PROPERTY(int glyphHero READ glyphHero CONSTANT) // 76

    // ── Motion durations (ms) ────────────────────────────────────────────────
    Q_PROPERTY(int durFast READ durFast CONSTANT)     // 120 — hover/press tints
    Q_PROPERTY(int durNormal READ durNormal CONSTANT) // 200 — page/state moves
    Q_PROPERTY(int durBusy READ durBusy CONSTANT)     // 1100 — one indeterminate sweep
    Q_PROPERTY(int durToast READ durToast CONSTANT)   // 4000 — toast dwell

    // ── Shell metrics (the V1 Fluent blended-titlebar + collapsible rail) ────
    Q_PROPERTY(int titleBarHeight READ titleBarHeight CONSTANT)         // 44
    Q_PROPERTY(int captionButtonWidth READ captionButtonWidth CONSTANT) // 46
    Q_PROPERTY(int railCompact READ railCompact CONSTANT)               // 48 (== hamburger cell)
    Q_PROPERTY(int railExpanded READ railExpanded CONSTANT)             // 236
    Q_PROPERTY(int navItemHeight READ navItemHeight CONSTANT)           // 40
    Q_PROPERTY(int hitRow READ hitRow CONSTANT)                         // 44 — comfortable row
    Q_PROPERTY(int minTouch READ minTouch CONSTANT)                     // 32 — smallest hit target
    Q_PROPERTY(int dotSize READ dotSize CONSTANT)                       // 8 — status dot

    // ── Responsive breakpoints (CONTENT width, px) ───────────────────────────
    Q_PROPERTY(int minWindowWidth READ minWindowWidth CONSTANT)     // 900
    Q_PROPERTY(int minWindowHeight READ minWindowHeight CONSTANT)   // 620
    Q_PROPERTY(int narrowBreakpoint READ narrowBreakpoint CONSTANT) // 860 — banner compacts
    Q_PROPERTY(int stackBreakpoint READ stackBreakpoint CONSTANT)   // 760 — Home row stacks
    Q_PROPERTY(int wideBreakpoint READ wideBreakpoint CONSTANT)     // 980 — Settings two-column

    // The "not actionable" treatment for an AbstractButton: enabled:false plus
    // this opacity plus Theme.disabledFg. NOT legal on information — a drawn
    // but unavailable capability keeps full opacity and uses Theme.mutedStrong.
    Q_PROPERTY(qreal disabledOpacity READ disabledOpacity CONSTANT)

    // The OS "animate controls inside windows" preference, inverted. When true,
    // indeterminate bars render as a static filled track, glyph animations stop
    // and Behavior durations collapse to 0. Sampled in the ctor; re-sampled by
    // refreshMotionPreference().
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY reducedMotionChanged)

  public:
    explicit TokensBridge(QObject* parent = nullptr);

    int textDisplay() const { return 26; }
    int textHero() const { return 21; }
    int textStatus() const { return 17; }
    int textTitle() const { return 20; }
    int textHeading() const { return 16; }
    int textBase() const { return 13; }
    int textSummary() const { return 12; }
    int textMeta() const { return 11; }
    int textChip() const { return 10; }
    qreal sectionLetterSpacing() const { return 1.5; }
    QString monoFamily() const;
    QString sansFamily() const;

    int s0() const { return 0; }
    int s1() const { return 2; }
    int s2() const { return 4; }
    int s3() const { return 6; }
    int s4() const { return 8; }
    int s5() const { return 10; }
    int s6() const { return 12; }
    int s7() const { return 14; }
    int s8() const { return 16; }
    int s9() const { return 20; }
    int s10() const { return 24; }
    int s11() const { return 32; }
    int pagePadding() const { return 24; }

    int radiusChip() const { return 5; }
    int radiusButton() const { return 6; }
    int radiusCard() const { return 8; }
    int radiusBar() const { return 2; }
    int radiusDialog() const { return 10; }

    int glyphSm() const { return 16; }
    int glyphMd() const { return 20; }
    int glyphLg() const { return 28; }
    int glyphXl() const { return 40; }
    int glyphHero() const { return 76; }

    int durFast() const { return 120; }
    int durNormal() const { return 200; }
    int durBusy() const { return 1100; }
    int durToast() const { return 4000; }

    int titleBarHeight() const { return 44; }
    int captionButtonWidth() const { return 46; }
    int railCompact() const { return 48; }
    int railExpanded() const { return 236; }
    int navItemHeight() const { return 40; }
    int hitRow() const { return 44; }
    int minTouch() const { return 32; }
    int dotSize() const { return 8; }

    int minWindowWidth() const { return 900; }
    int minWindowHeight() const { return 620; }
    int narrowBreakpoint() const { return 860; }
    int stackBreakpoint() const { return 760; }
    int wideBreakpoint() const { return 980; }

    qreal disabledOpacity() const { return 0.55; }

    bool reducedMotion() const { return reducedMotion_; }
    // Re-sample SPI_GETCLIENTAREAANIMATION and emit on a real change (the shell
    // calls this when the window regains activation — Windows sends no broadcast
    // a Quick app can bind to without a native event filter).
    Q_INVOKABLE void refreshMotionPreference();

  signals:
    void reducedMotionChanged();

  private:
    bool reducedMotion_ = false;
};

} // namespace dish::chrome
