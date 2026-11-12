// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "core/thread/TrackedMutex.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace dish::source::usb {

// What a feature write came to: the pad took the report, refused it (which a dongle under load
// does transiently, so writeFeatureWithRetry offers it again), or is gone, which no retry mends.
enum class FeatureWrite : std::uint8_t {
    Taken,
    Refused,
    PadGone,
};

// One claimed pad's OUT reports, written by the claim's output worker and nobody else on the hot
// path. Blocking: a call returns once the pad took the report or the write was given up.
class HidReportWriter {
  public:
    virtual ~HidReportWriter() = default;
    virtual bool write(const std::uint8_t* data, std::size_t len) = 0;
    // One feature report, `data[0]` its id, the whole report as built. Refused by a writer with no
    // feature path, which is every writer but a claimed pad's own.
    virtual FeatureWrite writeFeature(const std::uint8_t* /*data*/, std::size_t /*len*/) {
        return FeatureWrite::Refused;
    }

  protected:
    HidReportWriter() = default;
    HidReportWriter(const HidReportWriter&) = default;
    HidReportWriter& operator=(const HidReportWriter&) = default;
    HidReportWriter(HidReportWriter&&) = default;
    HidReportWriter& operator=(HidReportWriter&&) = default;
};

inline constexpr std::chrono::milliseconds kFeatureWriteRetryPause{20};

// A feature report offered `attempts` times in all, with a pause after each refusal: the way SDL
// and hid-steam retry the 2015 dongle's EPIPE under load (usbparse::configWriteAttempts says how
// often a family's reports are offered). True once the pad took it; a pad that is gone is not
// asked again.
inline bool writeFeatureWithRetry(HidReportWriter& writer, const std::uint8_t* report,
                                  std::size_t len, int attempts) {
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        switch (writer.writeFeature(report, len)) {
        case FeatureWrite::Taken:
            return true;
        case FeatureWrite::PadGone:
            return false;
        case FeatureWrite::Refused:
            break;
        }
        const bool lastAttempt = attempt == attempts;
        if (!lastAttempt) { std::this_thread::sleep_for(kFeatureWriteRetryPause); }
    }
    return false;
}

inline constexpr std::chrono::milliseconds kOutputWriteTimeout{250};

// What a failed HidD_SetFeature's last error says: the device left
// (ERROR_DEVICE_NOT_CONNECTED, ERROR_GEN_FAILURE), which no retry mends, or anything else.
FeatureWrite featureWriteOutcomeOf(unsigned long lastError);

// The real one: an overlapped WriteFile on the claim's handle, zero-padded to the collection's
// output length, waited on one event made once, and cancelled when the pad has not taken it within
// the timeout. A feature report goes through HidD_SetFeature, padded to the feature length.
// `writeMtx` is the claim's own (hid.write), shared with the gateway's other writes to the handle;
// the handle stays open until the worker that owns this writer has stopped.
class HandleReportWriter final : public HidReportWriter {
  public:
    HandleReportWriter(void* handle, std::size_t outputReportLen, std::size_t featureReportLen,
                       thread::TrackedMutex* writeMtx,
                       std::chrono::milliseconds timeout = kOutputWriteTimeout);
    ~HandleReportWriter() override;

    HandleReportWriter(const HandleReportWriter&) = delete;
    HandleReportWriter& operator=(const HandleReportWriter&) = delete;
    HandleReportWriter(HandleReportWriter&&) = delete;
    HandleReportWriter& operator=(HandleReportWriter&&) = delete;

    bool write(const std::uint8_t* data, std::size_t len) override;
    FeatureWrite writeFeature(const std::uint8_t* data, std::size_t len) override;

  private:
    void* const handle_;
    const std::size_t outputReportLen_;
    const std::size_t featureReportLen_;
    thread::TrackedMutex* const writeMtx_;
    const std::chrono::milliseconds timeout_;
    void* const event_;
};

} // namespace dish::source::usb
