// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Qt-free implementation of the payload trailer + zip layer. File IO is CRT
// stdio over wide paths (what miniz's cfile API consumes); destination files
// and directories go through Win32 so \\?\ long-path prefixing works.

#include "PayloadFormat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <io.h> // _get_osfhandle

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../third_party/miniz/miniz.h"

namespace dish::installer::payload {

namespace {

constexpr char kMagic[8] = {'D', 'I', 'S', 'H', 'S', 'F', 'X', '1'};
constexpr uint32_t kFormatVersion = 1;
constexpr size_t kIoChunk = 256 * 1024;

// RAII for CRT FILE*.
struct CFile {
    FILE* f = nullptr;
    explicit CFile(const wchar_t* path, const wchar_t* mode) { f = _wfopen(path, mode); }
    ~CFile() {
        if (f) fclose(f);
    }
    CFile(const CFile&) = delete;
    CFile& operator=(const CFile&) = delete;
};

bool fileSizeOf(FILE* f, uint64_t& out) {
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const long long size = _ftelli64(f);
    if (size < 0) return false;
    out = static_cast<uint64_t>(size);
    return true;
}

// Fully-qualifies `path` and applies the \\?\ prefix so every later Win32 call
// is long-path safe. Forward slashes are normalized first; \\?\ paths must use
// backslashes.
std::wstring longPath(const std::wstring& path) {
    std::wstring p = path;
    for (wchar_t& c : p) {
        if (c == L'/') c = L'\\';
    }
    if (p.rfind(L"\\\\?\\", 0) == 0) return p;
    DWORD needed = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return p;
    std::wstring full(needed, L'\0');
    DWORD written = GetFullPathNameW(p.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) return p;
    full.resize(written);
    if (full.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + full.substr(2);
    return L"\\\\?\\" + full;
}

// Creates every missing directory component of the (\\?\-prefixed) absolute
// directory path. Existing components are fine.
bool ensureDirectories(const std::wstring& prefixedDir) {
    // Skip past "\\?\C:" or "\\?\UNC\server\share" before walking components.
    size_t start = 0;
    if (prefixedDir.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        start = prefixedDir.find(L'\\', 8); // past server
        if (start == std::wstring::npos) return false;
        start = prefixedDir.find(L'\\', start + 1);   // past share
        if (start == std::wstring::npos) return true; // bare share root
    } else if (prefixedDir.rfind(L"\\\\?\\", 0) == 0) {
        start = prefixedDir.find(L'\\', 4);
        if (start == std::wstring::npos) return true; // bare drive root
    }
    size_t pos = start;
    for (;;) {
        pos = prefixedDir.find(L'\\', pos + 1);
        const std::wstring partial =
            (pos == std::wstring::npos) ? prefixedDir : prefixedDir.substr(0, pos);
        if (!partial.empty() && partial.back() != L':') {
            if (!CreateDirectoryW(partial.c_str(), nullptr)) {
                const DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS) return false;
            }
        }
        if (pos == std::wstring::npos) break;
    }
    return true;
}

// A segment whose base name (up to the first '.') is a DOS device name would
// resolve to the device wherever a path is used without the \\?\ prefix. Our
// own images never contain one; treat them as hostile.
bool isDosDeviceSegment(const char* seg, size_t len) {
    size_t base = 0;
    while (base < len && seg[base] != '.') ++base;
    const auto equalsCi = [seg](const char* upperWord, size_t wordLen, size_t baseLen) {
        if (baseLen != wordLen) return false;
        for (size_t i = 0; i < wordLen; ++i) {
            char c = seg[i];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (c != upperWord[i]) return false;
        }
        return true;
    };
    if (equalsCi("CON", 3, base) || equalsCi("PRN", 3, base) || equalsCi("AUX", 3, base) ||
        equalsCi("NUL", 3, base))
        return true;
    if (base == 4 && seg[3] >= '1' && seg[3] <= '9')
        return equalsCi("COM", 3, 3) || equalsCi("LPT", 3, 3);
    return false;
}

// ASCII zip entry name -> wide relative path with backslashes.
std::wstring entryNameToRelativeWin(const char* name) {
    std::wstring rel;
    rel.reserve(std::strlen(name));
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        rel.push_back(c == '/' ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return rel;
}

struct WriteSink {
    HANDLE file = INVALID_HANDLE_VALUE;
    ProgressFn progress = nullptr;
    void* ctx = nullptr;
    bool ioError = false;
    bool aborted = false;
};

size_t writeSinkCallback(void* opaque, mz_uint64 fileOfs, const void* buf, size_t n) {
    (void)fileOfs; // extraction writes are sequential
    WriteSink* sink = static_cast<WriteSink*>(opaque);
    DWORD written = 0;
    if (!WriteFile(sink->file, buf, static_cast<DWORD>(n), &written, nullptr) || written != n) {
        sink->ioError = true;
        return 0;
    }
    if (sink->progress && !sink->progress(sink->ctx, static_cast<uint64_t>(n))) {
        sink->aborted = true;
        return 0;
    }
    return n;
}

// Opens the archive slice [trailer.zipOffset, +zipSize) of exePath as a miniz
// reader. On success the caller owns both handles and must call
// mz_zip_reader_end() then fclose().
bool openPayloadReader(const wchar_t* exePath, const Trailer& trailer, mz_zip_archive& zip,
                       FILE*& file) {
    file = _wfopen(longPath(exePath).c_str(), L"rb");
    if (!file) return false;
    uint64_t size = 0;
    if (!fileSizeOf(file, size) || trailer.zipSize == 0 || trailer.zipOffset > size ||
        size - trailer.zipOffset < trailer.zipSize) {
        fclose(file);
        file = nullptr;
        return false;
    }
    if (_fseeki64(file, static_cast<long long>(trailer.zipOffset), SEEK_SET) != 0) {
        fclose(file);
        file = nullptr;
        return false;
    }
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_cfile(&zip, file, trailer.zipSize, 0)) {
        fclose(file);
        file = nullptr;
        return false;
    }
    return true;
}

} // namespace

bool readTrailer(const wchar_t* exePath, Trailer& out) {
    if (!exePath) return false;
    CFile in(longPath(exePath).c_str(), L"rb");
    if (!in.f) return false;
    uint64_t size = 0;
    if (!fileSizeOf(in.f, size) || size < sizeof(Trailer)) return false;
    if (_fseeki64(in.f, static_cast<long long>(size - sizeof(Trailer)), SEEK_SET) != 0)
        return false;
    if (fread(&out, 1, sizeof(Trailer), in.f) != sizeof(Trailer)) return false;
    if (std::memcmp(out.magic, kMagic, sizeof(kMagic)) != 0) return false;
    if (out.formatVersion != kFormatVersion) return false;
    if (out.zipSize == 0) return false;
    // Overflow-proof geometry check: the zip must end exactly where the
    // trailer begins.
    const uint64_t overlayEnd = size - sizeof(Trailer);
    if (out.zipSize > overlayEnd) return false;
    if (out.zipOffset != overlayEnd - out.zipSize) return false;
    return true;
}

bool checkOverlayCrc(const wchar_t* exePath, const Trailer& trailer) {
    if (!exePath || trailer.zipSize == 0) return false;
    CFile in(longPath(exePath).c_str(), L"rb");
    if (!in.f) return false;
    uint64_t size = 0;
    if (!fileSizeOf(in.f, size) || trailer.zipOffset > size ||
        size - trailer.zipOffset < trailer.zipSize)
        return false;
    if (_fseeki64(in.f, static_cast<long long>(trailer.zipOffset), SEEK_SET) != 0) return false;
    std::vector<unsigned char> buf(kIoChunk);
    mz_ulong crc = MZ_CRC32_INIT;
    uint64_t remaining = trailer.zipSize;
    while (remaining > 0) {
        const size_t want = remaining < static_cast<uint64_t>(buf.size())
                                ? static_cast<size_t>(remaining)
                                : buf.size();
        if (fread(buf.data(), 1, want, in.f) != want) return false;
        crc = mz_crc32(crc, buf.data(), want);
        remaining -= want;
    }
    return static_cast<uint32_t>(crc) == trailer.zipCrc32;
}

bool appendZipAndTrailer(const wchar_t* exePath, const wchar_t* zipPath) {
    if (!exePath || !zipPath) return false;
    {
        // Never stack a second payload onto an already-packed artifact.
        Trailer existing{};
        if (readTrailer(exePath, existing)) return false;
    }
    CFile zip(longPath(zipPath).c_str(), L"rb");
    if (!zip.f) return false;
    uint64_t zipSize = 0;
    if (!fileSizeOf(zip.f, zipSize) || zipSize == 0) return false;
    if (_fseeki64(zip.f, 0, SEEK_SET) != 0) return false;

    CFile exe(longPath(exePath).c_str(), L"r+b");
    if (!exe.f) return false;
    uint64_t zipOffset = 0;
    if (!fileSizeOf(exe.f, zipOffset)) return false; // append point == current EOF

    std::vector<unsigned char> buf(kIoChunk);
    mz_ulong crc = MZ_CRC32_INIT;
    uint64_t remaining = zipSize;
    while (remaining > 0) {
        const size_t want = remaining < static_cast<uint64_t>(buf.size())
                                ? static_cast<size_t>(remaining)
                                : buf.size();
        if (fread(buf.data(), 1, want, zip.f) != want) return false;
        crc = mz_crc32(crc, buf.data(), want);
        if (fwrite(buf.data(), 1, want, exe.f) != want) return false;
        remaining -= want;
    }

    Trailer trailer{};
    std::memcpy(trailer.magic, kMagic, sizeof(kMagic));
    trailer.formatVersion = kFormatVersion;
    trailer.zipCrc32 = static_cast<uint32_t>(crc);
    trailer.zipOffset = zipOffset;
    trailer.zipSize = zipSize;
    if (fwrite(&trailer, 1, sizeof(trailer), exe.f) != sizeof(trailer)) return false;
    if (fflush(exe.f) != 0) return false;
    // Push the artifact to disk before the pack tool reports success; CI moves
    // the file immediately afterwards.
    FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(exe.f))));
    return true;
}

