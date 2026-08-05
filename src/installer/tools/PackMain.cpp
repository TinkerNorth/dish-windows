// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// dish-payload-pack: builds dish-setup.exe out of a stub and a staged install
// image. Writes manifest.json into the image (hashing every file, applying the
// uninstall.exe -> dish-setup-ui.exe alias), zips the image (standard zip,
// DEFLATE level 9, no timestamps so packs are deterministic), appends
// zip + trailer to a copy of the stub, self-verifies the result and prints a
// per-component size report into the build log.
//
// Deliberately Qt-free (payload layer + miniz + Win32/BCrypt only) so the tool
// builds early and standalone; the engine's Qt-side Manifest.cpp parses what
// this writes.
//
// Usage:
//   dish-payload-pack --stub <stub.exe> --image <dir> --out <dish-setup.exe>
//                     --version <M.m.p> [--version-override <M.m.p>]

#include "../PayloadFormat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../../third_party/miniz/miniz.h"

// The CMake target list is spec-pinned and does not name bcrypt; resolve the
// SHA-256 dependency here instead.
#pragma comment(lib, "bcrypt")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace payload = dish::installer::payload;

namespace {

constexpr size_t kIoChunk = 256 * 1024;

struct ImageFile {
    std::wstring absPath;
    std::string zipName; // ASCII, forward slashes, image-relative
    uint64_t size = 0;
    std::string sha256Hex;
};

struct ManifestEntry {
    std::string path;     // installed relative path
    std::string stagedAs; // image file to copy from; empty = same as path
    uint64_t size = 0;
    std::string sha256Hex;
};

int fail(const wchar_t* what, const std::wstring& detail = std::wstring()) {
    if (detail.empty())
        fwprintf(stderr, L"dish-payload-pack: error: %ls\n", what);
    else
        fwprintf(stderr, L"dish-payload-pack: error: %ls: %ls\n", what, detail.c_str());
    return 1;
}

int usage() {
    fwprintf(stderr,
             L"Usage: dish-payload-pack --stub <stub.exe> --image <dir> --out <dish-setup.exe>\n"
             L"                         --version <M.m.p> [--version-override <M.m.p>]\n");
    return 2;
}

bool isStrictTripleVersion(const std::wstring& v) {
    int part = 0;
    size_t i = 0;
    while (part < 3) {
        const size_t start = i;
        while (i < v.size() && v[i] >= L'0' && v[i] <= L'9') ++i;
        if (i == start) return false;
        ++part;
        if (part < 3) {
            if (i >= v.size() || v[i] != L'.') return false;
            ++i;
        }
    }
    return i == v.size();
}

std::wstring absolutePath(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring full(needed, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) return path;
    full.resize(written);
    return full;
}

// Image-relative wide path (backslashes) -> zip entry name. Fails on anything
// outside printable ASCII: entry names are enforced ASCII by the format.
bool toZipName(const std::wstring& relWin, std::string& out) {
    out.clear();
    out.reserve(relWin.size());
    for (const wchar_t w : relWin) {
        if (w == L'\\') {
            out.push_back('/');
            continue;
        }
        if (w < 0x20 || w > 0x7E) return false;
        out.push_back(static_cast<char>(w));
    }
    return payload::isSafeEntryName(out.c_str());
}

bool walkImage(const std::wstring& rootAbs, const std::wstring& relWin, std::vector<ImageFile>& out,
               std::wstring& badPath) {
    const std::wstring dir = relWin.empty() ? rootAbs : rootAbs + L"\\" + relWin;
    WIN32_FIND_DATAW find{};
    const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) {
        badPath = dir;
        return false;
    }
    bool ok = true;
    do {
        if (std::wcscmp(find.cFileName, L".") == 0 || std::wcscmp(find.cFileName, L"..") == 0)
            continue;
        const std::wstring childRel =
            relWin.empty() ? find.cFileName : relWin + L"\\" + find.cFileName;
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = walkImage(rootAbs, childRel, out, badPath);
            if (!ok) break;
            continue;
        }
        if (childRel == L"manifest.json") continue; // stale from a previous pack; regenerated
        ImageFile file;
        file.absPath = rootAbs + L"\\" + childRel;
        file.size = (static_cast<uint64_t>(find.nFileSizeHigh) << 32) | find.nFileSizeLow;
        if (!toZipName(childRel, file.zipName)) {
            badPath = childRel;
            ok = false;
            break;
        }
        out.push_back(std::move(file));
    } while (FindNextFileW(h, &find));
    FindClose(h);
    return ok;
}

