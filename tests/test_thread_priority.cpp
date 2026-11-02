// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/ThreadPriority.h"

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using dish::source::PriorityRequest;
using dish::source::priorityRequestFor;
using dish::source::RaisedPriority;
using dish::source::SchedulerGateway;
using dish::source::ScopedThreadPriority;
using dish::source::ThreadPriorityGuard;
using dish::source::TimerPeriodGuard;
using dish::source::WindowsSchedulerGateway;
using dish::thread::ThreadRole;

namespace {

using Calls = std::vector<std::string>;

std::string set(int priority) { return "set " + std::to_string(priority); }

class FakeScheduler final : public SchedulerGateway {
  public:
    void* gamesTask = nullptr;
    std::optional<int> priority = THREAD_PRIORITY_NORMAL;
    bool prioritySettable = true;
    bool timerAccepts = true;

    Calls calls;
    void* leftTask = nullptr;

    void* joinGamesTask() override {
        calls.emplace_back("join");
        return gamesTask;
    }
    void leaveGamesTask(void* task) override {
        calls.emplace_back("leave");
        leftTask = task;
    }
    std::optional<int> threadPriority() override {
        calls.emplace_back("get");
        return priority;
    }
    bool setThreadPriority(int newPriority) override {
        calls.push_back(set(newPriority));
        if (!prioritySettable) { return false; }
        priority = newPriority;
        return true;
    }
    bool beginTimerPeriod(unsigned periodMs) override {
        calls.push_back("begin " + std::to_string(periodMs));
        return timerAccepts;
    }
    void endTimerPeriod(unsigned periodMs) override {
        calls.push_back("end " + std::to_string(periodMs));
    }
};

} // namespace

TEST_CASE("Only the SDL input, USB reader and receive threads ask for the Games task",
          "[thread-priority]") {
    constexpr std::array<std::pair<ThreadRole, PriorityRequest>, dish::thread::kThreadRoleCount>
        kTable{{
            {ThreadRole::Gui, PriorityRequest::None},
            {ThreadRole::SdlInput, PriorityRequest::Games},
            {ThreadRole::UsbReader, PriorityRequest::Games},
            {ThreadRole::Receive, PriorityRequest::Games},
            {ThreadRole::Heartbeat, PriorityRequest::None},
            {ThreadRole::Playout, PriorityRequest::None},
            {ThreadRole::AudioCapture, PriorityRequest::None},
            {ThreadRole::MoonlightRx, PriorityRequest::None},
            {ThreadRole::Update, PriorityRequest::None},
            {ThreadRole::Other, PriorityRequest::None},
        }};
    for (std::size_t i = 0; i < kTable.size(); ++i) {
        const auto& [role, request] = kTable[i];
        INFO(dish::thread::threadRoleName(role));
        REQUIRE(static_cast<std::size_t>(role) == i);
        REQUIRE(priorityRequestFor(role) == request);
    }
}

TEST_CASE("A guard that joins the Games task leaves it with the handle it got",
          "[thread-priority]") {
    FakeScheduler fake;
    int task = 0;
    fake.gamesTask = &task;
    {
        const ThreadPriorityGuard guard(ThreadRole::SdlInput, fake);
        REQUIRE(guard.raised() == RaisedPriority::GamesTask);
        REQUIRE(fake.calls == Calls{"join"});
    }
    REQUIRE(fake.calls == Calls{"join", "leave"});
    REQUIRE(fake.leftTask == &task);
}

TEST_CASE("Where MMCSS refuses, a guard raises the thread to TIME_CRITICAL and restores the "
          "priority it found",
          "[thread-priority]") {
    FakeScheduler fake;
    fake.gamesTask = nullptr;
    fake.priority = THREAD_PRIORITY_BELOW_NORMAL;
    {
        const ThreadPriorityGuard guard(ThreadRole::Receive, fake);
        REQUIRE(guard.raised() == RaisedPriority::TimeCritical);
        REQUIRE(fake.priority == THREAD_PRIORITY_TIME_CRITICAL);
    }
    REQUIRE(fake.priority == THREAD_PRIORITY_BELOW_NORMAL);
    REQUIRE(fake.calls == Calls{"join", "get", set(THREAD_PRIORITY_TIME_CRITICAL),
                                set(THREAD_PRIORITY_BELOW_NORMAL)});
}

TEST_CASE("A guard that cannot raise the thread has nothing to restore", "[thread-priority]") {
    FakeScheduler fake;
    fake.gamesTask = nullptr;
    SECTION("the priority cannot be read") {
        fake.priority = std::nullopt;
        {
            const ThreadPriorityGuard guard(ThreadRole::UsbReader, fake);
            REQUIRE(guard.raised() == RaisedPriority::None);
        }
        REQUIRE(fake.calls == Calls{"join", "get"});
    }
    SECTION("the priority cannot be set") {
        fake.prioritySettable = false;
        {
            const ThreadPriorityGuard guard(ThreadRole::UsbReader, fake);
            REQUIRE(guard.raised() == RaisedPriority::None);
        }
        REQUIRE(fake.calls == Calls{"join", "get", set(THREAD_PRIORITY_TIME_CRITICAL)});
    }
}

