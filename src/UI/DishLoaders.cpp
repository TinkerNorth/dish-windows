// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "DishLoaders.h"

#include "Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <array>
#include <cmath>

namespace dish::ui {

namespace {

// Frame interval driving every loader. 16 ms ~= 60 fps, plenty for the
// human eye and well within Qt's repaint cadence.
constexpr int kFrameIntervalMs = 16;

// Spec design canvas is 64x64 design units; every proportion divides
// off that constant. Keeping the magic numbers here as named constants
// matches the JSX source and dish-mac/UI/DishLoaders.swift.
constexpr qreal kDesignCanvas = 64.0;

} // namespace

// MARK: - Spinner ------------------------------------------------------

DishSpinnerWidget::DishSpinnerWidget(int size, QWidget* parent)
    : QWidget(parent), diameter_(size), anim_(new QVariantAnimation(this)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(diameter_, diameter_);
    // 0 -> 360 degrees over 1.2 s, restart forever. Linear easing matches
    // the SMIL `<animateTransform>` semantics (no acceleration).
    anim_->setStartValue(qreal{0.0});
    anim_->setEndValue(qreal{360.0});
    anim_->setDuration(1200);
    anim_->setLoopCount(-1);
    anim_->setEasingCurve(QEasingCurve::Linear);
    QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) {
                         angle_ = v.toReal();
                         update();
                     });
}

DishSpinnerWidget::~DishSpinnerWidget() = default;

void DishSpinnerWidget::start() {
    if (anim_->state() != QAbstractAnimation::Running) { anim_->start(); }
}

void DishSpinnerWidget::stop() {
    anim_->stop();
}

void DishSpinnerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    start();
}

void DishSpinnerWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    stop();
}

void DishSpinnerWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal size = static_cast<qreal>(diameter_);
    const qreal stroke = size * (6.0 / kDesignCanvas);
    // Inset by half the stroke so the stroke sits inside the widget bounds
    // rather than getting clipped by the edges.
    const qreal half = stroke / 2.0;
    const QRectF ring(half, half, size - stroke, size - stroke);

    const QColor primary(Theme::primary);

    // Background ring at 25 % alpha — the resting state behind the arc.
    QColor faint = primary;
    faint.setAlphaF(0.25);
    QPen bgPen(faint, stroke);
    bgPen.setCapStyle(Qt::FlatCap);
    p.setPen(bgPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ring);

    // Foreground arc — dasharray "50 88" -> 50 / 138 ~= 36.2 % of the
    // circumference is drawn, the rest is gap. QPainter::drawArc takes
    // angles in 1/16-degree units; 360 deg = 5760 units, so the visible
    // arc spans (50 / 138) * 5760 ~= 2087 units. Start angle is the
    // animated value (in deg). The minus sign inverts Qt's
    // counter-clockwise default so the arc rotates clockwise, matching
    // the SMIL spec.
    QPen arcPen(primary, stroke);
    arcPen.setCapStyle(Qt::RoundCap);
    p.setPen(arcPen);
    constexpr qreal kArcFraction = 50.0 / 138.0;
    const int spanUnits = static_cast<int>(std::lround(kArcFraction * 5760.0));
    const int startUnits = static_cast<int>(std::lround(-angle_ * 16.0));
    p.drawArc(ring, startUnits, -spanUnits);
}

// MARK: - Dots ---------------------------------------------------------

DishDotsWidget::DishDotsWidget(int size, QWidget* parent)
    : QWidget(parent), diameter_(size), tick_(new QTimer(this)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(diameter_, diameter_);
    tick_->setInterval(kFrameIntervalMs);
    QObject::connect(tick_, &QTimer::timeout, this, [this] { update(); });
}

DishDotsWidget::~DishDotsWidget() = default;

void DishDotsWidget::start() {
    if (!tick_->isActive()) {
        startMs_ = QDateTime::currentMSecsSinceEpoch();
        tick_->start();
    }
}

void DishDotsWidget::stop() {
    tick_->stop();
}

void DishDotsWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    start();
}

void DishDotsWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    stop();
}

void DishDotsWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Elapsed in seconds since the widget last became visible.
    const qreal now = (QDateTime::currentMSecsSinceEpoch() - startMs_) / 1000.0;

    const qreal scale = static_cast<qreal>(diameter_) / kDesignCanvas;
    const QColor primary(Theme::primary);

    // Three dots at design-x 16 / 32 / 48, design-y 32.
    constexpr std::array<qreal, 3> kCx{16.0, 32.0, 48.0};
    constexpr qreal kCy = 32.0;
    constexpr qreal kCycle = 1.2;        // seconds
    constexpr qreal kStaggerStep = 0.18; // seconds between dots

    for (int i = 0; i < 3; ++i) {
        // Phase in [0, 1) over a 1.2 s cycle, staggered `i * 0.18 s`.
        qreal raw = std::fmod(now + static_cast<qreal>(i) * kStaggerStep, kCycle);
        if (raw < 0.0) { raw += kCycle; }
        const qreal phase = raw / kCycle;
        // SMIL `values="A;B;A"` is linear interpolation -> triangle wave.
        // 0 at the cycle edges, 1 at the midpoint.
        const qreal tri = 1.0 - std::abs(phase - 0.5) * 2.0;
        const qreal opacity = 0.25 + 0.75 * tri;        // 0.25 -> 1.0
        const qreal r = scale * (4.0 + 2.0 * tri);      // 4 -> 6 design units
        QColor c = primary;
        c.setAlphaF(opacity);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const QPointF center(kCx[static_cast<std::size_t>(i)] * scale, kCy * scale);
        p.drawEllipse(center, r, r);
    }
}

