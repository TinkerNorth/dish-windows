# Dish and Satellite contract: client notes (Windows)

The protocol contract (REST surface, UDP streams, crypto, liveness, identity)
lives in one place: `docs/contract.md` in
[TinkerNorth/satellite](https://github.com/TinkerNorth/satellite/blob/main/docs/contract.md).
Dish for Windows offers protocol 4 and still speaks down to 1
(`kProtocolVersion` and `kProtocolVersionMin` in
[`src/core/model/Protocol.h`](../src/core/model/Protocol.h)). This file only
records where the contract lands in this client. When the code and the contract
disagree, the contract wins; `scripts/check-versions.ps1` fails a change that
moves the protocol number here, in `CONTRIBUTING.md` or in the code without the
others.

## Where the contract lands

The pure protocol layer lives under `src/core/` (no Qt networking, no sockets,
host-testable). Sockets, HTTPS and stores live under `src/Network/` and
`src/source/`.

| Contract concept | dish-windows home |
|---|---|
| Opcodes, caps, frame sizes, the protocol range | `src/core/model/Protocol.h` |
| HKDF session key, the HMAC proof, packet AEAD (direction nonce, AAD = token) | `src/core/wire/SessionCrypto.*`; the cross-repo pinned vectors in `tests/test_session_crypto.cpp` |
| Versioning: the offer, the 409 range, the settled version that keys the frame shapes | `src/core/reducer/ProtocolNegotiation.h`; `tests/test_protocol_negotiation.cpp` |
| UDP data plane: framing, per-direction counters, replay guard, heartbeat, receive loop, every stream codec (INPUT with the extended-buttons word on a session settled at 4, motion, battery, touchpad and pointer up; rumble, lightbar, trigger effects, player LEDs, audio down) | `src/Network/SatelliteClient.*`; `tests/test_satellite_client_*.cpp` |
| Controller audio: MIC_AUDIO up, SPEAKER_AUDIO and HAPTIC_AUDIO down, the reorder window, the Opus codec | `src/core/audio/` (`AudioJitter.h`, `AudioEnginePolicy.h`), `src/source/audio/` (`OpusAudioCodec`, `MicCaptureEngine`, `SpeakerPlayoutEngine`) |
| Authed REST: session PUT/GET/DELETE, per-slot PUT/DELETE, self-unpair, `X-Device-Id` + `X-Hmac-Proof` | `src/Network/HTTPClient.*` over `src/source/http/HttpTransport.*` |
| Pairing paths A and B and their outcome | `src/Network/WifiConnectionManager.*`, `src/Network/PairingOutcome.*`, `src/core/reducer/ReversePairing.h` |
| TOFU: the verdict ladder and the TLS enforcement | `src/core/net/Tofu.*`, `src/source/http/SatelliteTlsVerifier.*` |
| Reconcile, close-notify, reconnect backoff, REST error model | `src/core/reducer/Reconcile.h`, `CloseNotify.h`, `Backoff.h`, `RestOutcome.h` |
| Descriptor caps, `touchpadMode`, the Steam trackpad pick | `src/core/reducer/CapabilitySolver.*`, `TouchpadModeResolve.h`, `TrackpadPickResolve.h` |
| Controller-type catalog (`GET /api/catalog`) | `src/source/http/SatelliteCatalogRepository.*`, `src/core/catalog/`, `src/core/reducer/CatalogFeatureGate.h` |
| `GET /api/server/capabilities` | `src/Network/HTTPClient.*` |
| Discovery: mDNS `_satellite._udp` and the UDP `:9879` beacon | `src/source/connection/MdnsDiscovery.*`, `src/source/connection/DiscoveryGateway.*`; `tests/test_mdns_*.cpp`, `tests/test_beacon_parser.cpp` |
| Session lifecycle and liveness per connection | `src/Network/WifiConnection.*`, `src/Network/ConnectionHub.*` |

`tests/FakeSatelliteRest.h` is the in-process REST satellite the pairing and
`WifiConnectionManager` suites drive.

## Client behaviours this repo adds nothing to

The behaviours the contract requires of every client (declarative topology,
the session key never being the pairing key, terminal 401 and 409, the
1500-byte datagram ceiling) are stated in the satellite's contract and are not
restated here. A change to any of them is `[wire-coordinated]` in
`CHANGELOG.md` and lands with Satellite, Dish for Android, Dish for iOS, Dish
for macOS and Dish for Linux in the same release cycle.
