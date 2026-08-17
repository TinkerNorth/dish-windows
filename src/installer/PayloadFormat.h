// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The dish-setup.exe payload format. A packed installer is
//
//     [DishSetupStub PE] [payload.zip] [Trailer(32 bytes)]
//
// payload.zip is a STANDARD zip (`7z x dish-setup.exe` is the documented
// recovery path) and the fixed-size trailer at EOF locates it. This layer is
// deliberately Qt-free: it is compiled per-target into the /MT stub, the pack
// tool, dish-setup-ui.exe and DishTests, so it may only depend on Win32, the
// CRT and the vendored miniz (third_party/miniz, pinned 3.0.2).

#pragma once

#include <atomic>
#include <cstdint>

namespace dish::installer::payload {

#pragma pack(push, 1)
struct Trailer {            // exactly 32 bytes at EOF
    char magic[8];          // "DISHSFX1"
    uint32_t formatVersion; // 1
    uint32_t zipCrc32;      // CRC-32 (zlib polynomial) of the zip bytes
    uint64_t zipOffset;     // absolute offset of payload.zip in the file
    uint64_t zipSize;       // byte length of payload.zip
};
#pragma pack(pop)
static_assert(sizeof(Trailer) == 32, "Trailer is the on-disk format; it must stay 32 bytes");

// Reads the last 32 bytes of `exePath` into `out` and validates the shape:
// magic, formatVersion == 1, and zipOffset/zipSize describing a non-empty
// range that ends exactly where the trailer begins. Returns false for any
// short, unpacked or geometry-corrupt file; the payload bytes themselves are
// NOT checked here (that is checkOverlayCrc).
bool readTrailer(const wchar_t* exePath, Trailer& out);

// Streams the [zipOffset, zipOffset + zipSize) range and compares its CRC-32
// against trailer.zipCrc32. The whole-payload gate the stub runs before any
// extraction; per-entry CRCs are additionally verified by miniz during
// extraction.
bool checkOverlayCrc(const wchar_t* exePath, const Trailer& trailer);

// Pack-tool half: appends the bytes of `zipPath` plus a freshly computed
// trailer to the end of `exePath` (a stub copy). Refuses a file that already
// ends in a valid trailer, so an artifact can never be double-packed.
bool appendZipAndTrailer(const wchar_t* exePath, const wchar_t* zipPath);

// Zip entry-name police, enforced by the pack tool at write time and
// re-validated before extraction: printable ASCII only, forward slashes,
// relative, no "." or ".." segments, no empty segments (one trailing slash is
// allowed as a directory marker), no drive letters or other characters
// Windows file names cannot carry, no segment ending in a dot or space, no
// DOS device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9).
bool isSafeEntryName(const char* name);

// Progress callback for extractAll: `bytesDelta` uncompressed bytes were just
// written. Return false to abort mid-entry. May be null.
using ProgressFn = bool (*)(void* ctx, uint64_t bytesDelta);

// Extracts every entry of the payload into `destDir` (created if missing),
// verifying each entry's CRC-32 (miniz) and each entry name (isSafeEntryName).
// `cancel` is polled at every entry boundary ("stop at next entry"); ProgressFn
// returning false aborts mid-entry. Returns false on corruption, IO failure or
// cancellation; the caller distinguishes cancellation by its own `cancel` /
// callback state. Long paths are handled via \\?\ prefixing.
bool extractAll(const wchar_t* exePath, const Trailer& trailer, const wchar_t* destDir,
                ProgressFn progress, void* ctx, std::atomic<bool>& cancel);

// Additive helper (not part of the spec 3.2 surface): sum of the uncompressed
// sizes of every payload entry, for the stub's %TEMP% free-space preflight.
bool uncompressedTotal(const wchar_t* exePath, const Trailer& trailer, uint64_t& out);

} // namespace dish::installer::payload