// MARK: - Bar ----------------------------------------------------------

DishBarWidget::DishBarWidget(int width, QWidget* parent)
    : QWidget(parent), barWidth_(width), anim_(new QVariantAnimation(this)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedHeight(barHeight());
    setMinimumWidth(barWidth_);
    // Phase 0..1 over 1.4 s, restart forever. Mapped to slider x in the
    // paint event so a resize doesn't disrupt the animation timing.
    anim_->setStartValue(qreal{0.0});
    anim_->setEndValue(qreal{1.0});
    anim_->setDuration(1400);
    anim_->setLoopCount(-1);
    anim_->setEasingCurve(QEasingCurve::Linear);
    QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) {
                         phase_ = v.toReal();
                         update();
                     });
}

DishBarWidget::~DishBarWidget() = default;

void DishBarWidget::start() {
    if (anim_->state() != QAbstractAnimation::Running) { anim_->start(); }
}

void DishBarWidget::stop() {
    anim_->stop();
}

void DishBarWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    start();
}

void DishBarWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    stop();
}

int DishBarWidget::barHeight() const {
    // Spec: 240 design width, 16 design overall height (track is 8u centered
    // vertically). At target px width we keep the same proportion.
    return std::max(2, static_cast<int>(std::round(barWidth_ * (16.0 / 240.0))));
}

int DishBarWidget::sliderPxWidth() const {
    return static_cast<int>(std::round(barWidth_ * (80.0 / 240.0)));
}

void DishBarWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal w = static_cast<qreal>(barWidth_);
    // Track height: 8/240 of the width. Centered vertically in the widget.
    const qreal trackH = w * (8.0 / 240.0);
    const qreal trackY = (static_cast<qreal>(height()) - trackH) / 2.0;
    const qreal radius = trackH / 2.0;

    const QColor primary(Theme::primary);
    // Track at 22 % alpha — the resting backdrop.
    QColor track = primary;
    track.setAlphaF(0.22);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(QRectF(0, trackY, w, trackH), radius, radius);

    // Slider: 80/240 wide; x runs from `-sliderWidth` to `width` so the
    // bright pip enters from the left and exits to the right. We clip to
    // the track bounds so the slider never spills past the widget edges.
    const qreal sliderW = w * (80.0 / 240.0);
    const qreal x = -sliderW + (w + sliderW) * phase_;
    p.save();
    p.setClipRect(QRectF(0, trackY, w, trackH));
    p.setBrush(primary);
    p.drawRoundedRect(QRectF(x, trackY, sliderW, trackH), radius, radius);
    p.restore();
}

// MARK: - DishLoaderButton ---------------------------------------------

DishLoaderButton::DishLoaderButton(const QString& idleText, QWidget* parent)
    : QPushButton(idleText, parent), idleText_(idleText) {
    // Inner layout: 6 px spinner-label gap matches the dish-mac
    // `HStack(spacing: 6)`. Vertical margins follow the QSS padding of the
    // outlined-button rule (`padding: 6px 12px`) — kept zero here because
    // the button's own padding still applies around the layout.
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(12, 0, 12, 0);
    layout_->setSpacing(6);
    layout_->addStretch(1);
    spinner_ = new DishSpinnerWidget(12, this);
    spinner_->setVisible(false);
    layout_->addWidget(spinner_, 0, Qt::AlignVCenter);
    label_ = new QLabel(this);
    label_->setVisible(false);
    // Inherit the button's font so the inline label reads identically to
    // the native QPushButton text it temporarily replaces.
    label_->setAlignment(Qt::AlignCenter);
    label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout_->addWidget(label_, 0, Qt::AlignVCenter);
    layout_->addStretch(1);
    applyDisabledOpacityEffect(this);
}

void DishLoaderButton::setInFlight(bool inFlight, const QString& inFlightText) {
    inFlight_ = inFlight;
    if (inFlight && !inFlightText.isEmpty()) { label_->setText(inFlightText); }
    rebuildVisual();
}

void DishLoaderButton::setIdleEnabled(bool enabled) {
    idleEnabled_ = enabled;
    // While in-flight, the visible enabled state stays false. Otherwise it
    // reflects the caller's intent immediately.
    if (!inFlight_) { setEnabled(enabled); }
}

void DishLoaderButton::rebuildVisual() {
    if (inFlight_) {
        // While in-flight: empty native text (so the button does not paint
        // its own label on top of the inline label), show the inline
        // spinner+label, force-disable. The QSS `:disabled` selector +
        // the QGraphicsOpacityEffect already dim the whole control to the
        // canonical 0.4 alpha.
        if (label_->text().isEmpty()) { label_->setText(idleText_); }
        QPushButton::setText(QString());
        spinner_->setVisible(true);
        label_->setVisible(true);
        setEnabled(false);
    } else {
        spinner_->setVisible(false);
        label_->setVisible(false);
        QPushButton::setText(idleText_);
        setEnabled(idleEnabled_);
    }
}

} // namespace dish::ui
