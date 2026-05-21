// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QWidget>

#include <QPushButton>

class QHBoxLayout;
class QLabel;
class QTimer;
class QVariantAnimation;

namespace dish::ui {

// Three indeterminate progress indicators ported from the design spec in
// `app-icon/project/app-essentials.jsx` (sections `LoaderSpinner`,
// `LoaderDots`, `LoaderBar`). Proportions (stroke width, dasharray ratio,
// timings) are pixel-faithful to the spec; the color is `Theme::primary`
// rather than the spec's hard-coded `#8FCFE3` — every Dish client uses
// its own brand cyan (see `Theme.h`) but every geometric rule of the spec
// is preserved.
//
// Implementation choice: QPainter + QVariantAnimation rather than an
// animated SVG. Qt's QSvgRenderer in 6.x parses but does not fully execute
// SMIL `<animate>` / `<animateTransform>` tags, so the SVG loaders ship
// frozen rather than animated. A QPainter draw driven by a variant
// animation reproduces the spec's proportions exactly and renders at
// device-pixel resolution without rasterising an SVG every frame.
//
// Mirrors dish-mac/UI/DishLoaders.swift.
//
// Use:
//   * `DishSpinnerWidget` — default for short, bounded waits (Scan, Pair,
//     Connect — anything backed by a single network round-trip).
//   * `DishDotsWidget`    — "thinking" states (chat, search suggestions).
//   * `DishBarWidget`     — whole-area / pane-level loading.

// MARK: - Spinner -------------------------------------------------------

// Indeterminate rotating arc. Spec reference: `LoaderSpinner`.
//
// - 64x64 design canvas -> stroke width is `size * 6/64`.
// - Background ring sits at 25 % alpha, full arc on top.
// - Visible arc is `strokeDasharray="50 88"` of the circumference,
//   i.e. 50 / 138 ~= 36.2 % of the ring (the rest is gap).
// - 1.2 s linear rotation, indefinite.
//
// Sized small enough by default (16 px) to drop inside a button next to
// its label.
class DishSpinnerWidget : public QWidget {
    Q_OBJECT
  public:
    explicit DishSpinnerWidget(int size = 16, QWidget* parent = nullptr);
    ~DishSpinnerWidget() override;

    QSize sizeHint() const override { return QSize(diameter_, diameter_); }
    QSize minimumSizeHint() const override { return sizeHint(); }

    // Pause / resume the rotation. The widget auto-starts on show and stops
    // on hide so a hidden loader doesn't burn CPU.
    void start();
    void stop();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    int diameter_;
    QVariantAnimation* anim_;
    qreal angle_ = 0.0;
};

// MARK: - Dots ----------------------------------------------------------

// Three pulsing circles. Spec reference: `LoaderDots`.
//
// Each dot oscillates in opacity (0.25 <-> 1) and radius (4 <-> 6 design
// units) on a 1.2 s cycle, staggered 0.18 s between dots. The original
// SMIL `<animate values="A;B;A" dur="1.2s">` is a linear interpolation
// that produces a triangle wave — mirrored here directly so the visual
// matches.
class DishDotsWidget : public QWidget {
    Q_OBJECT
  public:
    explicit DishDotsWidget(int size = 16, QWidget* parent = nullptr);
    ~DishDotsWidget() override;

    QSize sizeHint() const override { return QSize(diameter_, diameter_); }
    QSize minimumSizeHint() const override { return sizeHint(); }

    void start();
    void stop();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    int diameter_;
    QTimer* tick_;
    qint64 startMs_ = 0;
};

// MARK: - Bar -----------------------------------------------------------

// Indeterminate horizontal bar — an 80-unit-wide highlight slides across a
// 240-unit track on a 1.4 s linear cycle. Spec reference: `LoaderBar`.
//
// The slider starts off-screen at `x = -80` and ends off-screen at
// `x = 240`, so the bright pip animates fully through the visible area.
class DishBarWidget : public QWidget {
    Q_OBJECT
  public:
    explicit DishBarWidget(int width = 240, QWidget* parent = nullptr);
    ~DishBarWidget() override;

    QSize sizeHint() const override { return QSize(barWidth_, barHeight()); }
    QSize minimumSizeHint() const override { return sizeHint(); }

    void start();
    void stop();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    int barHeight() const;
    int sliderPxWidth() const;

    int barWidth_;
    QVariantAnimation* anim_;
    qreal phase_ = 0.0;
};

// MARK: - Loader-aware button ------------------------------------------

// A QPushButton that, when set "in-flight", shows a `DishSpinnerWidget`
// next to a label inside its own footprint and goes disabled. Mirrors the
// dish-mac pattern in `ConnectionsView.swift` / `PairingSheet.swift`:
//
//   Button { HStack { DishSpinner(); Text("Pairing…") } }
//     .disabled(isPairing)
//
// Qt's QPushButton does not natively support a child layout that survives
// alongside its own text paint, so the implementation keeps the button's
// `text` empty while in-flight and draws the spinner + label via an
// inner QHBoxLayout. When the button is idle the layout is hidden and the
// button reverts to its standard text paint — so existing QSS rules
// (`QPushButton#primary`, hover, pressed) still apply.
//
// Also installs `applyDisabledOpacityEffect` so the canonical Dish
// design-system 0.4 disabled opacity rule applies even when the button is
// disabled for reasons other than being in-flight.
class DishLoaderButton : public QPushButton {
    Q_OBJECT
  public:
    explicit DishLoaderButton(const QString& idleText, QWidget* parent = nullptr);

    // Show / hide the inline spinner. The button is force-disabled while
    // in-flight; on clear, its enabled state is restored to whatever the
    // caller last requested via `setIdleEnabled`. `inFlightText` is shown
    // next to the spinner; passing an empty string keeps the previous one.
    void setInFlight(bool inFlight, const QString& inFlightText = QString());

    // Sets the enabled state the button should revert to when not in-flight.
    // Use this in place of `setEnabled` for any control that participates in
    // the in-flight lifecycle so the in-flight forced-disable doesn't get
    // overwritten by an enabled toggle from elsewhere.
    void setIdleEnabled(bool enabled);

  private:
    void rebuildVisual();

    QString idleText_;
    bool inFlight_ = false;
    bool idleEnabled_ = true;
    QHBoxLayout* layout_ = nullptr;
    DishSpinnerWidget* spinner_ = nullptr;
    QLabel* label_ = nullptr;
};

} // namespace dish::ui
