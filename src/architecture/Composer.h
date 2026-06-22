// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Composer<Out, Ins...> — vocabulary for the AbstractComposer role: a class
// that PURELY derives one Observable<Out> from upstream Observables via a pure
// transform, with no IO, events, or lifecycle of its own. It is a Combiner
// under the architectural name; subclass it for a named composer, or own a
// Combiner directly.

#pragma once

#include "architecture/Combiner.h"

namespace dish::arch {

template <class Out, class... Ins> class Composer : public Combiner<Out, Ins...> {
  public:
    using Combiner<Out, Ins...>::Combiner;
};

} // namespace dish::arch
