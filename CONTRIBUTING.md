# Contributing to Dish Windows

Thanks for your interest in improving the Windows client! This document
captures the conventions that aren't obvious from skimming the code.

## Getting set up

```powershell
# 1) Install build deps (see README "Install build dependencies")
# 2) Generate compile_commands.json + run the test suite
scripts\build.ps1 debug test
# 3) Point git at the in-tree pre-commit hook
scripts\setup-hooks.ps1
```

The pre-commit hook runs `clang-format -i` (autofix, re-stages) and
`clang-tidy -p build-debug` (advisory) on staged C++ files. It skips
gracefully if the tools aren't installed — CI re-runs `clang-format
--dry-run --Werror` and `clang-tidy` in strict mode, so anything that
slips locally fails the PR. The hook runs under Git for Windows' bundled
bash; no WSL required.

## License headers

Every source file (`*.h`, `*.hpp`, `*.cpp`) starts with:

```cpp
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
```

New files must include both lines. Don't introduce code under a different
license — the project is LGPL-3.0-or-later end-to-end (`LICENSE`,
`COPYING.GPL3`, source headers).

## Style

- C++17, four-space indent, 100-column soft limit. `.clang-format` is
  authoritative — run `clang-format -i` if you're unsure. Same config as
  dish-linux, dish-mac (JNI layer), dish-android (JNI layer), satellite.
- Warnings are enforced as errors on first-party targets (`dish_strict`).
  See `CMakeLists.txt` for the exact set. MSVC: `/W4 /permissive- /WX`.
  clang-cl / MinGW: `-Wall -Wextra -Wpedantic -Wshadow ...` matching the
  Linux flag set so a contributor on either toolchain sees the same
  warnings.
- Match the surrounding style. Headers go in the order: project, Qt, libs,
  std, separated by blank lines (see `src/AppModel.h` for the pattern).

## Branching & PRs

- All changes land on `main` via pull request — no direct pushes.
- Use the PR template (`.github/pull_request_template.md`) to describe
  the change, the manual test matrix you ran, and call out anything that
  touches the wire protocol.
- Keep commits focused; squash noisy fixup commits before review.

## What CI runs

Build + style:

- `windows-ci.yml`: `clang-format --dry-run --Werror`, Debug build +
  `ctest` (Catch2 suite under `tests/`), `clang-tidy -p build` over
  `src/` (UI excluded — MOC false-positives), Release build that uploads
  the Qt-bundled `dish.exe` as a CI artifact.

Security gates (also blocking):

- `security.yml`: action-pin lint, vulnerability allowlist expiry,
  OSV-Scanner against the worktree, gitleaks secret scan, GitHub
  `dependency-review-action`.
- `codeql.yml`: CodeQL `cpp` analysis (security-extended +
  security-and-quality query packs) on a Windows runner so MSVC-only
  constructs are covered.

Reproduce build steps locally with `scripts\build.ps1 debug test`.

## Security

### Adding a vulnerability allowlist entry

Open a PR that adds an entry to [`.security/allowlist.yaml`](.security/allowlist.yaml)
(see the schema in the file). Required fields: `cve`, `reason`, `owner`,
`expires`. CI rejects the PR if any field is missing or `expires` is in
the past. Renew or remove on or before `expires` — there is no silent
suppression.

### Running security checks locally

```powershell
# Action-pin lint (40-char SHA enforcement on every uses: line)
$bad = git ls-files .github/workflows/ |
    ForEach-Object { Select-String -Path $_ -Pattern '^\s*uses:' } |
    Where-Object { $_.Line -notmatch '@[0-9a-f]{40}\b' }
if ($bad) { $bad; throw "unpinned actions" } else { "all pinned" }

# Allowlist expiry — Python or PowerShell both work; Python form matches
# the dish-linux script verbatim so the regression test text stays in sync.
python -c @"
import datetime, yaml, sys
data = yaml.safe_load(open('.security/allowlist.yaml').read()) or {}
for e in data.get('exceptions', []) or []:
    if datetime.date.fromisoformat(str(e['expires'])) < datetime.date.today():
        print('EXPIRED:', e); sys.exit(1)
"@

# OSV-Scanner (install: scoop install osv-scanner, or download from releases)
osv-scanner --recursive --skip-git .

