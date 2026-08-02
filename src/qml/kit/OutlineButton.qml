// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The SECONDARY / outlined button — now a one-line alias over DishButton so the
// existing call sites (Forget, Cancel, Manage) keep working unchanged. New code
// writes `DishButton { variant: DishButton.Outline }` directly; a destructive
// action writes `DishButton { variant: DishButton.Destructive }` rather than an
// inline red.
//
// No imports: DishButton lives in this directory and resolves through the
// implicit directory import; adding one would be an unused-import lint failure.

DishButton {
    variant: DishButton.Outline
}
