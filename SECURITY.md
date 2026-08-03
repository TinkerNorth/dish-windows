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

The project has not cut a `v*` tag yet, so there is no released version to
support. Once releases begin: the latest minor on `main` is supported, the
previous minor receives high and critical backports for 90 days, and patch
releases are issued on demand for the latest minor.

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

A GitHub Release for a `v*` tag carries exactly two files:

- `dish-windows.zip`: `dish.exe`, the Qt runtime DLLs and QML modules staged by
  `windeployqt`, and a `SHA256SUMS` text file listing every other file in the
  bundle.
- `dish-windows.spdx.json`: an SPDX-JSON SBOM of the bundle, generated by Syft
  via `anchore/sbom-action`.

Verify the download:

```powershell
Expand-Archive .\dish-windows.zip -DestinationPath .\dish-windows
$expected = Get-Content .\dish-windows\SHA256SUMS |
    Where-Object { $_ -match 'dish\.exe' } |
    ForEach-Object { $_.Split()[0] }
$actual = (Get-FileHash .\dish-windows\dish.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw "checksum mismatch" } else { "ok" }
```

Understand what that check is worth. `SHA256SUMS` lives inside the zip it
describes, and nothing signs either one. It catches a truncated or corrupted
download. It does not prove the zip came from this project, because anyone who
could replace the zip could replace the `SHA256SUMS` inside it. Until the gaps
below are closed, the trust anchor is the GitHub Release page itself.

## Known gaps

- **No release signing.** The exe is not Authenticode-signed, so SmartScreen
  warns on first run of a fresh download. There are no cosign keyless
  signatures for the zip or the SBOM.
- **No build provenance.** SLSA provenance is not generated. There is no
  cryptographic link from an artifact back to the workflow run and commit that
  produced it.
- **No artifact vulnerability scan at release.** Dependencies are scanned in
  source form by OSV-Scanner and the dependency review; the built bundle is not
  scanned by Grype or an equivalent.
- **One SBOM format.** SPDX-JSON only. There is no CycloneDX SBOM.
- **`SHA256SUMS` is unsigned and in-band.** See above.
- **No release has shipped.** `release.yml` has never run against a tag, so the
  pipeline described here is verified by reading it, not by having produced an
  artifact.

These are tracked in the roadmap section of
[`CONTRIBUTING.md`](CONTRIBUTING.md). Nothing in this document should be read
as claiming any of them exists today.
