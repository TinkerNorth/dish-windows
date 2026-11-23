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
- OSV-Scanner 2.7.1 over the worktree and over every library `dish.exe` links,
  named by the upstream commit its pin resolves to (`scripts/osv-lockfile.sh`:
  the vcpkg ports at the baseline, ENet, Qt), with `.security/allowlist.yaml`
  as the only place a finding can be accepted.
- gitleaks 8.30.1 over the full history.
- CodeQL `cpp` analysis, `security-extended` query pack, on a Windows runner
  so MSVC-only constructs are covered; the results are uploaded to code
  scanning.
- GitHub `dependency-review-action` on pull requests, blocking on a
  high-severity finding in the dependencies a pull request changes.

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
- The shared harden workflow (`_release_harden.yml`, byte-identical across
  the fleet): Grype 0.122.1 fails on CRITICAL or HIGH, Syft 1.57.2 writes the
  SPDX-JSON and CycloneDX-JSON SBOMs, `SHA256SUMS` covers every asset and SBOM,
  and cosign 3.1.4 signs each file keyless into one Sigstore bundle
  (`<file>.sigstore.json`). Every pull request runs the same workflow in
  dry-run mode on that pull request's own installer and zip.
- SLSA build provenance from `slsa-framework/slsa-github-generator`,
  `dish-windows.intoto.jsonl`.
- Uploads everything to a draft release and publishes it only after the
  release measurements pass, so `releases/latest` never points at a
  half-uploaded release.

## Verifying a release artifact (consumer recipe)

Releases cut after the shared release-hardening workflow landed carry one
Sigstore bundle per file (`<file>.sigstore.json`); releases up to 2.8.0 carry a
`.sig` and a `.crt` per file and are verified the second way below. The tag is
bare `X.Y.Z` in every command.

### 0) Pre-requisites

A pinned cosign 3, checked against the upstream checksum file:

```bash
curl -fsSLO https://github.com/sigstore/cosign/releases/download/v3.1.4/cosign-darwin-arm64
echo "9f9b8cd557cf2e5b3df3373a00baa005a86b02391e3e06dd0ce37b3a1533d3a4  cosign-darwin-arm64" | shasum -a 256 -c -
chmod +x cosign-darwin-arm64 && mv cosign-darwin-arm64 cosign
# Linux: cosign-linux-amd64, 424e2c97d0b45fd7a6b608a87f090f35633f4c23ece5f092d48bba11aaf4bbfa
# Windows: cosign-windows-amd64.exe, cbe953787074611f1334cbf37dc1cf953995bb8c45398eb4fc7bb781d29c1f2a
# (all three from https://github.com/sigstore/cosign/releases/download/v3.1.4/cosign_checksums.txt)
```

### 1) Download every file from the Release

```bash
TAG=X.Y.Z
REPO=dish-windows
gh release download "$TAG" -R "TinkerNorth/$REPO" -D "$REPO-$TAG"
cd "$REPO-$TAG"
```

A release carries `dish-setup.exe`, `dish-windows.zip`, `latest.json`,
`dish-windows-components.spdx.json` (the bill of materials of what the bundle
ships, from releases after 2.8.0), `SHA256SUMS`, the Syft SBOMs
`dish-windows.sbom.spdx.json` and `dish-windows.sbom.cdx.json`, their
signatures, `dish-windows.intoto.jsonl` (the SLSA provenance) and
`measurements-<version>.json` / `.md`. The measurements are attached after the
harden step, so neither the checksums nor the signatures cover them.

### 2) Releases after 2.8.0: one bundle per file

```bash
# SHA256SUMS was signed by this repository's release workflow, on this tag.
cosign verify-blob \
  --bundle SHA256SUMS.sigstore.json \
  --certificate-identity "https://github.com/TinkerNorth/$REPO/.github/workflows/_release_harden.yml@refs/tags/$TAG" \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  SHA256SUMS

# Every file you downloaded matches it (macOS: shasum -a 256 --ignore-missing -c).
sha256sum --ignore-missing -c SHA256SUMS
```

