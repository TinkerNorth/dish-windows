// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/HidReportWriter.h"

#include <windows.h>

#include <hidsdi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace dish::source::usb {

HandleReportWriter::HandleReportWriter(void* handle, std::size_t outputReportLen,
                                       std::size_t featureReportLen, thread::TrackedMutex* writeMtx,
                                       std::chrono::milliseconds timeout)
    : handle_(handle), outputReportLen_(outputReportLen), featureReportLen_(featureReportLen),
      writeMtx_(writeMtx), timeout_(timeout),
      event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

HandleReportWriter::~HandleReportWriter() {
    if (event_ != nullptr) { ::CloseHandle(event_); }
}

// The handle is overlapped, so a write needs its own OVERLAPPED and event or it would complete into
// the read loop's. A pad that never completes is cancelled at the timeout and the report dropped,
// which is the right outcome for a lossy feedback path.
bool HandleReportWriter::write(const std::uint8_t* data, std::size_t len) {
    // Exactly the collection's output length, zero-padded: the HID stack rejects any other length,
    // and a longer report would reach the pad as a different one.
    std::array<std::uint8_t, 256> buf{};
    if (data == nullptr || len == 0 || len > outputReportLen_ || outputReportLen_ > buf.size() ||
        event_ == nullptr || handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::memcpy(buf.data(), data, len);
    const auto want = static_cast<DWORD>(outputReportLen_);

    std::lock_guard<thread::TrackedMutex> lock(*writeMtx_);
    thread::blockingRegion("hid.write");
    ::ResetEvent(event_);
    OVERLAPPED ov{};
    ov.hEvent = event_;
    DWORD written = 0;
    bool ok = ::WriteFile(handle_, buf.data(), want, &written, &ov) != 0;
    if (!ok && ::GetLastError() == ERROR_IO_PENDING) {
        const auto waitMs = static_cast<DWORD>(timeout_.count());
        if (::WaitForSingleObject(event_, waitMs) == WAIT_OBJECT_0) {
            ok = ::GetOverlappedResult(handle_, &ov, &written, FALSE) != 0;
        } else {
            ::CancelIoEx(handle_, &ov);
            ::GetOverlappedResult(handle_, &ov, &written, TRUE);
            ok = false;
        }
    }
    // A short write reached the pad as a different report, which is a failure even though the call
    // returned.
    return ok && written == want;
}

// Sized the way hidapi sizes it for SDL: a report shorter than the collection's feature length is
// zero-padded up to it, which HidD_SetFeature insists on, and a longer one goes whole for the HID
// class driver to cut to that length. HidD_SetFeature is synchronous, so there is no overlapped
// wait to cancel, and the handle's overlapped read is unaffected; the Steam Controller config
// writes share the handle the same way.
FeatureWrite HandleReportWriter::writeFeature(const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, 256> buf{};
    const std::size_t sendLen = std::max(len, featureReportLen_);
    if (data == nullptr || len == 0 || featureReportLen_ == 0 || sendLen > buf.size() ||
        handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) {
        return FeatureWrite::Refused;
    }
    std::memcpy(buf.data(), data, len);
    std::lock_guard<thread::TrackedMutex> lock(*writeMtx_);
    thread::blockingRegion("hid.write");
    if (::HidD_SetFeature(handle_, buf.data(), static_cast<ULONG>(sendLen)) != 0) {
        return FeatureWrite::Taken;
    }
    return featureWriteOutcomeOf(::GetLastError());
}

FeatureWrite featureWriteOutcomeOf(unsigned long lastError) {
    const bool gone = lastError == ERROR_DEVICE_NOT_CONNECTED || lastError == ERROR_GEN_FAILURE;
    return gone ? FeatureWrite::PadGone : FeatureWrite::Refused;
}

} // namespace dish::source::usb
