# Security policy

This file covers the Dish Windows client. The same policy and the same
reporting channel apply across the TinkerNorth clients and the
[`satellite`](https://github.com/TinkerNorth/satellite) server.

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

### Response targets

| Severity | Acknowledgement | Initial assessment | Fix target |
|---|---|---|---|
| Critical (CVSS >= 9.0) | 1 business day | 3 business days | 14 days, coordinated disclosure |
| High (CVSS 7.0-8.9) | 2 business days | 5 business days | 30 days |
| Medium / Low | 5 business days | 10 business days | next minor release |

If we miss these, you may publish 90 days after the original report date
regardless.

### Scope

In scope: this repository, the release artifacts attached to its GitHub
Releases, the wire protocol, and the pairing flow.

Out of scope:

- Anything that requires the attacker to already have local privileges on the
  user's PC (Administrator, or the ability to write to `%LOCALAPPDATA%` or the
  install directory).
- The ViGEmBus driver the server side depends on. File with
  [nefarius/ViGEmBus](https://github.com/nefarius/ViGEmBus).
- Denial of service by raw network flooding. The data plane is unauthenticated
  UDP with no rate limit, which is a deliberate latency trade-off; mitigation
  belongs in the network fabric.

## Supported versions

The latest minor release is supported, the previous minor receives high and
critical backports for 90 days, and patch releases are issued on demand for the
latest minor. 1.0.0 is the first release, so as of it there is no previous
minor to backport to.

Windows 10 and Windows 11 x64 are the supported platforms.

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
  this PC. Configuration lives in `HKCU\Software\TinkerNorth\Dish`, and the
  pairing key is stored there for anyone running as you.

## How CI keeps vulnerable code out

On every pull request and every push to `main`, blocking:

- Action-pin lint. Every `uses:` line must reference a 40-char commit SHA.
- Allowlist expiry. `.security/allowlist.yaml` entries must carry `cve`,
  `reason`, `owner`, and an unexpired `expires`.
- OSV-Scanner over the worktree.
- Gitleaks secret scan.
- CodeQL `cpp` analysis, `security-extended` and `security-and-quality` query
  packs, on a Windows runner so MSVC-only constructs are covered.

Advisory, not blocking: GitHub `dependency-review-action` runs on pull requests
with `continue-on-error: true`, because it needs Advanced Security, which the
repository did not have while it was private.

`security.yml` also runs weekly on a schedule. On a `v*` tag, `release.yml`
re-runs the action-pin lint, allowlist expiry, OSV-Scanner, and gitleaks jobs
against the tagged commit before it builds anything.

## What a release actually contains

A GitHub Release for a `v*` tag carries five files:

- `dish-setup.exe`: the installer, compiled by Inno Setup from
  [`installer.iss`](installer.iss). A single self-extracting exe carrying the
  whole install image; behaviour and command line are documented in
  [`docs/INSTALLER.md`](docs/INSTALLER.md).
- `dish-windows.zip`: `dish.exe`, the Qt runtime DLLs and QML modules staged by
  `windeployqt`, and a `SHA256SUMS` text file listing every other file in the
  bundle.
- `latest.json`: the update manifest every installed copy polls. It names the
  version, and the URL, size and SHA-256 of the two downloadable assets.
- `SHA256SUMS`: a release asset in its own right, listing the SHA-256 of
  `dish-setup.exe`, `dish-windows.zip`, `latest.json` and the SBOM. This is a
  different file from the one inside the zip, which covers the bundle's
  contents.
- `dish-windows.spdx.json`: an SPDX-JSON SBOM of the bundle, generated by Syft
  via `anchore/sbom-action`.

Verify the installer:

```powershell
$expected = (Get-Content .\SHA256SUMS |
    Where-Object { $_ -match 'dish-setup\.exe' }).Split()[0]
$actual = (Get-FileHash .\dish-setup.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw "checksum mismatch" } else { "ok" }
```

Or the portable bundle, whose checksums travel inside it:

```powershell
Expand-Archive .\dish-windows.zip -DestinationPath .\dish-windows
$expected = Get-Content .\dish-windows\SHA256SUMS |
    Where-Object { $_ -match 'dish\.exe' } |
    ForEach-Object { $_.Split()[0] }
$actual = (Get-FileHash .\dish-windows\dish.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw "checksum mismatch" } else { "ok" }
```

Understand what those checks are worth. Nothing signs `SHA256SUMS`, in either
location, and the in-zip copy lives inside the file it describes. They catch a
truncated or corrupted download. They do not prove an artifact came from this
project, because anyone who could replace an asset could replace the checksum
file next to it. Until the gaps below are closed, the trust anchor is the GitHub
Release page itself.

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

**What this does not give you.** The binaries are unsigned, so nothing in the
chain proves authorship; it proves that the file the manifest described is the
file that ran. The manifest itself is only as trustworthy as HTTPS to
github.com and GitHub's control of the release. The staging directory is
`%LOCALAPPDATA%\Dish\updates\`, which is user-writable by construction: a
process already running as you can replace the staged installer, and the boot
re-hash then rejects it, but that same process could equally replace `dish.exe`.
An attacker with code execution as the user has already won, which is why that
class of finding is out of scope above.

**Elevation.** A per-user install never raises UAC, at install or at update. An
all-users install applies its update by self-elevating exactly once, and that
prompt asks the user to consent to an unsigned binary running from a
user-writable directory. That is inherent without code signing, it is stated
here rather than buried, and it is one more reason the per-user scope is the
default.

**Turning it off.** *Check for updates automatically* in Settings stops every
update-related request. The portable zip has no updater at all: it detects the
absence of an Inno Setup uninstaller (`unins*.exe`) beside it and downgrades to
notify-only.

## Known gaps

- **No release signing.** Neither `dish-setup.exe` nor `dish.exe` is
  Authenticode-signed, so Microsoft Defender SmartScreen shows "Windows
  protected your PC" the first time a freshly downloaded installer from a new
  release is run, and the way through is *More info*, then *Run anyway*. There
  are no cosign keyless signatures for any asset. Automatic updates are not
  interposed by SmartScreen, because a file an app writes itself carries no mark
  of the web; that makes the checksum chain above the only thing standing
  between a release and an installed machine, which is the strongest argument
  for closing this gap.
- **No build provenance.** SLSA provenance is not generated. There is no
  cryptographic link from an artifact back to the workflow run and commit that
  produced it.
- **No artifact vulnerability scan at release.** Dependencies are scanned in
  source form by OSV-Scanner and the dependency review; the built bundle is not
  scanned by Grype or an equivalent.
- **One SBOM format.** SPDX-JSON only. There is no CycloneDX SBOM.
- **`SHA256SUMS` is unsigned and in-band.** See above.

These are tracked in the roadmap section of
[`CONTRIBUTING.md`](CONTRIBUTING.md). Nothing in this document should be read
as claiming any of them exists today.