TEST_CASE("A role that asks for nothing leaves the scheduler alone", "[thread-priority]") {
    for (const ThreadRole role :
         {ThreadRole::Gui, ThreadRole::Heartbeat, ThreadRole::Playout, ThreadRole::AudioCapture,
          ThreadRole::MoonlightRx, ThreadRole::Update, ThreadRole::Other}) {
        INFO(dish::thread::threadRoleName(role));
        FakeScheduler fake;
        int task = 0;
        fake.gamesTask = &task;
        {
            const ThreadPriorityGuard guard(role, fake);
            REQUIRE(guard.raised() == RaisedPriority::None);
        }
        REQUIRE(fake.calls.empty());
    }
}

TEST_CASE("A priority guard cannot be copied or moved off its thread", "[thread-priority]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<ThreadPriorityGuard>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<ThreadPriorityGuard>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<ScopedThreadPriority>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<ScopedThreadPriority>);
}

TEST_CASE("A timer period is ended only when it began, and with the same period",
          "[thread-priority]") {
    FakeScheduler fake;
    SECTION("began") {
        {
            const TimerPeriodGuard period(1, fake);
            REQUIRE(period.held());
        }
        REQUIRE(fake.calls == Calls{"begin 1", "end 1"});
    }
    SECTION("refused") {
        fake.timerAccepts = false;
        {
            const TimerPeriodGuard period(1, fake);
            REQUIRE_FALSE(period.held());
        }
        REQUIRE(fake.calls == Calls{"begin 1"});
    }
}

TEST_CASE("On the real scheduler a guard raises the calling thread and restores its priority",
          "[thread-priority]") {
    struct Seen {
        bool lowered = false;
        int before = THREAD_PRIORITY_ERROR_RETURN;
        RaisedPriority raised = RaisedPriority::None;
        int during = THREAD_PRIORITY_ERROR_RETURN;
        int after = THREAD_PRIORITY_ERROR_RETURN;
    };
    Seen seen;
    // A thread of its own, so a failure cannot leave the runner's thread raised; Catch2's
    // assertions are not thread-safe, so it records and the test checks after the join.
    std::thread([&seen] {
        const HANDLE self = ::GetCurrentThread();
        seen.lowered = ::SetThreadPriority(self, THREAD_PRIORITY_BELOW_NORMAL) != FALSE;
        seen.before = ::GetThreadPriority(self);
        {
            const ScopedThreadPriority priority(ThreadRole::SdlInput);
            seen.raised = priority.raised();
            seen.during = ::GetThreadPriority(self);
        }
        seen.after = ::GetThreadPriority(self);
    }).join();

    CAPTURE(seen.during);
    REQUIRE(seen.lowered);
    REQUIRE(seen.before == THREAD_PRIORITY_BELOW_NORMAL);
    REQUIRE(seen.raised != RaisedPriority::None);
    if (seen.raised == RaisedPriority::TimeCritical) {
        REQUIRE(seen.during == THREAD_PRIORITY_TIME_CRITICAL);
    }
    REQUIRE(seen.after == THREAD_PRIORITY_BELOW_NORMAL);
}

TEST_CASE("The real scheduler sets and restores the calling thread's priority",
          "[thread-priority]") {
    struct Seen {
        std::optional<int> before;
        bool raised = false;
        std::optional<int> during;
        bool restored = false;
        std::optional<int> after;
    };
    Seen seen;
    std::thread([&seen] {
        WindowsSchedulerGateway scheduler;
        seen.before = scheduler.threadPriority();
        seen.raised = scheduler.setThreadPriority(THREAD_PRIORITY_TIME_CRITICAL);
        seen.during = scheduler.threadPriority();
        seen.restored = scheduler.setThreadPriority(seen.before.value_or(THREAD_PRIORITY_NORMAL));
        seen.after = scheduler.threadPriority();
    }).join();

    REQUIRE(seen.before == THREAD_PRIORITY_NORMAL);
    REQUIRE(seen.raised);
    REQUIRE(seen.during == THREAD_PRIORITY_TIME_CRITICAL);
    REQUIRE(seen.restored);
    REQUIRE(seen.after == THREAD_PRIORITY_NORMAL);
}

TEST_CASE("The real timer takes a 1 ms period and refuses a period out of range",
          "[thread-priority]") {
    WindowsSchedulerGateway scheduler;
    const TimerPeriodGuard oneMs(1, scheduler);
    REQUIRE(oneMs.held());
    const TimerPeriodGuard zero(0, scheduler);
    REQUIRE_FALSE(zero.held());
}
