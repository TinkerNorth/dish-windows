// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Controller<S> — the C++ analogue of AbstractController<S>: the side-effect
// actuator, mirror image of a Composer. Subscribes to one upstream Observable
// and runs apply() on each value (including the current value on start).
// start() is idempotent; teardown is deliberate — a subclass may override
// stop() to survive (e.g. a crash-reporting opt-in that outlives a screen).

#pragma once

#include "architecture/Observable.h"

namespace dish::arch {

template <class S> class Controller {
  public:
    explicit Controller(const Observable<S>& upstream) : upstream_(&upstream) {}
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    virtual ~Controller() = default;

    // Idempotent: a second start() while already running is a no-op.
    void start() {
        if (started_) { return; }
        started_ = true;
        onStarting();
        subscription_ = upstream_->subscribe([this](const S& value) { apply(value); },
                                             /*emitCurrent=*/true);
    }

    virtual void stop() { cancelCollection(); }

  protected:
    virtual void apply(const S& value) = 0;

    // Runs once per (re)start, after the idempotency guard and before the
    // subscription is established — a hook for re-arming post-stop state.
    virtual void onStarting() {}

    void cancelCollection() {
        subscription_.reset();
        started_ = false;
    }

    const Observable<S>& upstream() const { return *upstream_; }

  private:
    const Observable<S>* upstream_;
    typename Observable<S>::Subscription subscription_;
    bool started_ = false;
};

} // namespace dish::arch