# Gitleaks (install: scoop install gitleaks, or download from releases)
gitleaks detect --no-banner --redact --source .
```

### Verifying a release artifact

Each GitHub Release ships the `dish-windows.zip` bundle (exe + bundled Qt
DLLs), `dish-windows.spdx.json` (SBOM), and a `SHA256SUMS` text file
inside the zip. Cosign keyless signing + SLSA L3 provenance are on the
roadmap — see `HANDOFF.md` item 6.

```powershell
# SHA256 verification
$expected = Get-Content .\dish-windows\SHA256SUMS |
    Where-Object { $_ -match 'dish\.exe' } |
    ForEach-Object { $_.Split()[0] }
$actual = (Get-FileHash .\dish-windows\dish.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw "checksum mismatch" } else { "ok" }
```

The full cross-repo verification recipe lives in
[`SECURITY.md`](SECURITY.md).

## Touching the hot path

The SDL gamepad thread runs at controller polling rate and must never
block on the UI thread or take a heap allocation. If you're modifying
`SDLGamepadBridge`, `GamepadInputProcessor`, or
`SatelliteClient::sendReport`:

- No `QObject::connect` cross-thread signals on the send path.
- No `std::mutex` longer than the existing routing-table lookup.
- No allocations per packet — use the preallocated buffer.
- Preserve `IP_TOS = 0xB8` (DSCP EF) on every send. Windows may strip it
  silently, but the call stays in place so an admin-tweaked registry can
  honour it.
- Winsock has no `MSG_NOSIGNAL` and doesn't need one (no SIGPIPE on
  Windows). The constant is `#define`d to `0` for source compatibility.

## Touching the wire protocol

The Windows, Linux, macOS, and Android clients all talk to the same
`satellite` server and must produce byte-identical traffic:

- AEAD: ChaCha20-Poly1305 IETF, 12-byte big-endian nonce derived from a
  monotonic counter.
- Packet layout: `token(4) | counter(4) | ciphertext+tag`, with the
  4-byte token as AAD.
- XUSB report: 12 bytes, little-endian.
- Ports: discovery UDP 9879, pairing TCP 9878, HTTP TCP 9877,
  streaming UDP 9876.

Any change here must be coordinated with `dish-android`, `dish-mac`,
`dish-linux`, and `satellite` in the same PR / release cycle.

## clang-tidy triage

`.clang-tidy`'s `WarningsAsErrors: ''` is intentional: clang-tidy is run
in CI as an advisory linter, not a gate. The remaining warnings on the
non-UI sources are all stylistic and tracked here for future cleanup
PRs (carried over from dish-linux verbatim — the same code triggers the
same checks):

| Check                                        | Notes                                          |
| -------------------------------------------- | ---------------------------------------------- |
| `modernize-use-nodiscard`                    | Add `[[nodiscard]]` to value-returning getters |
| `modernize-use-scoped-lock`                  | `std::lock_guard` → `std::scoped_lock`         |
| `readability-braces-around-statements`       | Single-statement `if`/`for` bodies             |
| `readability-identifier-naming`              | `Theme::` constexpr need the `k` prefix        |
| `cppcoreguidelines-avoid-c-arrays`           | Mostly fixed-size buffers — review case-by-case |
| `cppcoreguidelines-special-member-functions` | Rule-of-five on classes that own resources    |
| `performance-enum-size`                      | Underlying type narrower than `int`            |

Suppressions intentionally enabled in `.clang-tidy`:

- `-portability-avoid-pragma-once` — the project uses `#pragma once`
  everywhere by convention.

Anything new should land at zero net additional warnings on the
non-UI scope:

```powershell
$files = git ls-files 'src/*.cpp' 'src/*.h' | Where-Object { $_ -notlike 'src/UI/*' }
foreach ($f in $files) { clang-tidy -p build-debug --quiet $f }
```

UI files are excluded from CI's clang-tidy step because Qt's MOC-generated
code triggers a long tail of false positives.

## Reporting bugs

Use the issue templates under `.github/ISSUE_TEMPLATE/`. Include the
Windows build number (`winver` or
`[System.Environment]::OSVersion.Version`), Qt/SDL/libsodium versions
(`scripts\build.ps1 debug` prints them at the top of the configure step),
and the contents of `%LOCALAPPDATA%\TinkerNorth\Dish.log` if the app
crashed.
