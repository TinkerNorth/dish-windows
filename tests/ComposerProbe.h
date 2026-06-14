// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Test probe for Composer/Combiner output: captures the derived-state emission
// sequence, mirroring dish-android's ComposerProbe. `fakeUpstream` builds a
// settable Observable to inject as a combiner input.

#pragma once

#include "architecture/Observable.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace dish::test {

template <class S> class ComposerProbe {
  public:
    explicit ComposerProbe(const dish::arch::Observable<S>& observable) {
        subscription_ =
            observable.subscribe([this](const S& value) { states_.push_back(value); }, true);
    }

    const std::vector<S>& states() const { return states_; }
    const S& latest() const { return states_.back(); }
    std::size_t count() const { return states_.size(); }

  private:
    std::vector<S> states_;
    typename dish::arch::Observable<S>::Subscription subscription_;
};

template <class S> dish::arch::Observable<S> fakeUpstream(S initial) {
    return dish::arch::Observable<S>(std::move(initial));
}

} // namespace dish::test
