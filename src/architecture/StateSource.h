// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// StateSource<S> — the C++ analogue of dish-android's AbstractStateSource<S>:
// the owned-state primitive. Owns one Observable<S>, exposes it read-only via
// state(), and mutates only through protected setState. Lifecycle is opt-in
// via start()/stop() (there is no Android lifecycle on Windows — most
// process-scoped sources just live for the app's lifetime).

#pragma once

#include "architecture/Observable.h"

#include <functional>
#include <utility>

namespace dish::arch {

template <class S> class StateSource {
  public:
    explicit StateSource(S initial) : state_(std::move(initial)) {}
    StateSource(const StateSource&) = delete;
    StateSource& operator=(const StateSource&) = delete;
    virtual ~StateSource() = default;

    const Observable<S>& state() const { return state_; }

    virtual void start() {}
    virtual void stop() {}

  protected:
    void setState(S value) { state_.set(std::move(value)); }
    void setState(const std::function<S(const S&)>& reducer) { state_.update(reducer); }

  private:
    Observable<S> state_;
};

} // namespace dish::arch