The identity is exact, not a regular expression, and names
`_release_harden.yml` because the certificate's identity is the workflow that
ran the signing step. To verify one file on its own, run the same
`cosign verify-blob` with `--bundle <file>.sigstore.json` and `<file>`. This
form applies from the first release cut with it and has not been run yet: 2.8.0
is the latest release.

### 3) Releases up to 2.8.0: `.sig` and `.crt`

cosign 3 still verifies them with the deprecated flags (it prints two
deprecation lines); the identity is the same harden workflow:

```bash
cosign verify-blob \
  --certificate SHA256SUMS.crt --signature SHA256SUMS.sig \
  --certificate-identity "https://github.com/TinkerNorth/$REPO/.github/workflows/_release_harden.yml@refs/tags/$TAG" \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  SHA256SUMS
shasum -a 256 --ignore-missing -c SHA256SUMS
```

Run against 2.8.0 on 2026-11-23 with the cosign 3.1.4 above: `Verified OK`, and
every downloaded file `OK`.

### 4) The SLSA L3 provenance

`dish-windows.intoto.jsonl` is a Sigstore bundle whose statement names every
released file by SHA-256. It is signed by the SLSA generator at the exact
commit `release.yml` pins, which `slsa-verifier` refuses ("invalid ref")
because that is not a release tag, so verify it with cosign:

```bash
cosign verify-blob-attestation \
  --bundle dish-windows.intoto.jsonl --new-bundle-format --type slsaprovenance \
  --certificate-identity "https://github.com/slsa-framework/slsa-github-generator/.github/workflows/generator_generic_slsa3.yml@f7dd8c54c2067bafc12ca7a55595d5ee9b75204a" \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  dish-setup.exe
```

Run against 2.8.0's `dish-setup.exe` with cosign 3.1.4: `Verified OK`. The
commit is the generator v2.1.0 `release.yml` pins; `scripts/check-versions.ps1`
fails a change that moves one without the other.

### 5) Check the portable bundle's own checksums

The zip carries a `SHA256SUMS` of its contents. It is not signed, but the zip
that carries it is:

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

- **No Authenticode signature yet, by design until a certificate exists.**
  Signing is optional: `release.yml` signs `dish-setup.exe` whenever the
  `WIN_SIGN_CERT_BASE64` and `WIN_SIGN_PASSWORD` secrets exist and otherwise
  releases it unsigned with a warning (the repository variable
  `ENFORCE_RELEASE_SIGNING=true` turns that into a failure). Neither secret is
  configured: 2.8.0's run warned "Stable tag 2.8.0 is being released with an
  UNSIGNED installer. Missing: WIN_SIGN_CERT_BASE64 WIN_SIGN_PASSWORD", and its
  installer has an empty certificate table. Microsoft Defender SmartScreen
  therefore shows "Windows protected your PC" the first time a freshly
  downloaded installer from a new release runs (*More info*, then *Run
  anyway*). `dish.exe` and `crashpad_handler.exe` inside it are not signed
  either. Automatic updates are not interposed by SmartScreen, because a file an
  app writes itself carries no mark of the web, so the updater's checksum chain
  is what stands between a release and an installed machine.
- **The Syft SBOMs list almost nothing.** Syft scans the release directory,
  where the libraries sit inside a zip and an installer it does not open:
  2.8.0's SPDX SBOM has two packages. From the release after 2.8.0,
  `dish-windows-components.spdx.json` is the complete bill of materials,
  written from the build and checked against `assets/licenses/licenses.json`
  on every pull request; [`THIRD_PARTY.md`](THIRD_PARTY.md) lists the same set.
- **The in-zip `SHA256SUMS` is in-band.** It lives inside the file it
  describes; it is worth something only after the zip itself has been verified.

Nothing in this document should be read as claiming more than it states.
