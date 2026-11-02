// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "core/thread/TrackedMutex.h"

#include <cstdint>
#include <optional>

namespace dish::source {

enum class PriorityRequest : std::uint8_t {
    None,
    Games,
};

enum class RaisedPriority : std::uint8_t {
    None,
    GamesTask,
    TimeCritical,
};

constexpr PriorityRequest priorityRequestFor(thread::ThreadRole role) {
    switch (role) {
    case thread::ThreadRole::SdlInput:
    case thread::ThreadRole::UsbReader:
    case thread::ThreadRole::Receive:
        return PriorityRequest::Games;
    case thread::ThreadRole::Gui:
    case thread::ThreadRole::Heartbeat:
    case thread::ThreadRole::Playout:
    case thread::ThreadRole::AudioCapture:
    case thread::ThreadRole::MoonlightRx:
    case thread::ThreadRole::Update:
    case thread::ThreadRole::Other:
        break;
    }
    return PriorityRequest::None;
}

class SchedulerGateway {
  public:
    virtual ~SchedulerGateway() = default;

    virtual void* joinGamesTask() = 0;
    virtual void leaveGamesTask(void* task) = 0;
    virtual std::optional<int> threadPriority() = 0;
    virtual bool setThreadPriority(int priority) = 0;
    virtual bool beginTimerPeriod(unsigned periodMs) = 0;
    virtual void endTimerPeriod(unsigned periodMs) = 0;
};

class WindowsSchedulerGateway final : public SchedulerGateway {
  public:
    void* joinGamesTask() override;
    void leaveGamesTask(void* task) override;
    std::optional<int> threadPriority() override;
    bool setThreadPriority(int priority) override;
    bool beginTimerPeriod(unsigned periodMs) override;
    void endTimerPeriod(unsigned periodMs) override;
};

// AvRevertMmThreadCharacteristics fails on any thread but the one that joined the task, so a guard
// never leaves the stack of the thread it raised.
class ThreadPriorityGuard {
  public:
    ThreadPriorityGuard(thread::ThreadRole role, SchedulerGateway& scheduler);
    ~ThreadPriorityGuard();
    ThreadPriorityGuard(const ThreadPriorityGuard&) = delete;
    ThreadPriorityGuard& operator=(const ThreadPriorityGuard&) = delete;
    ThreadPriorityGuard(ThreadPriorityGuard&&) = delete;
    ThreadPriorityGuard& operator=(ThreadPriorityGuard&&) = delete;

    RaisedPriority raised() const { return held_.raised; }

  private:
    struct Held {
        RaisedPriority raised = RaisedPriority::None;
        void* task = nullptr;
        int previousPriority = 0;
    };
    static Held raise(PriorityRequest request, SchedulerGateway& scheduler);

    SchedulerGateway& scheduler_;
    const Held held_;
};

class ScopedThreadPriority {
  public:
    explicit ScopedThreadPriority(thread::ThreadRole role) : guard_(role, scheduler_) {}

    RaisedPriority raised() const { return guard_.raised(); }

  private:
    // Declared before guard_, which calls it until guard_'s own destructor has run.
    WindowsSchedulerGateway scheduler_;
    ThreadPriorityGuard guard_;
};

class TimerPeriodGuard {
  public:
    TimerPeriodGuard(unsigned periodMs, SchedulerGateway& scheduler);
    ~TimerPeriodGuard();
    TimerPeriodGuard(const TimerPeriodGuard&) = delete;
    TimerPeriodGuard& operator=(const TimerPeriodGuard&) = delete;
    TimerPeriodGuard(TimerPeriodGuard&&) = delete;
    TimerPeriodGuard& operator=(TimerPeriodGuard&&) = delete;

    bool held() const { return held_; }

  private:
    SchedulerGateway& scheduler_;
    const unsigned periodMs_;
    const bool held_;
};

} // namespace dish::source
