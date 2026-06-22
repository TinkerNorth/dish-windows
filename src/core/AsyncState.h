// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// AsyncState<T, E> — the canonical "an asynchronous value over time" container.
// The C++ analogue of the Resource/UiState<T> sealed class the sibling clients
// reach for: a single value that is one of { Idle, Loading, Success, Error },
// carrying the last-good data and (in Error) a typed reason.
//
// Why this exists: before this type, every async operation in the app modelled
// its lifecycle differently — a `bool busy`, a `QSet<id> inFlight`, an
// `std::optional<T>` that silently dropped failures, a one-shot errorMessage()
// toast. The UI could not bind "loading", could not show "why it failed", and
// could not tell "empty" from "still loading" from "errored". AsyncState makes
// all four states FIRST-CLASS and OBSERVABLE so a component binds every one and
// renders the right UX (spinner / error+retry / empty / content).
//
// Design rules (mirrors the UsbPathMachine doctrine — see core/reducer/):
//   * Pure & Qt-free: header-only, no QObject, no IO. `T` MAY be a Qt value type
//     (Observable already tolerates that); the transitions never touch Qt.
//   * Equality-comparable so it drops straight into Observable<AsyncState<T>>
//     and gets distinct-until-changed for free. `T` and `E` must be ==-comparable.
//   * Stale-while-revalidate by construction: toLoading()/toError() KEEP the
//     prior `data` and flag it `stale`, so a refresh shows a spinner OVER the
//     last good content and a failed refresh still shows the cached content with
//     an error chip — never a blank screen. This is the behaviour the catalog
//     repo hand-rolled; here it is a property of the type.
//   * Transitions are free functions (the "reducers" the UI binds through), so
//     they are unit-tested in isolation (test_async_state.cpp), exactly like the
//     (state,event) reducers in core/reducer/.
//
// `E` defaults to std::string (a ready-to-show detail) but callers SHOULD prefer
// a typed reason enum (e.g. AsyncState<CatalogDto, CatalogError>) so the reason
// is carried AS DATA and the UI localizes it — the same "render keys, not
// localized strings" rule the ConnectionsComposer follows.

#pragma once

#include <optional>
#include <string>
#include <utility>

namespace dish::core {

enum class AsyncPhase { Idle, Loading, Success, Error };

template <class T, class E = std::string> struct AsyncState {
    AsyncPhase phase = AsyncPhase::Idle;
    // The last good value. Present after the first Success and RETAINED across a
    // subsequent Loading/Error (stale-while-revalidate). Absent only before the
    // first successful load.
    std::optional<T> data;
    // The failure reason; populated iff phase == Error.
    std::optional<E> error;
    // True when `data` is carried over from a PRIOR success while phase is
    // Loading (refreshing) or Error (served-on-failure) — i.e. the data is not
    // freshly confirmed this cycle. Always false in a fresh Success.
    bool stale = false;

    // ── Convenience predicates (so bindings read intent, not enum compares) ──
    bool isIdle() const { return phase == AsyncPhase::Idle; }
    bool isLoading() const { return phase == AsyncPhase::Loading; }
    bool isSuccess() const { return phase == AsyncPhase::Success; }
    bool isError() const { return phase == AsyncPhase::Error; }
    bool hasData() const { return data.has_value(); }

    const T& valueOr(const T& fallback) const { return data ? *data : fallback; }

    bool operator==(const AsyncState& o) const {
        return phase == o.phase && data == o.data && error == o.error && stale == o.stale;
    }
    bool operator!=(const AsyncState& o) const { return !(*this == o); }
};

// ── Pure transitions (the canonical state moves) ─────────────────────────────
//
// Each takes the previous state and returns the next — value semantics, no
// mutation, trivially testable, and the natural argument to Observable::update.

// The empty starting point — nothing requested yet.
template <class T, class E = std::string> AsyncState<T, E> asyncIdle() {
    return AsyncState<T, E>{};
}

// Enter Loading, keeping any prior data (marked stale) so a refresh overlays a
// spinner on the last good content rather than blanking it. Clears any error.
template <class T, class E> AsyncState<T, E> toLoading(const AsyncState<T, E>& prev) {
    AsyncState<T, E> next;
    next.phase = AsyncPhase::Loading;
    next.data = prev.data;
    next.error = std::nullopt;
    next.stale = prev.data.has_value();
    return next;
}

// Loading from cold (drop any prior data) — for an operation whose new run
// invalidates the old value (e.g. a fresh discovery scan).
template <class T, class E = std::string> AsyncState<T, E> toLoadingFresh() {
    AsyncState<T, E> next;
    next.phase = AsyncPhase::Loading;
    return next;
}

// Succeed with a confirmed value. Clears error and the stale flag.
template <class T, class E> AsyncState<T, E> toSuccess(const AsyncState<T, E>& /*prev*/, T value) {
    AsyncState<T, E> next;
    next.phase = AsyncPhase::Success;
    next.data = std::move(value);
    next.error = std::nullopt;
    next.stale = false;
    return next;
}

// Succeed but re-serving a prior/cached value (e.g. an HTTP 304 Not-Modified):
// Success phase, but `stale` stays true to signal the data wasn't re-fetched.
template <class T, class E> AsyncState<T, E> toRevalidated(const AsyncState<T, E>& prev) {
    AsyncState<T, E> next;
    next.phase = AsyncPhase::Success;
    next.data = prev.data;
    next.error = std::nullopt;
    next.stale = true;
    return next;
}

// Fail with a typed reason. KEEPS any prior data (served stale) so the UI can
// show "couldn't refresh — showing last known" instead of going blank.
template <class T, class E> AsyncState<T, E> toError(const AsyncState<T, E>& prev, E reason) {
    AsyncState<T, E> next;
    next.phase = AsyncPhase::Error;
    next.data = prev.data;
    next.error = std::move(reason);
    next.stale = prev.data.has_value();
    return next;
}

} // namespace dish::core
