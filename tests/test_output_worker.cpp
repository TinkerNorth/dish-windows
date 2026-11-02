// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A Direct pad's feedback: producers post to the claim's worker and return at once, the worker
// writes on its own thread through the HID writer seam. The real writer is driven against a named
// pipe, which takes overlapped writes the way a HID handle does.

#include "source/usb/HidReportWriter.h"
#include "source/usb/OutputWorker.h"

#include "core/input/UsbReportParsers.h"
#include "core/thread/TrackedMutex.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using dish::input::usbparse::HidParser;
using dish::source::usb::HandleReportWriter;
using dish::source::usb::HidReportWriter;
using dish::source::usb::OutputWorker;
using namespace std::chrono_literals;

namespace {

// A pad that takes `delay` to accept each report, recording what it got.
struct SlowPad {
    std::chrono::milliseconds delay{0};
    mutable std::mutex mtx;
    std::vector<std::vector<std::uint8_t>> reports;

    std::vector<std::vector<std::uint8_t>> got() const {
        const std::lock_guard<std::mutex> lock(mtx);
        return reports;
    }
};

class SlowWriter final : public HidReportWriter {
  public:
    explicit SlowWriter(SlowPad& pad) : pad_(pad) {}
    bool write(const std::uint8_t* data, std::size_t len) override {
        std::this_thread::sleep_for(pad_.delay);
        const std::lock_guard<std::mutex> lock(pad_.mtx);
        pad_.reports.emplace_back(data, data + len);
        return true;
    }

  private:
    SlowPad& pad_;
};

bool waitFor(const SlowPad& pad, std::size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pad.got().size() >= count) { return true; }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

// The DualSense rumble report carries the strong motor's high byte at 4.
constexpr std::size_t kStrongByte = 4;

std::wstring pipeName(const wchar_t* what) {
    return L"\\\\.\\pipe\\dish-hid-writer-" + std::wstring(what) + L"-" +
           std::to_wstring(::GetCurrentProcessId());
}

} // namespace

TEST_CASE("a slow pad never delays the thread posting its feedback", "[output-worker]") {
    SlowPad pad;
    pad.delay = 100ms;
    OutputWorker worker(std::make_unique<SlowWriter>(pad), HidParser::DualSense);
    auto slowest = std::chrono::steady_clock::duration::zero();
    for (int i = 0; i < 50; ++i) {
        const auto started = std::chrono::steady_clock::now();
        worker.postRumble(static_cast<std::uint16_t>(i << 8), 0);
        slowest = std::max(slowest, std::chrono::steady_clock::now() - started);
    }
    CHECK(slowest < 20ms);
    CHECK(waitFor(pad, 1));
    worker.stop();
}

TEST_CASE("the newest feedback wins over a stale one", "[output-worker]") {
    SlowPad pad;
    pad.delay = 150ms;
    OutputWorker worker(std::make_unique<SlowWriter>(pad), HidParser::DualSense);
    worker.postRumble(0x1000, 0);
    // The first write is under way now; these two land while the pad is busy.
    std::this_thread::sleep_for(20ms);
    worker.postRumble(0x2000, 0);
    worker.postRumble(0x3000, 0);
    REQUIRE(waitFor(pad, 2));
    std::this_thread::sleep_for(400ms);
    const auto reports = pad.got();
    REQUIRE(reports.size() == 2U);
    CHECK(reports[0][kStrongByte] == 0x10);
    CHECK(reports[1][kStrongByte] == 0x30);
    worker.stop();
}

TEST_CASE("a stopped worker writes nothing more", "[output-worker]") {
    SlowPad pad;
    OutputWorker worker(std::make_unique<SlowWriter>(pad), HidParser::DualSense);
    worker.stop();
    worker.postLightbar(1, 2, 3);
    std::this_thread::sleep_for(50ms);
    CHECK(pad.got().empty());
    worker.stop();
}

TEST_CASE("a family without the surface writes nothing and counts the refusal", "[output-worker]") {
    SlowPad pad;
    OutputWorker worker(std::make_unique<SlowWriter>(pad), HidParser::DualShock4);
    worker.postPlayerLeds(0x01);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (worker.failedWrites() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(worker.failedWrites() == 1U);
    CHECK(pad.got().empty());
}

TEST_CASE("the handle writer pads a report to the output length", "[output-worker][hid]") {
    const std::wstring name = pipeName(L"pad");
    HANDLE pad = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT,
                                    1, 0, 4096, 0, nullptr);
    REQUIRE(pad != INVALID_HANDLE_VALUE);
    HANDLE handle = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    dish::thread::TrackedMutex writeMtx{"hid.write"};
    {
        HandleReportWriter writer(handle, 8, &writeMtx);
        const std::array<std::uint8_t, 3> report{0x05, 0x11, 0x22};
        CHECK(writer.write(report.data(), report.size()));
        CHECK_FALSE(writer.write(report.data(), 0));
        std::array<std::uint8_t, 9> tooLong{};
        CHECK_FALSE(writer.write(tooLong.data(), tooLong.size()));
    }
    std::array<std::uint8_t, 8> got{};
    DWORD read = 0;
    REQUIRE(::ReadFile(pad, got.data(), static_cast<DWORD>(got.size()), &read, nullptr) != 0);
    CHECK(read == 8U);
    CHECK(got == std::array<std::uint8_t, 8>{0x05, 0x11, 0x22, 0, 0, 0, 0, 0});
    ::CloseHandle(handle);
    ::CloseHandle(pad);
}

TEST_CASE("the handle writer gives up on a pad that never takes the report",
          "[output-worker][hid]") {
    const std::wstring name = pipeName(L"stuck");
    HANDLE pad = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT,
                                    1, 0, 1, 0, nullptr);
    REQUIRE(pad != INVALID_HANDLE_VALUE);
    HANDLE handle = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    dish::thread::TrackedMutex writeMtx{"hid.write"};
    HandleReportWriter writer(handle, 256, &writeMtx, 50ms);
    const std::array<std::uint8_t, 256> report{0x02};
    // Nobody reads the pipe, so once its buffer is full a write stays pending.
    bool gaveUp = false;
    auto slowest = std::chrono::steady_clock::duration::zero();
    for (int i = 0; i < 200 && !gaveUp; ++i) {
        const auto started = std::chrono::steady_clock::now();
        gaveUp = !writer.write(report.data(), report.size());
        slowest = std::max(slowest, std::chrono::steady_clock::now() - started);
    }
    CHECK(gaveUp);
    CHECK(slowest >= 40ms);
    CHECK(slowest < 1s);
    ::CloseHandle(handle);
    ::CloseHandle(pad);
}
