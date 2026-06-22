// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// EventChannel<E> — a small bounded, fire-and-forget event channel: the C++
// analogue of a Kotlin `MutableSharedFlow(replay=0, extraBufferCapacity=N,
// onBufferOverflow=DROP_OLDEST)`. Unlike an Observable<S> it has NO retained
// value (no replay to late subscribers) and unlike a Qt signal it carries an
// explicit bounded buffer with DROP_OLDEST semantics for the case where there
// is no live collector to drain it.
//
// The architecture kernel deliberately has no event-channel primitive (a Source
// exposes state, not events) — only the two "rare event-emitting" classes own
// one (android: SatelliteConnectionManager, DishNotifications). This header is
// that idiom, scoped to source/notification where DishNotifications uses it; it
// is NOT a general kernel addition.
//
// Behaviour (mirrors SharedFlow DROP_OLDEST):
//   * push(e): if a subscriber is attached, deliver synchronously and DON'T
//     buffer (collected, like SharedFlow with an active collector). If no
//     subscriber is attached, queue onto the bounded buffer; once the buffer is
//     full the OLDEST queued event is dropped to make room. (Named push, not
//     emit, because Qt reserves `emit` as a macro.)
//   * subscribe(cb): attach the single collector and immediately drain any
//     buffered events to it in FIFO order, then deliver future pushes live.
//
// Single-collector (DishNotifications has exactly one renderer). Pure C++17,
// header-only, Qt-free; not thread-safe by itself (callers emit on the UI loop).

#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <utility>

namespace dish::source {

template <class E> class EventChannel {
  public:
    using Listener = std::function<void(const E&)>;

    explicit EventChannel(std::size_t capacity = 16) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Push an event. Delivered live if a listener is attached; otherwise queued
    // with DROP_OLDEST overflow.
    void push(E event) {
        if (listener_) {
            listener_(event);
            return;
        }
        if (buffer_.size() >= capacity_) { buffer_.pop_front(); }
        buffer_.push_back(std::move(event));
    }

    // Attach the (single) collector and drain the backlog to it in FIFO order.
    // Replaces any prior listener.
    void subscribe(Listener listener) {
        listener_ = std::move(listener);
        if (!listener_) { return; }
        while (!buffer_.empty()) {
            E event = std::move(buffer_.front());
            buffer_.pop_front();
            listener_(event);
        }
    }

    // Detach the collector; subsequent emits buffer again.
    void unsubscribe() { listener_ = nullptr; }

    // Diagnostics / tests: how many events are currently buffered.
    std::size_t buffered() const { return buffer_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool hasListener() const { return static_cast<bool>(listener_); }

  private:
    std::size_t capacity_;
    std::deque<E> buffer_;
    Listener listener_;
};

} // namespace dish::source
