# miniz (vendored)

Single-file ZIP + DEFLATE + CRC-32 library used by the Dish installer: the
`dish-setup.exe` stub extracts the appended `payload.zip` with it, and
`dish-payload-pack` writes that zip with it. Vendored rather than pulled from
vcpkg because the stub is a static-CRT (`/MT`) binary while the project's
`x64-windows` triplet is `/MD`; manifest mode cannot mix triplets per
dependency (spec decision, docs/INSTALLER.md).

## Provenance

- Project: https://github.com/richgel999/miniz
- Version: **3.0.2** (release amalgamation; `MZ_VERSION` inside the header
  reports the zlib-style pseudo-version `11.0.2`)
- Upstream artifact:
  https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
- License: MIT (see `LICENSE` in this directory, copied verbatim from the
  release archive; also recorded in THIRD_PARTY.md and
  assets/licenses/licenses.json)

SHA-256 of the vendored files (and of the release archive they came from):

```
ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5  miniz-3.0.2.zip (upstream release asset)
0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740  miniz.c
295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b  miniz.h
0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3  LICENSE
```

`miniz.c` and `miniz.h` are byte-identical to the files inside the release
archive. Do NOT edit them: compile-flag relaxations (warning level, never
`/WX`) belong in the consuming CMake targets, and `third_party/` sits outside
the `src/` globs so clang-format, clang-tidy and the repo lint gates never
touch it by design.

## Threat model note

osv-scanner cannot see vendored sources; this is an accepted, documented risk.
miniz only ever parses archives our own build produced, and only after the
stub's whole-payload CRC-32 trailer gate has passed; entry names are
additionally re-validated by `dish::installer::payload::isSafeEntryName`
before extraction.

## Update procedure

1. Pick the new upstream release tag and download its amalgamation zip:
   `https://github.com/richgel999/miniz/releases/download/<ver>/miniz-<ver>.zip`.
2. Extract; copy `miniz.c`, `miniz.h` and `LICENSE` over this directory
   verbatim (no local patches, ever).
3. Recompute `sha256sum` for all three files plus the release zip and update
   the table above, THIRD_PARTY.md, and assets/licenses/licenses.json.
4. Verify the APIs the installer uses still exist and behave the same:
   `mz_crc32`, `mz_zip_reader_init_cfile`, `mz_zip_reader_file_stat`,
   `mz_zip_reader_extract_to_callback` (must keep verifying per-entry CRC-32),
   `mz_zip_reader_is_file_a_directory`, `mz_zip_writer_init_cfile`,
   `mz_zip_writer_add_cfile`, `mz_zip_writer_finalize_archive`.
5. Build `dish_setup_exe`, run `ctest -R installer` (payload round-trip
   suite), and run `scripts/test-installer-roundtrip.ps1` against the fresh
   `dish-setup.exe`.
6. Run `gitleaks detect` before pushing; if miniz's tables trip entropy rules,
   extend the anchored `^third_party/miniz/` allowlist in `.gitleaks.toml` in
   the same PR.
