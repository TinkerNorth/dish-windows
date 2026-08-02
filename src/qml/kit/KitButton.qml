// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The PRIMARY action button — now a one-line alias over DishButton so the
// existing call sites (Scan, Pair, Bind…) keep working unchanged. New code
// writes `DishButton { variant: DishButton.Primary }` directly.
//
// No imports: DishButton lives in this directory and resolves through the
// implicit directory import; adding one would be an unused-import lint failure.

DishButton {
    variant: DishButton.Primary
}