bool sha256File(const std::wstring& path, std::string& hexOut) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    std::vector<unsigned char> buf(kIoChunk);
    unsigned char digest[32] = {};
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        if (NT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
            ok = true;
            for (;;) {
                const size_t n = fread(buf.data(), 1, buf.size(), f);
                if (n > 0 &&
                    !NT_SUCCESS(BCryptHashData(hash, buf.data(), static_cast<ULONG>(n), 0))) {
                    ok = false;
                    break;
                }
                if (n < buf.size()) {
                    if (ferror(f)) ok = false;
                    break;
                }
            }
            if (ok) ok = NT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    fclose(f);
    if (!ok) return false;
    static const char* hexDigits = "0123456789abcdef";
    hexOut.clear();
    hexOut.reserve(64);
    for (const unsigned char b : digest) {
        hexOut.push_back(hexDigits[b >> 4]);
        hexOut.push_back(hexDigits[b & 0x0F]);
    }
    return true;
}

// Entry names/versions are pre-validated ASCII without quotes or backslashes;
// escape anyway so the writer is correct on its own.
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string buildManifestJson(const std::string& version, uint64_t totalBytes,
                              const std::vector<ManifestEntry>& entries) {
    std::string json;
    json += "{\n";
    json += "  \"schema\": 1,\n";
    json += "  \"version\": \"" + jsonEscape(version) + "\",\n";
    json += "  \"totalBytes\": " + std::to_string(totalBytes) + ",\n";
    json += "  \"files\": [\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const ManifestEntry& e = entries[i];
        json += "    { \"path\": \"" + jsonEscape(e.path) +
                "\", \"size\": " + std::to_string(e.size) + ", \"sha256\": \"" + e.sha256Hex + "\"";
        if (!e.stagedAs.empty()) json += ", \"stagedAs\": \"" + jsonEscape(e.stagedAs) + "\"";
        json += i + 1 < entries.size() ? " },\n" : " }\n";
    }
    json += "  ]\n";
    json += "}\n";
    return json;
}

bool writeFileBytes(const std::wstring& path, const std::string& bytes) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return fclose(f) == 0 && ok;
}

