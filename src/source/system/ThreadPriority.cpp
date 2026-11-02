// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/ThreadPriority.h"

#include <QLoggingCategory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <avrt.h>
#include <timeapi.h>

namespace dish::source {

namespace {

Q_LOGGING_CATEGORY(lcDishThread, "dish.thread")

} // namespace

void* WindowsSchedulerGateway::joinGamesTask() {
    DWORD taskIndex = 0;
    void* const task = ::AvSetMmThreadCharacteristicsW(L"Games", &taskIndex);
    if (task == nullptr) {
        const DWORD error = ::GetLastError();
        qCInfo(lcDishThread) << "MMCSS refused the Games task, error" << error;
    }
    return task;
}

void WindowsSchedulerGateway::leaveGamesTask(void* task) {
    if (::AvRevertMmThreadCharacteristics(task) == FALSE) {
        const DWORD error = ::GetLastError();
        qCWarning(lcDishThread) << "AvRevertMmThreadCharacteristics failed, error" << error;
    }
}

std::optional<int> WindowsSchedulerGateway::threadPriority() {
    const int priority = ::GetThreadPriority(::GetCurrentThread());
    if (priority == THREAD_PRIORITY_ERROR_RETURN) {
        const DWORD error = ::GetLastError();
        qCWarning(lcDishThread) << "GetThreadPriority failed, error" << error;
        return std::nullopt;
    }
    return priority;
}

bool WindowsSchedulerGateway::setThreadPriority(int priority) {
    if (::SetThreadPriority(::GetCurrentThread(), priority) == FALSE) {
        const DWORD error = ::GetLastError();
        qCWarning(lcDishThread) << "SetThreadPriority" << priority << "failed, error" << error;
        return false;
    }
    return true;
}

bool WindowsSchedulerGateway::beginTimerPeriod(unsigned periodMs) {
    const MMRESULT result = ::timeBeginPeriod(periodMs);
    if (result != TIMERR_NOERROR) {
        qCWarning(lcDishThread) << "timeBeginPeriod" << periodMs << "refused, MMRESULT" << result;
        return false;
    }
    return true;
}

void WindowsSchedulerGateway::endTimerPeriod(unsigned periodMs) {
    const MMRESULT result = ::timeEndPeriod(periodMs);
    if (result != TIMERR_NOERROR) {
        qCWarning(lcDishThread) << "timeEndPeriod" << periodMs << "refused, MMRESULT" << result;
    }
}

ThreadPriorityGuard::ThreadPriorityGuard(thread::ThreadRole role, SchedulerGateway& scheduler)
    : scheduler_(scheduler), held_(raise(priorityRequestFor(role), scheduler)) {}

ThreadPriorityGuard::~ThreadPriorityGuard() {
    switch (held_.raised) {
    case RaisedPriority::GamesTask:
        scheduler_.leaveGamesTask(held_.task);
        break;
    case RaisedPriority::TimeCritical:
        scheduler_.setThreadPriority(held_.previousPriority);
        break;
    case RaisedPriority::None:
        break;
    }
}

ThreadPriorityGuard::Held ThreadPriorityGuard::raise(PriorityRequest request,
                                                     SchedulerGateway& scheduler) {
    if (request == PriorityRequest::None) { return {}; }
    if (void* const task = scheduler.joinGamesTask(); task != nullptr) {
        return {RaisedPriority::GamesTask, task, 0};
    }
    // MMCSS refuses a task the registry does not list and a caller without the privilege. The
    // thread then raises itself, which, unlike the Games task, nothing throttles.
    const std::optional<int> previous = scheduler.threadPriority();
    if (!previous.has_value() || !scheduler.setThreadPriority(THREAD_PRIORITY_TIME_CRITICAL)) {
        return {};
    }
    return {RaisedPriority::TimeCritical, nullptr, *previous};
}

TimerPeriodGuard::TimerPeriodGuard(unsigned periodMs, SchedulerGateway& scheduler)
    : scheduler_(scheduler), periodMs_(periodMs), held_(scheduler.beginTimerPeriod(periodMs)) {}

TimerPeriodGuard::~TimerPeriodGuard() {
    if (held_) { scheduler_.endTimerPeriod(periodMs_); }
}

} // namespace dish::source