bool isSafeEntryName(const char* name) {
    if (!name || *name == '\0') return false;
    const size_t len = std::strlen(name);
    if (len > 4096) return false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c > 0x7E) return false; // printable ASCII only
        switch (c) {
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false; // Windows-invalid (':' also kills drive letters)
        default:
            break;
        }
    }
    if (name[0] == '/') return false; // absolute
    size_t segStart = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i != len && name[i] != '/') continue;
        const size_t segLen = i - segStart;
        if (segLen == 0) {
            // Only a single trailing slash (directory marker) may produce an
            // empty segment.
            if (i != len) return false;
            break;
        }
        if (segLen == 1 && name[segStart] == '.') return false;
        if (segLen == 2 && name[segStart] == '.' && name[segStart + 1] == '.') return false;
        const char last = name[segStart + segLen - 1];
        if (last == '.' || last == ' ') return false; // Windows strips these; hostile
        if (isDosDeviceSegment(name + segStart, segLen)) return false;
        segStart = i + 1;
    }
    return true;
}

bool extractAll(const wchar_t* exePath, const Trailer& trailer, const wchar_t* destDir,
                ProgressFn progress, void* ctx, std::atomic<bool>& cancel) {
    if (!exePath || !destDir || *destDir == L'\0') return false;

    const std::wstring destRoot = longPath(destDir);
    if (!ensureDirectories(destRoot)) return false;

    mz_zip_archive zip;
    FILE* file = nullptr;
    if (!openPayloadReader(exePath, trailer, zip, file)) return false;

    bool ok = true;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count && ok; ++i) {
        if (cancel.load(std::memory_order_relaxed)) { // "stop at next entry"
            ok = false;
            break;
        }
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            ok = false;
            break;
        }
        if (!isSafeEntryName(stat.m_filename)) {
            ok = false;
            break;
        }
        const std::wstring target = destRoot + L"\\" + entryNameToRelativeWin(stat.m_filename);
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            std::wstring dir = target;
            while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
            if (!ensureDirectories(dir)) ok = false;
            continue;
        }
        const size_t lastSep = target.find_last_of(L'\\');
        if (lastSep != std::wstring::npos && !ensureDirectories(target.substr(0, lastSep))) {
            ok = false;
            break;
        }
        WriteSink sink;
        sink.progress = progress;
        sink.ctx = ctx;
        sink.file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (sink.file == INVALID_HANDLE_VALUE) {
            ok = false;
            break;
        }
        // miniz verifies the entry's CRC-32 as part of this call.
        const mz_bool extracted =
            mz_zip_reader_extract_to_callback(&zip, i, &writeSinkCallback, &sink, 0);
        CloseHandle(sink.file);
        if (!extracted) ok = false;
    }

    mz_zip_reader_end(&zip);
    fclose(file);
    return ok;
}

bool uncompressedTotal(const wchar_t* exePath, const Trailer& trailer, uint64_t& out) {
    if (!exePath) return false;
    mz_zip_archive zip;
    FILE* file = nullptr;
    if (!openPayloadReader(exePath, trailer, zip, file)) return false;
    bool ok = true;
    uint64_t total = 0;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            ok = false;
            break;
        }
        total += stat.m_uncomp_size;
    }
    mz_zip_reader_end(&zip);
    fclose(file);
    if (ok) out = total;
    return ok;
}

} // namespace dish::installer::payload