bool fileSizeOf(const std::wstring& path, uint64_t& out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    out = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

const char* componentFor(const std::string& zipName) {
    if (zipName == "dish.exe") return "app";
    if (zipName == "dish-setup-ui.exe") return "setup ui";
    if (zipName == "uninstall-helper.exe") return "uninstall helper";
    if (zipName == "manifest.json") return "manifest";
    if (zipName.rfind("licenses/", 0) == 0) return "licenses";
    if (zipName == "msvcp140.dll" || zipName == "msvcp140_1.dll" || zipName == "msvcp140_2.dll" ||
        zipName == "vcruntime140.dll" || zipName == "vcruntime140_1.dll")
        return "vc++ crt";
    return "qt runtime";
}

double toMb(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

// Reopens the finished artifact and prints the per-component size report.
bool printSizeReport(const std::wstring& outPath, const payload::Trailer& trailer) {
    FILE* f = _wfopen(outPath.c_str(), L"rb");
    if (!f) return false;
    if (_fseeki64(f, static_cast<long long>(trailer.zipOffset), SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_cfile(&zip, f, trailer.zipSize, 0)) {
        fclose(f);
        return false;
    }
    struct Row {
        const char* name;
        int files = 0;
        uint64_t uncompressed = 0;
        uint64_t compressed = 0;
    };
    Row rows[] = {{"app"},      {"setup ui"}, {"uninstall helper"}, {"qt runtime"},
                  {"vc++ crt"}, {"licenses"}, {"manifest"}};
    Row total{"total"};
    bool ok = true;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            ok = false;
            break;
        }
        const char* component = componentFor(stat.m_filename);
        for (Row& row : rows) {
            if (std::strcmp(row.name, component) != 0) continue;
            row.files += 1;
            row.uncompressed += stat.m_uncomp_size;
            row.compressed += stat.m_comp_size;
            break;
        }
        total.files += 1;
        total.uncompressed += stat.m_uncomp_size;
        total.compressed += stat.m_comp_size;
    }
    mz_zip_reader_end(&zip);
    fclose(f);
    if (!ok) return false;

    printf("  %-18s %6s %14s %14s\n", "component", "files", "uncompressed", "compressed");
    for (const Row& row : rows) {
        if (row.files == 0) continue;
        printf("  %-18s %6d %11.1f MB %11.1f MB\n", row.name, row.files, toMb(row.uncompressed),
               toMb(row.compressed));
    }
    printf("  %-18s %6d %11.1f MB %11.1f MB\n", total.name, total.files, toMb(total.uncompressed),
           toMb(total.compressed));
    const uint64_t finalSize = trailer.zipOffset + trailer.zipSize + sizeof(payload::Trailer);
    printf("  stub %.1f MB + payload.zip %.1f MB + trailer = dish-setup.exe %.1f MB\n",
           toMb(trailer.zipOffset), toMb(trailer.zipSize), toMb(finalSize));
    return true;
}

bool writePayloadZip(const std::wstring& zipPath, const std::vector<ImageFile>& files,
                     std::wstring& badPath) {
    FILE* out = _wfopen(zipPath.c_str(), L"wb");
    if (!out) {
        badPath = zipPath;
        return false;
    }
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_cfile(&zip, out, 0)) {
        fclose(out);
        badPath = zipPath;
        return false;
    }
    bool ok = true;
    for (const ImageFile& file : files) {
        FILE* src = _wfopen(file.absPath.c_str(), L"rb");
        if (!src) {
            badPath = file.absPath;
            ok = false;
            break;
        }
        // No timestamp (null pFile_time) => DOS epoch in every entry, so
        // identical inputs produce identical archives.
        const mz_bool added =
            mz_zip_writer_add_cfile(&zip, file.zipName.c_str(), src, file.size, nullptr, nullptr, 0,
                                    MZ_BEST_COMPRESSION, nullptr, 0, nullptr, 0);
        fclose(src);
        if (!added) {
            badPath = file.absPath;
            ok = false;
            break;
        }
    }
    if (ok && !mz_zip_writer_finalize_archive(&zip)) {
        badPath = zipPath;
        ok = false;
    }
    mz_zip_writer_end(&zip);
    if (fclose(out) != 0) ok = false;
    return ok;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring stubPath;
    std::wstring imageDir;
    std::wstring outPath;
    std::wstring version;
    std::wstring versionOverride;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == L"--stub" && hasValue)
            stubPath = argv[++i];
        else if (arg == L"--image" && hasValue)
            imageDir = argv[++i];
        else if (arg == L"--out" && hasValue)
            outPath = argv[++i];
        else if (arg == L"--version" && hasValue)
            version = argv[++i];
        else if (arg == L"--version-override" && hasValue)
            versionOverride = argv[++i];
        else
            return usage();
    }
    const std::wstring effectiveVersion = versionOverride.empty() ? version : versionOverride;
    if (stubPath.empty() || imageDir.empty() || outPath.empty() || effectiveVersion.empty())
        return usage();
    if (!isStrictTripleVersion(effectiveVersion))
        return fail(L"version must be MAJOR.MINOR.PATCH", effectiveVersion);

    while (!imageDir.empty() && (imageDir.back() == L'\\' || imageDir.back() == L'/'))
        imageDir.pop_back();
    const std::wstring imageAbs = absolutePath(imageDir);
    const DWORD imageAttributes = GetFileAttributesW(imageAbs.c_str());
    if (imageAttributes == INVALID_FILE_ATTRIBUTES || !(imageAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return fail(L"image directory not found", imageAbs);
    if (GetFileAttributesW(stubPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return fail(L"stub not found", stubPath);

    // 1. Collect the image (excluding any stale manifest.json).
    std::vector<ImageFile> files;
    std::wstring badPath;
    if (!walkImage(imageAbs, std::wstring(), files, badPath))
        return fail(L"unreadable or non-ASCII image path", badPath);
    if (files.empty()) return fail(L"image directory is empty", imageAbs);

    // 2. Hash everything once; manifest entries and the alias reuse these.
    for (ImageFile& file : files) {
        if (!sha256File(file.absPath, file.sha256Hex)) return fail(L"hashing failed", file.absPath);
    }

    // 3. Manifest: every installed file, minus the image-only pair, plus the
    // uninstall.exe alias (the UI binary is stored once, D9).
    const ImageFile* setupUi = nullptr;
    for (const ImageFile& file : files) {
        if (file.zipName == "dish-setup-ui.exe") setupUi = &file;
        if (file.zipName == "uninstall.exe")
            return fail(L"image must not contain uninstall.exe (it is an alias)", file.absPath);
    }
    if (!setupUi) return fail(L"image is missing dish-setup-ui.exe", imageAbs);

    std::vector<ManifestEntry> entries;
    entries.reserve(files.size() + 1);
    for (const ImageFile& file : files) {
        if (file.zipName == "dish-setup-ui.exe") continue; // not installed under its own name
        ManifestEntry entry;
        entry.path = file.zipName;
        entry.size = file.size;
        entry.sha256Hex = file.sha256Hex;
        entries.push_back(std::move(entry));
    }
    ManifestEntry alias;
    alias.path = "uninstall.exe";
    alias.stagedAs = "dish-setup-ui.exe";
    alias.size = setupUi->size;
    alias.sha256Hex = setupUi->sha256Hex;
    entries.push_back(std::move(alias));
    std::sort(entries.begin(), entries.end(),
              [](const ManifestEntry& a, const ManifestEntry& b) { return a.path < b.path; });
    uint64_t totalBytes = 0;
    for (const ManifestEntry& entry : entries) totalBytes += entry.size;

    std::string versionUtf8;
    versionUtf8.reserve(effectiveVersion.size());
    for (const wchar_t c : effectiveVersion) versionUtf8.push_back(static_cast<char>(c));

    const std::wstring manifestPath = imageAbs + L"\\manifest.json";
    if (!writeFileBytes(manifestPath, buildManifestJson(versionUtf8, totalBytes, entries)))
        return fail(L"could not write", manifestPath);

    // 4. Zip the image (fresh manifest included), sorted for determinism.
    ImageFile manifestFile;
    manifestFile.absPath = manifestPath;
    manifestFile.zipName = "manifest.json";
    if (!fileSizeOf(manifestPath, manifestFile.size)) return fail(L"could not stat", manifestPath);
    files.push_back(std::move(manifestFile));
    std::sort(files.begin(), files.end(),
              [](const ImageFile& a, const ImageFile& b) { return a.zipName < b.zipName; });

    const std::wstring zipTemp = outPath + L".payload.tmp";
    if (!writePayloadZip(zipTemp, files, badPath)) {
        DeleteFileW(zipTemp.c_str());
        return fail(L"could not build payload.zip", badPath);
    }

    // 5. stub copy + zip + trailer = dish-setup.exe.
    if (GetFileAttributesW(outPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        SetFileAttributesW(outPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileW(stubPath.c_str(), outPath.c_str(), FALSE)) {
        DeleteFileW(zipTemp.c_str());
        return fail(L"could not copy the stub to", outPath);
    }
    SetFileAttributesW(outPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    const bool appended = payload::appendZipAndTrailer(outPath.c_str(), zipTemp.c_str());
    DeleteFileW(zipTemp.c_str());
    if (!appended) return fail(L"could not append payload + trailer to", outPath);

    // 6. Self-verify exactly like the stub will, then report.
    payload::Trailer trailer{};
    if (!payload::readTrailer(outPath.c_str(), trailer) ||
        !payload::checkOverlayCrc(outPath.c_str(), trailer))
        return fail(L"self-verification failed for", outPath);
    printf("dish-payload-pack: packed version %s (%d files, %llu manifest bytes)\n",
           versionUtf8.c_str(), static_cast<int>(entries.size()),
           static_cast<unsigned long long>(totalBytes));
    if (!printSizeReport(outPath, trailer)) return fail(L"could not read back", outPath);
    return 0;
}
