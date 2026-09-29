// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// WifiConnectionManager's effects on virtual time. A delayed call waits until
// the test advances the clock past it; a scan is counted and answered only when
// the test says so; every UDP link refuses to open, and is counted. Nothing
// here touches a socket or a thread, so a behaviour that regresses into
// "retry" or "rescan" shows up as a count, at once, instead of real traffic.

#pragma once

#include "Network/SatelliteClient.h"
#include "Network/WifiManagerEffects.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dish::test {

class FakeWifiEffects {
  public:
    FakeWifiEffects() = default;
    FakeWifiEffects(const FakeWifiEffects&) = delete;
    FakeWifiEffects& operator=(const FakeWifiEffects&) = delete;
    FakeWifiEffects(FakeWifiEffects&&) = delete;
    FakeWifiEffects& operator=(FakeWifiEffects&&) = delete;
    ~FakeWifiEffects() = default;

    // The manager's copy calls back into this object, which must outlive it.
    net::WifiManagerEffects effects() {
        net::WifiManagerEffects e;
        e.after = [this](int delayMs, QObject* context, net::WifiManagerEffects::Callback fn) {
            schedule(delayMs, context, std::move(fn));
        };
        e.scan = [this](QObject* context, net::WifiManagerEffects::ScanDone done) {
            scans_.push_back(PendingScan{QPointer<QObject>(context), std::move(done)});
        };
        e.openLink = [this](const std::string&, int) {
            ++linkAttempts_;
            return std::shared_ptr<net::SatelliteClient>();
        };
        return e;
    }

    // Runs every delayed call that falls due in the next `ms`, in due order.
    void advance(std::int64_t ms) {
        const std::int64_t until = nowMs_ + ms;
        while (true) {
            const auto next = nextDue(until);
            if (next == pending_.end()) { break; }
            PendingCall call = std::move(*next);
            pending_.erase(next);
            nowMs_ = call.dueMs;
            if (!call.context.isNull()) { call.fn(); }
        }
        nowMs_ = until;
    }

    // Delayed calls still waiting whose context is alive.
    long long pendingCalls() const {
        long long live = 0;
        for (const auto& call : pending_) {
            if (!call.context.isNull()) { ++live; }
        }
        return live;
    }

    long long scansStarted() const { return static_cast<long long>(scans_.size()); }

    // Answers every scan started so far with `found`.
    void finishScans(const QList<models::DiscoveredServer>& found) {
        auto waiting = std::move(scans_);
        scans_.clear();
        for (auto& scan : waiting) {
            if (!scan.context.isNull()) { scan.done(found); }
        }
    }

    long long linkAttempts() const { return linkAttempts_; }

  private:
    struct PendingCall {
        std::int64_t dueMs = 0;
        QPointer<QObject> context;
        net::WifiManagerEffects::Callback fn;
    };
    struct PendingScan {
        QPointer<QObject> context;
        net::WifiManagerEffects::ScanDone done;
    };

    void schedule(int delayMs, QObject* context, net::WifiManagerEffects::Callback fn) {
        pending_.push_back(
            PendingCall{nowMs_ + delayMs, QPointer<QObject>(context), std::move(fn)});
    }

    std::vector<PendingCall>::iterator nextDue(std::int64_t until) {
        auto best = pending_.end();
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            const bool isDue = it->dueMs <= until;
            const bool isEarlier = best == pending_.end() || it->dueMs < best->dueMs;
            if (isDue && isEarlier) { best = it; }
        }
        return best;
    }

    std::int64_t nowMs_ = 0;
    std::vector<PendingCall> pending_;
    std::vector<PendingScan> scans_;
    long long linkAttempts_ = 0;
};

} // namespace dish::test
