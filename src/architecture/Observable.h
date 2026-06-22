// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Observable<S> — the C++ analogue of Kotlin's StateFlow<S> used by
// dish-android. A value holder that always has a current value and notifies
// subscribers when it changes (distinct-until-changed via operator==).
//
// Deliberately Qt-free (callback-list core, no QObject/moc): the architecture
// kernel is pure C++17 so core/ logic and its tests never depend on Qt. A thin
// QObject adapter for widget binding can be added in ui/ later if needed.
//
// See architecture/README.md and migration-plan/analysis/android-architecture.md.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace dish::arch {

template <class S> class Observable {
    struct Impl {
        explicit Impl(S v) : value(std::move(v)) {}
        mutable std::mutex mutex;
        S value;
        std::uint64_t nextToken = 1; // 0 is reserved for "no subscription"
        std::map<std::uint64_t, std::function<void(const S&)>> subscribers;
    };

  public:
    // RAII unsubscribe handle. Move-only. Safe whether it outlives or
    // predeceases the Observable — it holds only a weak reference to the
    // shared state.
    class Subscription {
      public:
        Subscription() = default;
        Subscription(std::weak_ptr<Impl> impl, std::uint64_t token)
            : impl_(std::move(impl)), token_(token) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept { moveFrom(other); }
        Subscription& operator=(Subscription&& other) noexcept {
            if (this != &other) {
                reset();
                moveFrom(other);
            }
            return *this;
        }
        ~Subscription() { reset(); }

        void reset() {
            if (token_ != 0) {
                if (auto impl = impl_.lock()) {
                    std::lock_guard<std::mutex> lock(impl->mutex);
                    impl->subscribers.erase(token_);
                }
            }
            impl_.reset();
            token_ = 0;
        }

      private:
        void moveFrom(Subscription& other) {
            impl_ = std::move(other.impl_);
            token_ = other.token_;
            other.impl_.reset();
            other.token_ = 0;
        }
        std::weak_ptr<Impl> impl_;
        std::uint64_t token_ = 0;
    };

    explicit Observable(S initial) : impl_(std::make_shared<Impl>(std::move(initial))) {}

    Observable(const Observable&) = delete;
    Observable& operator=(const Observable&) = delete;
    Observable(Observable&&) noexcept = default;
    Observable& operator=(Observable&&) noexcept = default;
    ~Observable() = default;

    // Current value (copied under the lock).
    S value() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->value;
    }

    // Set a new value; notifies subscribers only if it differs (== compare).
    // Subscribers are invoked outside the lock so a callback may re-enter.
    void set(S next) {
        std::vector<std::function<void(const S&)>> toNotify;
        S snapshot;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->value == next) { return; }
            impl_->value = std::move(next);
            snapshot = impl_->value;
            toNotify.reserve(impl_->subscribers.size());
            for (const auto& entry : impl_->subscribers) { toNotify.push_back(entry.second); }
        }
        for (const auto& callback : toNotify) { callback(snapshot); }
    }

    // Read-modify-write convenience: set(reducer(current)). The reducer runs
    // outside the lock.
    void update(const std::function<S(const S&)>& reducer) { set(reducer(value())); }

    // Logically const: subscribing does not change the observed value, so a
    // Controller holding a `const Observable<S>&` can still subscribe.
    // emitCurrent=true mirrors StateFlow replaying the latest to new collectors.
    Subscription subscribe(std::function<void(const S&)> callback, bool emitCurrent = true) const {
        std::uint64_t token = 0;
        S snapshot;
        std::function<void(const S&)> immediate = callback;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            token = impl_->nextToken++;
            impl_->subscribers.emplace(token, std::move(callback));
            snapshot = impl_->value;
        }
        if (emitCurrent) { immediate(snapshot); }
        return Subscription(impl_, token);
    }

  private:
    std::shared_ptr<Impl> impl_;
};

} // namespace dish::arch
