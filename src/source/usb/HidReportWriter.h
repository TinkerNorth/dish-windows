// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "core/thread/TrackedMutex.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace dish::source::usb {

// One claimed pad's OUT reports, written by the claim's output worker and nobody else on the hot
// path. Blocking: a call returns once the pad took the report or the write was given up.
class HidReportWriter {
  public:
    virtual ~HidReportWriter() = default;
    virtual bool write(const std::uint8_t* data, std::size_t len) = 0;

  protected:
    HidReportWriter() = default;
    HidReportWriter(const HidReportWriter&) = default;
    HidReportWriter& operator=(const HidReportWriter&) = default;
    HidReportWriter(HidReportWriter&&) = default;
    HidReportWriter& operator=(HidReportWriter&&) = default;
};

inline constexpr std::chrono::milliseconds kOutputWriteTimeout{250};

// The real one: an overlapped WriteFile on the claim's handle, zero-padded to the collection's
// output length, waited on one event made once, and cancelled when the pad has not taken it within
// the timeout. `writeMtx` is the claim's own (hid.write), shared with the gateway's other writes
// to the handle; the handle stays open until the worker that owns this writer has stopped.
class HandleReportWriter final : public HidReportWriter {
  public:
    HandleReportWriter(void* handle, std::size_t outputReportLen, thread::TrackedMutex* writeMtx,
                       std::chrono::milliseconds timeout = kOutputWriteTimeout);
    ~HandleReportWriter() override;

    HandleReportWriter(const HandleReportWriter&) = delete;
    HandleReportWriter& operator=(const HandleReportWriter&) = delete;
    HandleReportWriter(HandleReportWriter&&) = delete;
    HandleReportWriter& operator=(HandleReportWriter&&) = delete;

    bool write(const std::uint8_t* data, std::size_t len) override;

  private:
    void* const handle_;
    const std::size_t outputReportLen_;
    thread::TrackedMutex* const writeMtx_;
    const std::chrono::milliseconds timeout_;
    void* const event_;
};

} // namespace dish::source::usb
