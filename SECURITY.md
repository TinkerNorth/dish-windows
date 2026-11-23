# Security policy

This file covers Dish for Windows. The same policy and the same reporting
channel apply across the TinkerNorth family, each repository with its own copy
of this file:

- [Satellite](https://github.com/TinkerNorth/satellite), the server (Windows, Linux, macOS)
- [Dish for Android](https://github.com/TinkerNorth/dish-android)
- [Dish for iOS](https://github.com/TinkerNorth/dish-ios)
- [Dish for macOS](https://github.com/TinkerNorth/dish-mac)
- [Dish for Windows](https://github.com/TinkerNorth/dish-windows), this repository
- [Dish for Linux](https://github.com/TinkerNorth/dish-linux)
- The website, <https://dish.tinkernorth.com>

This file is the source of truth for reporting a vulnerability in Dish for
Windows, what happens to the report, and how anyone can verify a release.
[`CONTRIBUTING.md`](CONTRIBUTING.md), "Security", has the local commands for
the same checks CI runs.

## Reporting a vulnerability

**Do not file a public issue for a suspected vulnerability.**

Use either:

1. **GitHub private vulnerability reporting.** Open this repository, then
   *Security* > *Report a vulnerability*. Preferred, because it creates a
   tracked advisory and a private discussion thread.
2. **Email** `security@tinkernorth.com` (PGP key on request). Include the
   version (commit SHA or release tag), reproduction steps, and impact.

Please do not test exploits against infrastructure you do not own. The
on-LAN threat model below already assumes an attacker with packet-injection
ability on the local network; that is the documented design boundary, not a
bug.

### Response SLA

| Severity | Acknowledgement | Initial assessment | Fix target |
|---|---|---|---|
| Critical (CVSS >= 9.0) | 1 business day | 3 business days | 14 days, coordinated disclosure |
| High (CVSS 7.0-8.9) | 2 business days | 5 business days | 30 days |
| Medium / Low | 5 business days | 10 business days | next minor release |

If we miss these, you may publish 90 days after the original report date
regardless.

### Scope

In scope: this repository, the release artifacts attached to its GitHub
Releases, the wire protocol (protocol 4; the single source of truth is
[`satellite/docs/contract.md`](https://github.com/TinkerNorth/satellite/blob/main/docs/contract.md),
and [`docs/contract.md`](docs/contract.md) maps it onto this client), and the
pairing flow.

Out of scope:

- Anything that requires the attacker to already have local privileges on the
  user's PC (Administrator, or the ability to write to `%LOCALAPPDATA%` or the
  install directory).
- The virtual-controller drivers the server side depends on (ViGEmBus,
  HIDMaestro). File with [nefarius/ViGEmBus](https://github.com/nefarius/ViGEmBus)
  or the HIDMaestro project.
- Denial of service by raw network flooding. The data plane is unauthenticated
  UDP with no rate limit, which is a deliberate latency trade-off; mitigation
  belongs in the network fabric.

## Supported versions

| Product | Supported | Platform floor |
|---|---|---|
| Dish for Windows | latest minor; previous minor for 90 days | Windows 10 1809 (build 17763) or Windows 11, x64; ARM64 runs the x64 build under emulation |
| Satellite | latest minor; previous minor for 90 days | see its `SECURITY.md` |
| Dish for Android | latest minor; previous minor for 90 days | Android 7.0 (API 24) |
| Dish for iOS | latest minor; previous minor for 90 days | iOS 26 |
| Dish for macOS | latest minor; previous minor for 90 days | macOS 13 |
| Dish for Linux | latest minor; previous minor for 90 days | see its `SECURITY.md` |

For Dish for Windows: the latest minor is 2.8, so 2.8.x is supported and 2.7.x
receives high and critical fixes for 90 days after 2.8.0 (released
2026-11-23). Patch releases (`X.Y.Z+1`) are issued on demand for the latest
minor.

## Threat model

Dish sends gamepad input from this PC to a `satellite` server on the same
local network. Three surfaces matter.

### Discovery

The client listens for UDP broadcast beacons on port 9879 and browses mDNS.
Both are unauthenticated by construction. Anyone on the LAN can advertise a
server, and a beacon can claim any name and address. Discovery decides only
what appears in the list; it grants no trust. Trust comes from pairing.

### Pairing and TLS

Pairing and the REST control plane are HTTPS on port 9443. The satellite
presents a **self-signed certificate**, so there is no CA chain to validate.
The client pins the SHA-256 fingerprint of the certificate it first saw for a
given satellite id and rejects any later certificate that differs, keeping the
original pin (`src/core/net/Tofu.*`, `src/source/http/SatelliteTlsVerifier.*`).

What trust-on-first-use protects against: an attacker who joins the network
*after* you paired cannot impersonate your server, even with full control of
DNS, ARP, and routing. The fingerprint mismatch is a hard reject and the pin is
not overwritten.

What it does not protect against: an attacker already in position at the moment
of **first** contact. First contact trusts whatever certificate it is given.
Pair on a network you control.

The PIN carries the pairing key exactly once, over that pinned TLS channel.
Authenticated REST calls carry an HMAC-SHA256 proof of key possession rather
than the key itself. A PIN observed during the pairing window is enough for an
attacker on the same LAN to pair. Treat one as a password while it is live.

### Streaming

The data plane is UDP on port 9876, encrypted with ChaCha20-Poly1305 IETF using
a session key derived per session by HKDF-SHA256 from the pairing key, the
server-supplied session salt, and the session token. The raw pairing key never
encrypts a packet. The nonce is `direction(1) | 0x00 x7 | counter(4 BE)`, with
the direction byte keeping the two halves of a session from colliding, and the
4-byte token is the AAD.

This gives confidentiality and integrity for input reports and for the rumble,
lightbar, and battery return path. An observer on the LAN cannot read your
input or forge a packet the server will accept.

It does not hide **that** you are streaming, or to whom, or how much. Packet
timing and volume are visible, and gamepad input rate is not a subtle signal.
There is no padding and no cover traffic.

Replay is rejected in one direction only. The client drops a server-to-client
packet whose counter is less than or equal to the last one it accepted. Packets
are otherwise unordered UDP; a dropped packet is a dropped input frame, by
design.

There is no rate limit. Anyone who can reach port 9876 can make the client and
server spend cycles on packets that fail authentication.

### If you are on a hostile LAN

- Pair on a network you trust. The one moment TOFU cannot defend is first
  contact.
- A certificate-mismatch rejection means something changed. Either the server
  was reinstalled or someone is in the middle. Do not clear the pin to make the
  error go away without knowing which.
- Assume the fact and the volume of your session are observable.
- Nothing here defends against an attacker who already has code execution on
  this PC. Configuration lives in `HKCU\Software\TinkerNorth\Dish`. The
  pairing key and the Moonlight private key are stored there encrypted with
  DPAPI for your account, which keeps another account or a copied hive out
  and does nothing against a program running as you.

## How CI prevents vulnerable code from shipping

On every pull request and every push to `main`, blocking:

- Action-pin lint. Every `uses:` line must reference a 40-char commit SHA.
- Allowlist expiry. `.security/allowlist.yaml` entries must carry `cve`,
  `reason`, `owner`, and an unexpired `expires`.
- OSV-Scanner over the worktree.
- Gitleaks secret scan over the full history.
- CodeQL `cpp` analysis, `security-extended` query pack, on a Windows runner
  so MSVC-only constructs are covered; the results are uploaded to code
  scanning.
- GitHub `dependency-review-action` is wired into `security.yml` but switched
  off (`dependency_review_enabled: false`): the action needs the repository's
  Dependency graph setting, which is off, and fails without it.

`security.yml` also runs weekly on a schedule.

On a release tag, `release.yml`, also blocking:

- Re-runs the action-pin lint, allowlist expiry, OSV-Scanner and gitleaks
  against the tagged commit before it builds anything.
- Builds through the same presets and scripts as pull-request CI, runs the
  portable bundle and the install image, and round-trips the installer it is
  about to publish.
- Signs `dish-setup.exe` with Authenticode when the `WIN_SIGN_CERT_BASE64` and
  `WIN_SIGN_PASSWORD` secrets exist, and releases it unsigned with a warning
  when they do not (the repository variable `ENFORCE_RELEASE_SIGNING=true`
  turns that into a failure).
- The shared harden workflow (`_release_harden.yml`): an Anchore Grype scan
  that fails on CRITICAL or HIGH, Syft SBOMs in SPDX-JSON and CycloneDX-JSON,
  `SHA256SUMS` over every asset and SBOM, and cosign keyless signatures
  (`.sig` + `.crt`) for each of them.
- SLSA build provenance from `slsa-framework/slsa-github-generator`,
  `dish-windows.intoto.jsonl`.
- Uploads everything to a draft release and publishes it only after the
  release measurements pass, so `releases/latest` never points at a
  half-uploaded release.

## Verifying a release artifact (consumer recipe)

Run against 2.8.0 on 2026-11-23 with cosign 3.1.4; every step below printed
what it says.

### 0) Pre-requisites

```bash
brew install cosign gh        # macOS; or https://github.com/sigstore/cosign/releases
```

### 1) Download every file from the Release

```bash
gh release download 2.8.0 -R TinkerNorth/dish-windows -D ./release
cd release
```

2.8.0 carries `dish-setup.exe`, `dish-windows.zip`, `latest.json`,
`SHA256SUMS`, `dish-windows.sbom.spdx.json`, `dish-windows.sbom.cdx.json`, a
`.sig` and `.crt` beside each of those, `dish-windows.intoto.jsonl` (the SLSA
provenance) and `measurements-2.8.0.json` / `.md`. The measurements are
attached after the harden step, so neither the checksums nor the signatures
cover them.

### 2) Verify the checksum bundle

```bash
shasum -a 256 -c SHA256SUMS     # macOS; sha256sum -c SHA256SUMS on Linux
```

Every line says `OK`.

### 3) Verify cosign signed `SHA256SUMS` from the release workflow

The signing identity is the shared harden workflow that `release.yml` calls,
at the release tag:

```bash
cosign verify-blob \
  --certificate SHA256SUMS.crt \
  --signature   SHA256SUMS.sig \
  --certificate-identity-regexp '^https://github\.com/TinkerNorth/dish-windows/\.github/workflows/_release_harden\.yml@refs/tags/[0-9]+\.[0-9]+\.[0-9]+$' \
  --certificate-oidc-issuer 'https://token.actions.githubusercontent.com' \
  SHA256SUMS
```

Output ends with `Verified OK`. The same command with `dish-setup.exe`,
`dish-windows.zip` or `latest.json` in place of `SHA256SUMS` (and its own
`.crt` and `.sig`) verifies each asset on its own.

### 4) Check the portable bundle's own checksums

The zip carries a `SHA256SUMS` of its contents. It is not signed, but the zip
that carries it is (step 3):

```powershell
Expand-Archive .\dish-windows.zip -DestinationPath .\dish-windows
Push-Location .\dish-windows
Get-Content .\SHA256SUMS | ForEach-Object {
    $hash, $path = $_ -split '  ', 2
    if ((Get-FileHash $path -Algorithm SHA256).Hash.ToLower() -ne $hash) { throw "mismatch: $path" }
}
Pop-Location
```

## Auto-update trust model

An installed copy fetches `latest.json` from
`https://github.com/TinkerNorth/dish-windows/releases/latest/download/latest.json`,
and, when it names a newer version, downloads that release's `dish-setup.exe`
and runs it at the next start. What that chain does and does not guarantee:

**Transport.** Both requests use a dedicated `QNetworkAccessManager` with Qt's
default certificate validation against the Windows system root store, over
Schannel. Redirects are followed only to HTTPS. The trust-on-first-use verifier
the satellite path uses (`QSslSocket::VerifyNone` plus a pinned fingerprint,
because a satellite presents a self-signed certificate) is **never** installed
on the updater's transports; a comment at the construction site says so, and the
two code paths share no object. The final download host is deliberately
unpinned, because it is a GitHub CDN redirect and the checksum below is what
makes the identity of the host irrelevant.

**Integrity.** `latest.json` states the SHA-256 and the byte size of
`dish-setup.exe`. The download is hashed while it streams, re-hashed in full off
disk before it is promoted out of the staging directory, and hashed a third time
at the next boot immediately before the handoff. A mismatch at any of the three
points discards the file. Inside the installer, Inno Setup's own per-file CRC
checks reject a corrupted payload during extraction.

**Downgrade protection.** All of it lives in the app, deliberately ahead of the
spawn: an update is staged only when the manifest version is strictly greater
than the running version, the boot gate re-evaluates that against the version
of the exe actually on disk (so an update overtaken by a manual install is
discarded rather than applied), and the janitor deletes any stage at or below
the running version. The installer itself does not version-gate: running an
older `dish-setup.exe` by hand is an explicit user action and installs what it
carries.

**Yank.** Deleting a release un-stages it: any successful check whose version is
less than or equal to a staged version discards that stage. The window in which
a pulled build can still be applied is a cold start before the next successful
check. The build applied in that window is authentic, just withdrawn.

**What this does not give you.** The binaries carry no Authenticode signature
(see Known gaps), so nothing the updater checks proves authorship; it proves
that the file the manifest described is the file that ran. The cosign
signatures on the release assets do prove it, but the updater does not check
them. The manifest itself is only as trustworthy as HTTPS to
github.com and GitHub's control of the release. The staging directory is
`%LOCALAPPDATA%\Dish\updates\`, which is user-writable by construction: a
process already running as you can replace the staged installer, and the boot
re-hash then rejects it, but that same process could equally replace `dish.exe`.
An attacker with code execution as the user has already won, which is why that
class of finding is out of scope above.

**Elevation.** A per-user install never raises UAC, at install or at update. An
all-users install applies its update by self-elevating exactly once, and that
prompt asks the user to consent to a binary with no Authenticode signature
running from a user-writable directory. That is inherent until the installer is
signed, it is stated here rather than buried, and it is one more reason the
per-user scope is the default.

**Turning it off.** *Check for updates automatically* in Settings stops every
update-related request. The portable zip has no updater at all: it detects the
absence of an Inno Setup uninstaller (`unins*.exe`) beside it and downgrades to
notify-only.

## Known gaps

- **No Authenticode signature yet.** `release.yml` signs `dish-setup.exe`
  whenever the certificate secrets exist, and none is configured: the 2.8.0
  installer has an empty certificate table. Microsoft Defender SmartScreen
  therefore shows "Windows protected your PC" the first time a freshly
  downloaded installer from a new release runs (*More info*, then *Run
  anyway*). `dish.exe` and `crashpad_handler.exe` inside it are not signed
  either. Automatic updates are not interposed by SmartScreen, because a file
  an app writes itself carries no mark of the web, so the updater's checksum
  chain is what stands between a release and an installed machine.
- **The SLSA provenance does not verify with `slsa-verifier`.**
  `slsa-verifier verify-artifact` (2.7.1) rejects 2.8.0's
  `dish-windows.intoto.jsonl` with `invalid ref: "f7dd8c5...": unexpected ref
  type`: the generator is called by commit SHA, and the verifier accepts only a
  builder referenced by tag. The cosign signatures above are the verifiable
  link to the workflow run until that changes.
- **The SBOMs list almost nothing.** 2.8.0's SPDX SBOM has two packages (the
  app and the release directory); Syft does not see the vcpkg libraries, Qt or
  the CRT inside the bundle. [`THIRD_PARTY.md`](THIRD_PARTY.md) is the
  complete list.
- **The in-zip `SHA256SUMS` is in-band.** It lives inside the file it
  describes; it is worth something only after step 3 has verified the zip.

Nothing in this document should be read as claiming more than it states.
