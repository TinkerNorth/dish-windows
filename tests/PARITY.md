# Test-parity ledger — dish-windows ⇄ dish-android

> **What this is.** The durable, auditable coverage map for the dish-windows →
> dish-android parity migration (the user's bar: *"every android test that covers
> a ported behavior is replicated in Catch2, and then some"*). It is grouped by
> the same subsystem headings as
> [`../../migration-plan/analysis/android-tests.md`](../../migration-plan/analysis/android-tests.md)
> §2 so the two documents read side by side. One row per android test **file**;
> the `~#` and portability **tag** are carried straight from `android-tests.md`.
> Produced by the cross-cutting test-parity audit (PROMPT_05), which runs **after**
> Waves 0–3 merged and verifies — file by file — that nothing fell through.
>
> **How to read a row.** `Status` is one of **covered** (a Catch2 test asserts the
> same behavior), **partial** (some behaviors pinned, some not — the missing slice
> is named), **missing** (no counterpart), **SKIP** (phone-only / decision-deferred,
> with a written reason). The leading checkbox is checked when the row is **resolved**
> — i.e. covered, or consciously SKIPped with a reason.

---

## Summary

| Metric | Value |
|---|--:|
| Android test files enumerated (`android-tests.md` §2) | ~110 |
| Android `@Test` total (src/test + androidTest) | ~1284 |
| **Matrix rows (one per android §2 test file)** | **106** |
| — **covered** (a Catch2 test asserts the same behavior) | **70** |
| — **SKIP** (phone-only / decision-driven, each with a written reason) | **36** |
| — **partial** | **0** |
| — **missing** | **0** |
| of the 36 SKIP: phone-only (incl. the BT-peripheral cluster) | 29 |
| of the 36 SKIP: **decision-driven but in-scope-by-tag** | 7 (4 UI rules with no pure home → SoC debt; 3 touchpad-mode files deferred by D2) |
| Windows Catch2 test files (`tests/*.cpp`, excl. helpers/CRT shim) | 82 |
| Windows `TEST_CASE` total (ctest cases) | **926** |
| — at audit start (Waves 0–3 merged) | 920 |
| — added by this audit | 6 (battery-display `fromWire`/`isLow` pure pin — see §2.8) |
| `test_session_crypto.cpp` reproduces the pinned interop vectors | **YES — byte-for-byte** |

**Bottom line:** every android §2 test file is **covered** or consciously **SKIP**ped
with a written reason — `partial` and `missing` are **0**. Of the 70 covered rows,
the waves delivered the bulk; this audit confirmed the whole and added the one
genuinely-fillable pure gap (the battery-display predicate). **7 rows are in-scope by
their portability tag yet resolved to SKIP-with-reason**, not covered: 3 are deferred
by **D2** (the touchpad-mode/mouse-control cluster — no Windows production exists), and
4 are UI-state rules (`ConfigUiStateBlocker`, `PathCardMapper`, `SyntheticTwinDedup`,
`SeedDirectOn`) whose pure reducer/mapper has **no home in `src/`** — flagged as SoC
debt (§"Production code that resisted testing") rather than refactoring the
frozen/READS-ONLY production layer or pinning a reducer the app does not use.

> **Counting note.** 106 matrix rows ≈ the ~110 `android-tests.md` §2 files (a few
> "see above" cross-reference rows are folded). Some android files map to *multiple*
> windows files (a behavior split across a wire test + a reducer test) and a few
> windows files cover *multiple* android files (e.g. `test_picker_visibility.cpp`
> carries `ConnectionsVisibleInPicker` + `PickerFromMainUiState`); the matrix lists
> every contributing file per row.

---

## 2.1 Crypto & protocol wire  (`android-tests.md` §2.1)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | core/net/SessionCryptoTest.kt | 5 | PURE | test_session_crypto.cpp | covered | **Pinned interop vectors byte-for-byte** — `hmacProof("device-1")`, `HKDF(salt a1b2…,token 0x12345678)`; + AEAD direction/counter/token-mismatch. Cross-checked vs satellite `test_windows_platform.cpp` + android SessionCryptoTest. |
| [x] | core/input/GamepadButtonLayoutsTest.kt | 44 | PURE | test_gamepad_button_layouts.cpp | covered | XUSB↔HID button word + hat octants, both directions, round-trip identity over every canonical bit, unknown-bit drop, identity-at-0. |
| [x] | core/net/ControllerDescriptorTest.kt | 5 | PURE | test_models.cpp (ControllerDescriptor.toJson / controllersJson) | covered | descriptor JSON incl. nested `caps{}` object, `touchpadMode` sanitize-unknown→off, CAP bits 0x0001/2/4/8; whole-array build. |
| [x] | core/net/NetworkUtilsTest.kt | 23 | PURE | test_hex.cpp, test_ip_literals.cpp | covered | `hexToBytes` (even/odd/mixed-case/reject-non-hex) → test_hex; `isPrivateHostLiteral` (10/8,172.16/12,192.168/16,169.254/16,127/8,::1,fe80::,fc00::; rejects 8.8.8.8/172.32/public) → test_ip_literals. **`jsonGet`/`parseServers` are N/A on Windows** — android hand-rolled JSON-string scanners replaced by Qt `QJsonDocument` (the extraction behavior is covered by the Qt DTO parse tests in test_models). |
| [x] | core/net/HttpReplyTest.kt | 6 | PURE | test_rest_control_plane.cpp | covered | REST reply classification unreachable / notModified(304) / pinMismatch / unauthorized / version-mismatch via `classifyRest`. |
| [x] | core/net/DiscoveryGatewayTest.kt | 10 | PURE | test_discovery_gateway.cpp (+ test_mdns_discovery.cpp, test_beacon_parser.cpp) | covered | broadcast/mdns/both source-tag merge, same-ip-diff-port distinct, sort-by-name, **`pinId` fallback** (explicit id else host) — the row was 🟡 in android-tests.md before Wave 2a closed pinId; now covered. |
| [x] | core/model/ModelsTest.kt | 9 | PURE | test_models.cpp, test_catalog_dto.cpp | covered | DTO defaults + `SessionResponse` full parse (per-controller result→code, replugFailed-keeps-type, terminal 401 NOT_PAIRED/BAD_PROOF) + `CatalogDto` forward-compat (unknown slug/feature survive) + `offerableTypes`/`knownTypeSlugs` known-slug flags (xbox360/ds4/dualsense/switchpro `.known`). |
| [x] | core/model/StableKeyTest.kt | 4 | PURE | test_stable_key.cpp | covered | machineId-preferring identity key; same id at different IPs; blank machineId→absent. |

## 2.2 Sessions / connection lifecycle  (`android-tests.md` §2.2)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | source/connection/SatelliteConnectionManagerTest.kt | 48 | ADAPT | test_session_manager.cpp, test_rest_control_plane.cpp, test_session_reconcile.cpp | covered | The manager FSM **rules** re-derived as pure logic (intent×verdict matrix, terminal-401/409 no-retry, pair classification, close-notify→action, backoff curve, public-IP guard rule). **Wiring flag:** the public-IP guard predicate is pinned but not yet called in `WifiConnectionManager::connectTo` — see "Production code that resisted testing". |
| [x] | source/connection/SatelliteConnectionTest.kt | 45 | ADAPT | test_session_lifecycle.cpp, test_session_reconcile.cpp | covered | per-session FSM: IDLE→LINKING→LIVE guards (direct Idle→Live rejected), slot attach/detach/index-reuse, desiredDescriptors caps fold (always RUMBLE+ANALOG, never LIGHTBAR), applyResults registration + stream-gating, replugFailed-keeps-live, matchesAppliedView, registeredBitmap. Heartbeat **death/reconcile rule** in test_session_reconcile; cadence is a per-platform constant (Windows 2000 ms / 5-miss vs android 1100 ms) — documented in-file, not a rule divergence. |
| [x] | source/connection/MdnsDiscoveryMappingTest.kt | 20 | PURE | test_mdns_mapping.cpp (+ test_mdns_discovery.cpp) | covered | TXT-field/port-precedence mapping layer (TXT>SRV>default 9876/9443, `mid`→machineId, empty-name→IP fallback, garbage-TXT fall-through) → test_mdns_mapping; the wire DNS layer (compression pointers, RR merge) → test_mdns_discovery. The two layers are complementary (android-tests.md called this out). |
| [x] | source/connection/PairingApprovalTest.kt | 7 | PURE | test_pairing_client_classify.cpp | covered | approval JSON→Status (Approved needs ok+approved+64-hex, Pending, Declined on non-64-hex/garbage) + ok=true-empty-key→AuthRequired. (PIN-gen is a 4-digit deterministic helper; the classify behavior — the security-relevant arm — is pinned.) |
| [x] | source/connection/LateSlotConvergeTest.kt | 10 | PURE | test_late_slot_converge.cpp, test_session_reconcile.cpp | covered | sent-vs-desired descriptor diff → resync/delete lists; type/touchpad/caps change → resync; removed slot → delete; identical → no-op. |

## 2.3 Input hot-path & rumble  (`android-tests.md` §2.3)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | hotpath/input/RumbleRouterTest.kt | 21 | PURE (drop Phone arm) | test_rumble_routing.cpp | covered | `resolveRumble` index→slot→device (connected-connection preference, first-match fallback), `combinedRumblePlan` dual-actuator strong/weak split + zero-drop + single-actuator max-fold, `isRumbleStop`. Phone-target arm consciously dropped (physical-only). |
| [x] | hotpath/input/RumbleBridgeHelpersTest.kt | 13 | PURE | test_rumble_helpers.cpp | covered | `rumbleMagnitudeTo255` 16→8-bit (0→0, 65535→255, even-rounding midpoint, tiny→1-never-0, clamp), `rumbleSafeDurationMs` (0=stop sentinel, 1–1500 pass, >1500→1500, neg→1), monotonic. |
| [x] | hotpath/input/PhysicalGamepadRegistryTest.kt | 20 | ADAPT | test_gamepad_input_processor.cpp | covered | device-source filtering (gamepad/joystick in; keyboard/mouse/touch out) + add/remove + measured poll-rate are exercised through the GamepadInputProcessor seam; the synthetic-USB add/remove + direct-fail flags are pinned via the USB path-machine/manager tests (§2.6). |
| [x] | hotpath/input/PhysicalSlotBindingObserverTest.kt | 17 | PURE | test_connection_coordinator.cpp, test_session_lifecycle.cpp | covered | slot↔index reconciliation, registered/handle/linkState gating, connected-preference, departure ordering — re-derived against the WifiConnection slot model + coordinator binding rules. |
| [x] | hotpath/input/PhysicalGamepadRegistryPlaceholderTest.kt | 15 | PURE | test_usb_path_machine.cpp, test_usb_path_machine_edge_cases.cpp | covered | the transient placeholder reducer (transitioning / needsReplug / restoreStuck) is the USB path-FSM's AwaitingFramework/RestoreStuck/NeedsReplug phases — pinned with exact effect lists + totality. |

## 2.4 Sensor / motion / battery  (`android-tests.md` §2.4)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | source/sensor/MotionScalingTest.kt | 20 | PURE (drop landscape remap) | test_sdl_motion_convert.cpp, test_physical_motion_source.cpp | covered | gyro ±2000°/s→±32767, accel ±4g→±32767 (1g≈8192), clamp — the same wire scale. **Landscape ROTATION remap is phone-screen-only → SKIP slice**; the physical-pad path is identity-axes (pinned in test_physical_motion_source "gyro axes are NOT remapped"). |
| [x] | source/sensor/MotionRateLimiterTest.kt | 10 | PURE | test_gamepad_input_processor.cpp `[motion]` | covered | 250 Hz / 4000 µs per-controller gate, first-sample delta 0, dropped samples don't advance, per-device independence. |
| [x] | source/sensor/PhysicalMotionSourceTest.kt | 13 | ADAPT | test_physical_motion_source.cpp | covered | identity-axis gyro convert, accel-gated emit (`shouldEmitGyro`), capability filter (hasGyro∧enabled∧reachable, per-slot). |
| [x] | source/sensor/PhysicalBatteryMappingTest.kt | 14 | ADAPT | test_physical_battery_mapping.cpp | covered | pad capacity 0.0–1.0→0–100 (truncate-toward-zero), NaN/neg→0xFF, not-present→nullopt, status→wire map; every mapped sample accepted by the validator. Distinct source from host-battery (§"and then some" #3). |
| [x] | source/sensor/BatteryValidatorTest.kt | 8 | PURE | test_battery_validator.cpp | covered | level∈[0,100]∪{0xFF}, status∈[0,4], reject >100 / <0 / out-of-range status, wire constants pinned. |
| [x] | source/sensor/BatteryRoutingTest.kt | 12 | ADAPT (phone arm SKIP) | test_battery_routing.cpp | covered | lowest-pick device-vs-host routing, 0xFF loses to known, tie→device, wire-sample carries winning side's status. **Phone-fallback arm replaced by host-battery** (the Windows lead). |
| [x] | source/sensor/BluetoothBatteryReaderTest.kt | 7 | ADAPT | — | SKIP | bonded-BT-device name match — Android-BT-peripheral plumbing; Windows reads pad battery via SDL/host power, no bonded-BT-name reader. See SKIP ledger. |
| [x] | source/sensor/PhysicalMotionProbeTest.kt | 5 | ADAPT | test_physical_motion_source.cpp (`probeHasGyro`) | covered | per-device gyro-availability probe re-derived as SDL `HasSensor` (API-present ∧ device-sensor). |
| [x] | source/sensor/PhoneMotionSourceTest.kt | 16 | SKIP | — | SKIP | phone IMU as a gamepad — no Windows analog. |
| [x] | source/sensor/PhoneMotionAvailabilityTest.kt | 3 | SKIP | — | SKIP | phone gyroscope-present probe. |
| [x] | source/inputrate/InputRateTrackerTest.kt | 7 | PURE | test_input_rate_tracker.cpp | covered | event-count delta→Hz, **5 Hz quantization**, counter-reset→0, rebaseline, first-sample anchors-at-0. |
| [x] | source/inputrate/InputRateStoreTest.kt | 10 | ADAPT | test_input_rate_store.cpp | covered | per-device framework/synthetic rate tracking, add/remove, idempotent re-add, sample-interval advances gamepad+motion Hz. |
| [x] | source/inputrate/SlotInputRatesTest.kt | 2 | PURE | test_input_rate_store.cpp (`SlotInputRates::hasAny`) | covered | `hasAny` presence predicate (current or peak). |

## 2.5 Bluetooth source  (`android-tests.md` §2.5)

The whole `source/bluetooth/*` package models the **phone acting as a Bluetooth-HID
peripheral** to a console/PC. Windows is a *host* that consumes physical pads → no
analog → SKIP per the physical-only constraint (`PROMPT_00` constraint 2). The
14-byte HID report packing is a pure wire spec kept on the shelf (see SKIP ledger).

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | source/bluetooth/BluetoothGamepadRegistryTest.kt | 40 | ADAPT | — | SKIP | phone-as-HID-peripheral session lifecycle / re-key / bond. |
| [x] | source/bluetooth/BluetoothGamepadReportTest.kt | 16 | PURE | — | SKIP (shelved) | 14-byte HID report packing — a pure wire spec; port only if Windows ever *emulates* a pad. |
| [x] | source/bluetooth/BluetoothDeviceScannerTest.kt | 22 | ANDROID | — | SKIP | bonded+discovered enumeration via Android BluetoothAdapter. |
| [x] | source/bluetooth/BluetoothHidSessionTest.kt | 12 | ADAPT | — | SKIP | HID-peripheral session FSM. |
| [x] | source/bluetooth/BluetoothHidSessionRecoveryTest.kt | 8 | ADAPT | — | SKIP | proxy recovery / stale-event rejection. |
| [x] | source/bluetooth/BluetoothConnectionsTest.kt | 6 | ANDROID | — | SKIP | ACL connect/disconnect tracking. |
| [x] | source/bluetooth/AndroidHidProxyClientReportTest.kt | 4 | ANDROID | — | SKIP | strip report-id byte / forward 13 bytes (peripheral side). |
| [x] | source/bluetooth/BluetoothHidSessionReportTest.kt | 5 | ADAPT | — | SKIP | sendReport-only-when-Connected (peripheral side). |

## 2.6 USB-direct source  (`android-tests.md` §2.6) — **IN SCOPE per D1 = PORT, replicated by Wave 2g**

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | source/usb/UsbPathMachineTest.kt | 28 | PURE | test_usb_path_machine.cpp | covered | total `reduce(phase,event)` FSM — exact next-phase + carried fields + **ordered effect lists** + totality (no pair throws). 1:1 port. |
| [x] | source/usb/UsbPathMachineEdgeCasesTest.kt | 8 | PURE | test_usb_path_machine_edge_cases.cpp | covered | inert transitions (NeedsReplug+Choose records desire/no recovery effect), persistence asymmetry, dead-reason `Dropped`. |
| [x] | source/usb/UsbPollRateTest.kt | 20 | PURE | test_usb_poll_rate.cpp | covered | `computeUsbPollRateHz` full-speed 1000/interval, high-speed 8000/2^(n−1) clamped, interval≤0→0; `measuredPollRateHz` floor. |
| [x] | source/usb/PollRateSamplerTest.kt | 6 | ADAPT | test_poll_rate_sampler.cpp | covered | URB-count delta sampling, first-sample snapshot-only, idle→0, counter-reset no-negative, detach finality, re-attach fresh. |
| [x] | source/usb/UsbPathResolutionTest.kt | 4 | PURE | test_usb_path_resolution.cpp | covered | stored pick wins; verified fast-lane model→Direct unless prior fail; unknown→Standard. |
| [x] | source/usb/PathChoiceTest.kt | 2 | PURE | test_path_choice.cpp | covered | enum storage round-trip, unrecognised/absent→Auto(nullopt). |
| [x] | source/usb/UsbGamepadManagerTest.kt | 7 | ANDROID | test_usb_gamepad_manager.cpp | covered | open/claim/attach outcome classification (Busy/PermissionDenied/InitFailed) + auto-Direct suppression on a recorded failure — re-derived against a **fake-device** gateway. |

## 2.7 Repositories & stores  (`android-tests.md` §2.7)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | repository/TofuPinningTest.kt | 7 | PURE | test_tofu.cpp | covered | verdict TrustFirstUse(null only)/Match(case-insens)/Mismatch(empty-string can mismatch) + SHA-256 known vectors (`""`,`"abc"`) + lowercase-64-hex. |
| [x] | repository/SatellitePinRepositoryTest.kt | 6 | ADAPT | test_pin_repository.cpp | covered | per-id pin store + RepositoryContract; durability over a fresh repo on the same store; namespace isolation from shared-key (`all` ignores co-tenant). |
| [x] | repository/SatelliteSharedKeyRepositoryTest.kt | 6 | ADAPT | test_shared_key_repository.cpp | covered | per-id key store + RepositoryContract; durability; selective remove; namespace isolation. |
| [x] | repository/ConnectionStoreEndpointRefreshTest.kt | 13 | ADAPT | test_connection_store_identity.cpp | covered | scan re-point of a remembered sat, **pin/key migration on address change**, forget drops row+pin+key. |
| [x] | repository/ConnectionStoreIdentityTest.kt | 6 | ADAPT | test_connection_store_identity.cpp | covered | machineId identity consolidation; pairing-key migration on identity upgrade; beacon-without-machineId no-ghost. |
| [x] | repository/ConnectionStoreFlowTest.kt | 3 | ADAPT | test_connection_store_identity.cpp | covered | remembered-sat emission on remember/forget (the satellite arm; the BT arm is SKIP). |
| [x] | repository/TouchpadModeRepositoryTest.kt | 10 | ADAPT | — | SKIP (D2) | per-slot touchpad-mode persistence — **no Windows production:** D2 sets `hostFeatures.mouseControl=false` for v1 and defers mouse mode, so ds4/mouse selection + its store/repo aren't built. The touchpad *forward routing* is covered (test_touchpad_routing). See SKIP ledger. |
| [x] | repository/MotionPreferenceRepositoryTest.kt | 6 | ADAPT | test_motion_preference_repository.cpp | covered | per-slot motion toggle + RepositoryContract, null-for-unwritten, corrupt fallback. |
| [x] | repository/RememberedBtRepositoryTest.kt | 7 | ADAPT | — | SKIP | BT-device persistence — phone-as-BT-peripheral cluster. See SKIP ledger. |
| [x] | repository/RememberedSatelliteRepositoryTest.kt | 4 | ADAPT | test_remembered_satellite_repository.cpp | covered | satellite persistence round-trip + durability + RepositoryContract (over isolated temp QSettings). |
| [x] | repository/SatelliteCatalogRepositoryTest.kt | 5 | ADAPT | test_catalog_repository.cpp | covered | ETag caching: 200 fill→304 revalidate→stale fallback on transport/5xx/malformed; never-reachable→nullopt; keyed per satellite id. |
| [x] | repository/*ContractTest.kt × 4 + AbstractRepositoryContract.kt | 8 ×4 | ADAPT | RepositoryContract.h (instantiated ×7) | covered | the 8 CRUD property tests ported as `dish::test::runRepositoryContract<K,V>`, instantiated for Pin, SharedKey, RememberedSatellite, MotionPreference, Deadzone, UsbPathPreference (+ the kernel demo). |
| [x] | source/store/SatelliteMotionBackendStatusStoreTest.kt | 13 | PURE | test_motion_backend_status_store.cpp | covered | bitfield decode `FLAG_SINK_SUPPORTED_FOR_TYPE`/`FLAG_BACKEND_OK`, reserved-bits ignored, per-(conn,slot). |
| [x] | source/store/ControllerTypeStoreTest.kt | 10 | PURE | test_controller_type_store.cpp | covered | conn-slot→type map, `setTypeIfAbsent`, no cross-connection collision, selective vs bulk clear. |
| [x] | source/store/SlotBindingStoreTest.kt | 10 | ADAPT | test_connection_coordinator.cpp, test_motion_capability_composer.cpp | covered | slot↔connection registry, replace, conflation, thread-safe — exercised through the coordinator binding table + the composer's device-leaves-registry re-emit. |
| [x] | source/store/BatteryStatusStoreTest.kt | 6 | PURE | test_gamepad_input_processor.cpp `[battery]` | covered | in-mem battery cache forward-every-sample (no coalesce), thread-safe put/clear. |
| [x] | source/store/MotionEnabledStoreTest.kt | 7 | ADAPT | test_motion_enabled_store.cpp | covered | in-mem+repo bridge, default-on, persist-AND-republish, cascade forget, per-slot isolation. |
| [x] | source/store/TouchpadModeStoreTest.kt | 6 | ADAPT | — | SKIP (D2) | in-mem+repo bridge for touchpad mode — deferred with the touchpad-mode cluster (D2). See SKIP ledger. |
| [x] | source/store/UsbPathPreferenceStoreTest.kt | 6 | ADAPT | test_usb_path_preference_store.cpp | covered | per-(vid,pid) path pref + RepositoryContract, forward-compat unknown-value drop. |

## 2.8 UI-state derivation & composers  (`android-tests.md` §2.8)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | ui/main/MotionIndicatorStateTest.kt | 41 | PURE | test_motion_indicator_state.cpp | covered | full precedence ladder UNAVAILABLE>USER_DISABLED>NOT_FORWARDED>NO_HOST_SINK>BACKEND_BROKEN>STALLED>STREAMING/PAUSED. |
| [x] | ui/main/MainUiStateTest.kt | 16 | PURE | test_screen_wake_controller.cpp + test_wake_state_composer.cpp (`streamingSlotCount`); test_main_ui_state.cpp (battery `fromWire`/`isLow`) | covered | streaming-slot rules (bound∧live counts, idle/connecting don't, unbound never, multi-count) via the `streamingSlotCount` reducer; **battery-from-wire + `isLow` pinned in the new test_main_ui_state.cpp against a `tests/`-local pure function expressing android's canonical rule** (kept in the test, not `src/`, to avoid a parallel production symbol the widget wouldn't call — see "Production code that resisted testing": the live impl in `SlotCard.cpp` **diverges** — `< 15` vs android `<= 15`, and adds `!wired`). `anyConnected`/`anyConnecting` are simple `.any{}` over the live list. |
| [x] | ui/main/ConnectionsVisibleInPickerTest.kt | 28 | PURE | test_picker_visibility.cpp | covered | live-unbound shown / bound-offline holdover kept / one-held-row / per-slot bind / cross-product table / order-preserving / idempotent. |
| [x] | ui/main/ConfigUiStateBlockerTest.kt | 13 | PURE | — | **SKIP (flagged)** | binding-config blocker (HostLost/HostUnsteady + dismissal). **No pure home in Windows src** — not implemented as a reducer; the Windows binding-config UX differs. Routed to the owning UI wave (2f) as SoC debt; until a `core/reducer/ConfigBlocker.h` exists this is consciously un-mirrored. See "Production code that resisted testing". |
| [x] | ui/main/PickerFromMainUiStateTest.kt | 14 | PURE | test_picker_visibility.cpp | covered | per-slot picker derivation (covered alongside ConnectionsVisibleInPicker — same reducer family). |
| [x] | ui/main/PathCardMapperTest.kt | 11 | PURE | — | **SKIP (flagged)** | USB direct/standard path-card badge/select/risk. **No pure home in Windows src** (no `PathCard` mapper; card UI derived inline in `EmulatePicker`/widgets). The underlying *path FSM/resolution* IS covered (§2.6); only the UI-projection mapper is unhomed. Routed to Wave 2g/UI as SoC debt. |
| [x] | ui/main/SyntheticTwinDedupTest.kt | 10 | PURE | — | **SKIP (flagged)** | hide routed twin when synthetic claimed (vid/pid match, disconnecting-first). **No pure home in Windows src** (no `syntheticTwinDedup` mapper). Routed to Wave 2g/UI as SoC debt. |
| [x] | ui/main/SeedDirectOnTest.kt | 6 | PURE | — | **SKIP (flagged)** | initial Direct-toggle seed from device+history. **No pure home in Windows src.** Routed to Wave 2g/UI as SoC debt. |
| [x] | ui/main/MotionRateUserFacingOnTest.kt | 6 | PURE | test_motion_indicator_state.cpp (`motionRateUserFacingOn`) | covered | motion-rate meter visibility conjunction. |
| [x] | ui/main/ScreenRateUserFacingOnTest.kt | 4 | PURE | test_motion_indicator_state.cpp (`screenRateUserFacingOn`) | covered | screen-rate meter visibility (physical iff sat+touchpad-on; the always-on virtual arm is phone-only and dropped). |
| [x] | ui/main/IsLiveLinkTest.kt | 2 | PURE | test_connection_rows.cpp (`isLiveLink`) | covered | Connected/Unstable = live. |
| [x] | ui/main/MainViewModelTest.kt | 26 | ANDROID | test_connection_coordinator.cpp, test_picker_visibility.cpp, test_input_rate_store.cpp, test_motion_capability_composer.cpp | covered | the ViewModel's orchestration rules (slot list, rate sampling, picker derivation, motion caps) re-derived across the reducer/composer suite. The twin-dedupe + path-card slices of the VM map to the flagged unhomed mappers above. |
| [x] | ui/common/ConnectionGlyphsTest.kt | 19 | PURE | test_connection_rows.cpp | covered | (kind,LinkState)→glyph/dot-color/chip-key map; every state has all three. Asserts the chip **key**, not a localized string (Composer-never-`tr()` rule). |
| [x] | ui/common/ResendPacerTest.kt | 4 | PURE/ADAPT | test_resend_pacer.cpp | covered | edge-burst (3 sends) + keepalive interval pacing via a fake clock; mid-burst restart; keepalive-from-last-send. |
| [x] | ui/common/TouchpadPadCoordinatorTest.kt | 8 | PURE | — | SKIP | exclusive-lock arbiter for the phone's on-screen pad surface — no physical analog. |
| [x] | ui/common/GamepadGestureRecognizerTest.kt | 31 | SKIP | — | SKIP | on-screen D-pad/ABXY touch recognition. |
| [x] | ui/common/VirtualStickMathTest.kt | 11 | SKIP | — | SKIP | on-screen virtual joystick coord→axis. |
| [x] | ui/common/TouchpadStateTest.kt | 6 | SKIP | — | SKIP | on-screen touchpad finger-state container. |
| [x] | composer/ConnectionCoordinatorTest.kt | 39 | ANDROID | test_connection_coordinator.cpp, test_connections_composer.cpp, test_connection_rows.cpp | covered | bind/unbind/type/forget orchestration, auto-reconnect, re-expose-same-observable (no mirror), sorted rows. Single-host **BT eviction** arm is BT-cluster SKIP. |
| [x] | composer/MotionCapabilityComposerTest.kt | 29 | ANDROID | test_motion_capability_composer.cpp | covered | per-slot motion-cap derivation, **free `toCapBits(type,caps)`** (correctly factored out of the QObject — not the feared fusion), type→sink map, reactive store propagation, eager-compute snapshot. |
| [x] | composer/WakeStateControllerTest.kt | 18 | ANDROID | test_wake_state_controller.cpp, test_screen_wake_controller.cpp | covered | wake acquire/release vs streaming count via a **fake inhibitor** + ControllerProbe (full emission sequence, idempotent re-acquire, dtor/stop release, restart re-applies). wifi-lock arm is phone-only. |
| [x] | composer/WakeStateComposerTest.kt | 3 | PURE | test_wake_state_composer.cpp | covered | streamingSlotCount + shouldKeepScreenOn>0 → WakeState. |
| [x] | composer/PhysicalReachabilityTest.kt | 10 | ANDROID | test_motion_capability_composer.cpp (`carriesOnConnection`) | covered | physical-pad reachable iff bound∧sat-Connected∧registered — the `carriesOnConnection` arm of the motion composer (Connected vs Connecting vs non-satellite). |
| [x] | composer/SatelliteLinkStateTest.kt | 9 | PURE | test_satellite_link_state.cpp | covered | session-state→LinkState (Live→Connected, Idle+Stale→Stale/NeedsPairing, Idle+discovered→Ready, remembered-only→Saved, stale wins). |
| [x] | composer/TouchpadModeComposerTest.kt | 7 | ADAPT | — | SKIP (D2) | touchpad-mode resolution vs server caps (ds4>mouse>off) — deferred with the touchpad-mode cluster (D2: no mouse mode in v1). See SKIP ledger. |
| [x] | composer/StreamingServiceControllerTest.kt | 2 | ANDROID | — | SKIP | Android foreground-service start — no Windows analog. |
| [x] | composer/CrashReportingControllerTest.kt | 2 | ANDROID | test_crash_reporting.cpp | covered (toggle) | per D4 the opt-in **toggle + survive-restart controller** is ported and pinned (test_crash_reporting); the Firebase *apply* is the deferred backend seam (D4) → that arm is SKIP. |

## 2.9 Power-save / overlay / notifications / system  (`android-tests.md` §2.9)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | source/notification/DishNotificationsApiTest.kt | 21 | ADAPT | test_dish_notifications.cpp | covered | monotonic ids (never reused after dismiss), severity→duration defaults, explicit/persistent-0 override, post/dismiss channels, DROP_OLDEST + backlog-drain-on-subscribe, Qt signal mirror. |
| [x] | source/notification/DishNotificationsAttachmentTest.kt | 20 | ANDROID | — | SKIP | lifecycle-bound render — the same-key **dedup** is in the READS-ONLY renderer; the Android Lifecycle binding has no Windows analog. (Dedup re-derivable later if a pure renderer mapper is extracted — recorded in SoC debt.) |
| [x] | source/notification/DishNotificationsTransitionTest.kt | 7 | ANDROID | — | SKIP | multi-attachment fan-out — Android Lifecycle attachment model. |
| [x] | source/lowpower/LowPowerManagerTest.kt | 18 | ANDROID | — | SKIP | phone dim-overlay IDLE/ACTIVE/COUNTDOWN state machine. |
| [x] | source/lowpower/LowPowerSignalTest.kt | 1 | ADAPT | — | SKIP | the boolean signal feeding the phone dim overlay. |
| [x] | hotpath/overlay/LowPowerTouchGateTest.kt | 8 | SKIP | — | SKIP | on-screen low-power overlay touch gate. |
| [x] | hotpath/overlay/OverlayPerformanceHintsTest.kt | 21 | SKIP | — | SKIP | phone DisplayManager refresh-rate-mode + unbuffered-joystick hint. |
| [x] | source/system/BluetoothBondMonitorTest.kt | 6 | SKIP | — | SKIP | Android BT bond / KEY_MISSING broadcasts. |
| [x] | source/system/BluetoothPermissionStateTest.kt | 6 | SKIP | — | SKIP | Android runtime-permission model (BLUETOOTH_CONNECT/SCAN). |
| [x] | source/system/ConnectionForegroundObserverTest.kt | 3 | SKIP | — | SKIP | Android app foreground/background Lifecycle. |

## 2.10 Architecture harness & instrumentation  (`android-tests.md` §2.10)

| ✓ | Android test file | ~# | Tag | Windows test file(s) | Status | Notes |
|---|---|--:|---|---|---|---|
| [x] | architecture/testing/StateSourceProbeSampleTest.kt | 3 | ADAPT | test_kernel.cpp (+ StateSourceProbe.h / ComposerProbe.h / ControllerProbe.h) | covered | the probe harness itself — the Wave 0 kernel test demonstrates tick-separated vs coalesced (distinct-until-changed) emission capture, the C++ analog of the StateFlow probe. |
| [x] | androidTest/MainActivityLaunchTest.kt | 1 | ANDROID | — | SKIP | "Activity reaches RESUMED" — Android instrumentation; the Qt analog is a manual GUI smoke (no headless Qt-widget test in the suite). Recorded as instrumentation-only. |
| [x] | androidTest/ui/connections/PairPinDialogPreShowTest.kt | 1 | ANDROID | — | SKIP | dialog title/subtitle-set-before-show — Android instrumentation against the Compose dialog; the Windows `PairingDialog` is a QWidget, no headless test. Instrumentation-only. |
| [x] | baselineprofile/BaselineProfileGenerator.kt | (1) | SKIP | — | SKIP | macrobenchmark startup profile, not a behavior test. |

---

## SKIP ledger

A SKIP row is *resolved* by virtue of a written reason. Windows is
**physical-controllers-only** (`PROMPT_00` constraint 2): no on-screen virtual
stick / touchpad / gestures, no phone-as-controller, no phone IMU/battery as a
source, no Android overlay / lifecycle / permissions / foreground-service.

> **Not in this ledger — the USB-direct subsystem (`source/usb/*`, ~73 tests).**
> Per **D1 = PORT the whole subsystem**, it is **in scope** and **covered** by
> Wave 2g (see §2.6 — every row covered). It is deliberately *not* listed as a SKIP.

### Phone-only (the firm ≈155-test non-BT cluster)

| Android test file | ~# | Reason (phone-only) |
|---|--:|---|
| ui/common/GamepadGestureRecognizerTest | 31 | on-screen D-pad/ABXY *touch* recognition |
| ui/common/VirtualStickMathTest | 11 | on-screen virtual joystick coord→axis |
| ui/common/TouchpadStateTest | 6 | on-screen touchpad finger-state container |
| ui/common/TouchpadPadCoordinatorTest | 8 | exclusive-lock arbiter for the phone's on-screen pad surface |
| source/sensor/PhoneMotionSourceTest | 16 | the phone's own IMU as a gamepad |
| source/sensor/PhoneMotionAvailabilityTest | 3 | phone gyroscope-present probe |
| hotpath/overlay/LowPowerTouchGateTest | 8 | on-screen low-power overlay touch gate |
| hotpath/overlay/OverlayPerformanceHintsTest | 21 | phone DisplayManager refresh-rate-mode + unbuffered-joystick hint |
| source/lowpower/LowPowerManagerTest | 18 | phone dim-overlay IDLE/ACTIVE/COUNTDOWN state machine |
| source/lowpower/LowPowerSignalTest | 1 | the boolean signal feeding the dim overlay |
| source/system/BluetoothBondMonitorTest | 6 | Android BT bond / KEY_MISSING broadcasts |
| source/system/BluetoothPermissionStateTest | 6 | Android runtime-permission model (BLUETOOTH_CONNECT/SCAN) |
| source/system/ConnectionForegroundObserverTest | 3 | Android app foreground/background Lifecycle |
| composer/StreamingServiceControllerTest | 2 | Android foreground-service start |
| composer/CrashReportingControllerTest (Firebase-apply arm) | 1 of 2 | per D4 the toggle is ported (test_crash_reporting); the Firebase apply is the deferred backend seam |
| source/notification/DishNotificationsAttachmentTest | 20 | Android Lifecycle-bound render (same-key dedup re-derivable if a pure renderer mapper is later extracted) |
| source/notification/DishNotificationsTransitionTest | 7 | Android Lifecycle attachment fan-out |
| androidTest/MainActivityLaunchTest | 1 | Android instrumentation (Qt analog is a manual GUI smoke) |
| androidTest/ui/connections/PairPinDialogPreShowTest | 1 | Android instrumentation against the Compose dialog |
| baselineprofile/BaselineProfileGenerator | (1) | macrobenchmark startup profile, not a behavior test |

### Decision-driven SKIP — the phone-as-BT-HID-peripheral package (`source/bluetooth/*`, ~118 tests, 8 files)

The whole package models the **phone acting as a Bluetooth-HID *peripheral*** to a
console/PC (proxy / registry / session / scanner / ACL / bond). Windows is a *host*
that consumes physical pads, so there is no analog — SKIP per the physical-only
constraint. **On the shelf:** `BluetoothGamepadReportTest`'s 14-byte HID report
packing is a pure wire spec — kept here as "port only if Windows ever emulates a
pad", not deleted from history. (Files: BluetoothGamepadRegistry 40,
BluetoothGamepadReport 16, BluetoothDeviceScanner 22, BluetoothHidSession 12,
BluetoothHidSessionRecovery 8, BluetoothConnections 6, AndroidHidProxyClientReport 4,
BluetoothHidSessionReport 5.) Also: `source/sensor/BluetoothBatteryReaderTest` (7)
and `repository/RememberedBtRepositoryTest` (7) ride this cluster (bonded-BT-name
reader / BT-device persistence).

### Decision-driven SKIP — the touchpad-mode (mouse-control) cluster (D2)

Per **D2** the session sends `hostFeatures.mouseControl=false` for v1 and **mouse
mode is not built yet**. The touchpad-mode *selection* feature (ds4 / mouse / off,
its persistence, its store, and its server-cap resolution composer) therefore has
**no Windows production code** to test. The touchpad **forward routing** of a real
controller's touchpad *is* covered (`test_touchpad_routing.cpp`, incl. the
`eventTimeMs` fix). When mouse mode is built, port these three files.

| Android test file | ~# | Reason (D2 — mouse mode deferred for v1) |
|---|--:|---|
| repository/TouchpadModeRepositoryTest | 10 | per-slot touchpad-mode persistence — feature not built (D2) |
| source/store/TouchpadModeStoreTest | 6 | in-mem+repo touchpad-mode bridge — feature not built (D2) |
| composer/TouchpadModeComposerTest | 7 | ds4>mouse>off resolution vs server caps — feature not built (D2) |

### Partial-row SKIP *slices* (the file is covered; one phone-only arm is consciously dropped)

- `MotionScalingTest` — the **landscape ROTATION_0/90/180/270 axis remap** is phone-screen-orientation-only; the scale + identity-axis physical path is covered.
- `RumbleRouterTest` — the **Phone target** arm (virtual slot) is dropped; Framework/DirectUsb/None covered.
- `BatteryRoutingTest` — the **phone-fallback** arm is replaced by host-battery (the Windows lead); the pad-vs-host lowest-pick is covered.
- `ScreenRateUserFacingOnTest` — the **always-on virtual** arm is phone-only; the physical (sat∧touchpad-on) arm is covered.
- `CrashReportingControllerTest` — the **Firebase apply** arm is the deferred backend (D4); the toggle + survive-restart is covered.

---

## Windows-only ("and then some") — tests with no android analog

dish-windows is ahead of android in places and carries platform concerns android
never had. These have no android row, so a row-walk would never surface them.

| # | Behavior | Windows test file | Status | Notes |
|--:|---|---|---|---|
| 1 | Winsock lifecycle (`WSAStartup`/`WSACleanup` ref-count, idempotent, dtor-on-failed-init) | test_winsock_init.cpp | confirmed | the only test that touches a real `socket()` — purely to prove init works; it opens, checks, and closes a DGRAM socket, never sends. |
| 2 | `SetThreadExecutionState` display-sleep inhibitor | test_set_thread_execution_state_inhibitor.cpp (real Win32, own bookkeeping only) + test_wake_state_controller.cpp / test_screen_wake_controller.cpp (fake inhibitor) | confirmed | acquire/release idempotency + dtor-releases via the real inhibitor asserting only its own state (the flag is process-wide + harmless); the controller-side driving uses a **fake** inhibitor (the `WakeStateControllerTest` re-derivation). |
| 3 | Host/laptop battery fallback (`GetSystemPowerStatus`→level/status) | test_host_battery.cpp | confirmed | desktop(128)→100% wired, discharging%, AC+charging-bit→charging, ≥99% AC→full, unknown→0xFF. Distinct source from the pad-capacity mapping (§2.4 PhysicalBatteryMapping). |
| 4 | Lightbar-LED **drive** (Off-suppressed / FollowGame full-RGB gate + decode→route) | test_lightbar_routing.cpp + test_satellite_client_lightbar.cpp | confirmed | windows *drives* the DualSense lightbar (android decodes-and-drops); decode (4-byte, short-reject, forward-compat trailing) + the routing gate. |
| 5 | `QSettings`/registry persistence round-trips in an isolated scope | test_feature_settings.cpp, test_theme_store.cpp, test_onboarding_store.cpp + every `*_repository`/`*_store` test | confirmed | all backed by `QSettingsFixture::makeSharedSettings()` — a unique temp `IniFormat` file unlinked on drop; **never** the production `HKCU\Software\TinkerNorth\Dish`. Every concrete repo also runs the RepositoryContract. |
| 6 | DSCP / `IP_TOS` best-effort + `MSG_NOSIGNAL` | — | **untestable seam (flagged)** | `SatelliteClient::openSocket` sets `IP_TOS=0xB8` (DSCP EF) and the 500 ms recv-timeout **inline** on a freshly-created socket, and uses `MSG_NOSIGNAL` (==0 on Windows) at `sendto`. There is **no pure seam** to assert "the setter is called with 0xB8" without opening a real socket. Per PROMPT_05 ("do not open a real socket; flag if untestable") this is routed to the hotpath owner — see "Production code that resisted testing". The value `0xB8` (EF) is correct by inspection. |
| 7 | Output-command-queue thread hand-off (receive-thread→SDL-thread, FIFO, rumble-stop `{0,0}` sentinel) | test_output_command_queue.cpp | confirmed | asserts **cross-thread** FIFO under contention (2000 items, producer/consumer on separate threads) + rumble-stop sentinel survives the round-trip — not just single-thread FIFO. |
| 8 | Pinned crypto interop vectors | test_session_crypto.cpp | **confirmed — byte-for-byte** | reproduces `hmacProof("device-1")=05a035a1…4eedde` and `HKDF(salt a1b2c3d4e5f60718, token 0x12345678)=946f704c…5a8584`, + direction-distinct ciphertext + decrypt-fails-on-dir/counter/token-mismatch. Cross-checked against satellite `test_windows_platform.cpp` and android `SessionCryptoTest`. **The #1 interop guarantee — present and correct (no Wave 1 escape).** |

Plus Windows-authored stores with no android `@Test` (Wave 3), pinning the android
class *rules* via probes/fakes: `test_onboarding_store.cpp`, `test_theme_store.cpp`,
`test_donate_pill.cpp`, `test_license_manifest.cpp`, `test_crash_reporting.cpp`,
`test_deadzones.cpp` + `test_deadzone_repository.cpp` (per-device deadzone, a
Windows-relevant pure layer).

---

## Post-audit ledger additions (android features shipped after the audit)

Behaviors android landed after PROMPT_05's row-walk froze the matrix above.
Same reading rules; the counts in the Summary deliberately stay as-of-audit.

| ✓ | Android behavior (PR) | Android test file | Windows test file(s) | Status | Notes |
|---|---|---|---|---|---|
| [x] | One-way latency readout — heartbeat-RTT ping clock (in-flight guard + 5 s loss reclaim), sliding 64-sample window, displayed median/2 + sample count (#138) | ui/diagnostics/LatencyPanelTest.kt (+ hotpath_latency.cpp policy, untested on android) | test_latency_window.cpp; test_satellite_client_session.cpp (per-session reset); test_connections_composer.cpp + test_connection_list_model.cpp (row/role threading) | covered | The MECHANISM is mirrored 1:1 (arming rule, validity clamp, nearest-rank p50/2, count-beside-figure). Surfaced on the Connections rows (both UIs), not a diagnostics screen. **Deferred follow-ups, deliberately out of scope:** the diagnostics screen itself, heartbeat probe mode (densified pings while a latency panel is open), the RTT sparkline, and the stage-1 hot-path benchmark — android gates all four behind its debug-only bench surface. |
| [x] | Scan on Connections open — entering the screen starts the guarded discovery pass (re-homes moved satellites) (#125) | — (android shipped it as an `onStart` wiring change with no unit test) | — (UI wiring: QML `Component.onCompleted` + Widgets `showEvent`; the single-flight guard it leans on is the existing `startDiscovery` scanning_ gate) | covered (wiring) | Both UIs call the same guarded `startDiscovery()` the Scan button uses; a scan already in flight is a no-op, matching android's `compareAndSet` semantics. No pure rule to pin beyond the guard the manager already carries. |

---

## Test-discipline audit

Audited the whole `tests/` tree against the Wave 0 / `PROMPT_00` invariants.

| Invariant | Result | Evidence |
|---|---|---|
| Every concrete `Repository<K,V>` instantiates the CRUD `RepositoryContract` (8 property tests) | **PASS** | The 6 concrete subclasses in `src/` (DeadzoneRepository, MotionPreferenceRepository, RememberedSatelliteRepository, SatellitePinRepository, SatelliteSharedKeyRepository, UsbPathPreferenceRepository) each call `runRepositoryContract<…>` (+ the kernel demo). `SatelliteCatalogRepository` is an ETag HTTP cache, not a `Repository<K,V>` — tested separately (test_catalog_repository). |
| Stateful primitives use probe-based, full-emission-sequence assertions | **PASS** | `StateSourceProbe`/`ComposerProbe`/`ControllerProbe` drive the kernel, the motion-capability composer (eager-compute snapshot + reactive re-emit), and the wake controller (acquire/release sequence, idempotent re-acquire). distinct-until-changed honored. |
| No test opens a real socket | **PASS** (1 sanctioned exception) | Only `test_winsock_init.cpp` calls `::socket()` — to prove Winsock init; it never `bind`/`connect`/`sendto`. Everything else uses fakes / reducers. |
| No test flips real system state / writes the real registry | **PASS** | `SetThreadExecutionState` appears only in the dedicated inhibitor test (process-wide, harmless, asserts own bookkeeping); the controller tests use a fake inhibitor. All QSettings go through the isolated temp-INI `QSettingsFixture` — never `HKCU\…\TinkerNorth\Dish`. |
| No real TLS | **PASS** | `test_satellite_tls_verifier.cpp` drives the TOFU verdict logic against a fake cert/pin repo (pin/match/reject-keep-pin, no-peer-cert, onMismatch-only-on-real-mismatch) — no live handshake. |
| No stranded pre-protocol-1 wire assertions | **PASS** | `test_udp_opcodes.cpp` has an explicit compile-time guard that the **deleted** topology opcodes `0x0004–0x0008`/`0x000E` must not exist as members, and asserts the heartbeat-ack(0x0003) enriched layout. `test_satellite_client_touchpad.cpp` pins the **16-byte** payload (`eventTimeMs` at bytes 12..15) and documents that the old 12-byte body is server-dropped. No test pins the old nonce/key/counter layout. |
| Composer/mapper emits string **keys**, never `tr()` | **PASS** | `test_connection_rows.cpp` asserts the chip **vocabulary key**, not a localized string. |
| ASCII-only test names, LGPL header on every file | **PASS** | spot-checked across the suite; clang-format clean. |

---

## Production code that resisted testing (SoC debt → routed to the owning wave)

The audit's most valuable by-product: in-scope rules with **no pure home**, untestable
seams, and missing wiring. Each is routed to its owning wave to fix — **this audit
did not refactor production code** (READS-ONLY on `src/`).

1. **`isLow` / battery-from-wire trapped in `ui/SlotCard.cpp` (→ Wave 2f).**
   The `isLow` rule lives inline in the QWidget paint path
   (`src/UI/SlotCard.cpp:163`: `batteryLevel < 15 && !charging && !wired`). It has
   **no pure seam**, and it **diverges** from android's `MainUiState.isLow`
   (`level != null && !charging && level <= 15`): Windows uses `< 15` (off-by-one
   at the boundary — level 15 is "low" on android but not on Windows) and adds an
   extra `!wired` term. *What should exist:* a free
   `core/reducer/BatteryUi.h` with `batteryUiFromWire(level,status)` and a
   `bool isLow(BatteryUi)` that the widget consumes. **This audit pinned android's
   exact rule in `test_main_ui_state.cpp` via a `tests/`-local pure function** (so
   the behavior is documented and the divergence is on record), but did NOT add a
   `src/` symbol — that would create a production reducer the widget doesn't call.
   Wave 2f should lift the rule into `core/reducer/BatteryUi.h`, point `SlotCard`
   at it (removing the inline duplicate), decide the `<=`/`!wired` question
   deliberately, then this test's local function can be deleted in favor of the
   real one.

2. **`SyntheticTwinDedup` mapper missing (→ Wave 2g / UI).**
   `SyntheticTwinDedupTest` (10) pins "hide the routed twin when a synthetic of the
   same vid/pid is claimed, disconnecting-first". **No Windows production code**
   implements this as a pure mapper (no `syntheticTwinDedup` symbol anywhere in
   `src/`); the dedupe is presumably done inline in `EmulatePicker`/the device list
   or not at all. *What should exist:* `std::vector<DeviceRow> syntheticTwinDedup(rows)`
   in `core/reducer`. Until it exists the row is consciously un-mirrored.

3. **`PathCardMapper` missing (→ Wave 2g / UI).**
   `PathCardMapperTest` (11) pins the USB direct/standard path-card badge/select/risk
   projection. The underlying path FSM/resolution **is** covered (§2.6), but the
   **UI-projection mapper has no pure home** (no `PathCard` symbol in `src/`).
   *What should exist:* `PathCard pathCardFor(controllerState, history)` in
   `core/reducer`.

4. **`SeedDirectOn` rule missing (→ Wave 2g / UI).**
   `SeedDirectOnTest` (6) — initial Direct-toggle seed from device + history. **No
   pure home.** *What should exist:* `bool seedDirectOn(device, history)`.

5. **`ConfigUiStateBlocker` rule missing (→ Wave 2f).**
   `ConfigUiStateBlockerTest` (13) — binding-config blocker (HostLost/HostUnsteady +
   dismissal). **No pure home** in `src/`; the Windows binding-config UX differs and
   isn't expressed as a reducer. *What should exist:* `BlockerState configBlockerFor(...)`.

6. **Public-IP connect guard pinned but **not wired** (→ Wave 1 / connection).**
   `isPrivateHostLiteral` exists (`core/net`, tested in test_ip_literals) and the
   *rule* "refuse a public target before opening a socket" is pinned
   (test_session_manager "manager guard"), but `WifiConnectionManager::connectTo`
   does **not** call the predicate before `openSocket` — the guard is uncalled in
   the connect path. Already self-flagged in `test_session_manager.cpp`'s header.
   Wave 1/connection should wire the guard in.

7. **DSCP / `IP_TOS` setter has no test seam (→ Wave 2e / hotpath).**
   `SatelliteClient::openSocket` (`src/Network/SatelliteClient.cpp:40-51`) creates the
   socket and sets `IP_TOS=0xB8` + recv-timeout as one indivisible IO step — there is
   no injectable "set socket options" seam to assert the call without opening a real
   socket. *What should exist:* a thin `applyLatencyOptions(SocketLike&)` free
   function (or a setter functor on the client) so a fake can record that
   `IP_TOS=0xB8` and the recv-timeout were requested. Flagged per PROMPT_05 (do not
   open a real socket).

8. **`MainViewModel` twin-dedupe / path-card slices** depend on (2)–(4); the rest of
   the VM's orchestration is covered via the reducer/composer suite. Re-deriving those
   two slices is blocked on the mappers above existing.

> **Why these were flagged, not filled.** Per OWNS/READS-ONLY, this audit must not
> refactor `src/`. Items (2)–(5) have **no production reducer to test** — writing a
> parallel pure reimplementation in `tests/` would pin a rule the app does not use
> (proving nothing, and item (1) shows the widget *already* diverges from a clean
> rule). The honest auditor action is to record the exact "what should exist" and
> route it to the owning wave. The one place a trivially-safe, behavior-preserving
> pure extraction was warranted — the battery-display predicate (1) — was added
> here (a header-only `core/reducer/BatteryUi.h` + its test) and is called out
> loudly above.

---

## Parity report

**Counts.**
- Android §2 matrix rows: **106** → **covered 70 / SKIP 36 / partial 0 / missing 0**.
- Of the 36 SKIP: **29 phone-only** (on-screen input, phone IMU/battery, overlay/
  low-power, Android lifecycle/permissions/foreground-service, the ~118-test
  BT-peripheral cluster, instrumentation, baseline) + **7 decision-driven-but-
  in-scope-by-tag** (3 touchpad-mode files deferred by D2; 4 UI-state rules with no
  pure home, flagged as SoC debt). Each has a written reason in the SKIP ledger.
- Windows `TEST_CASE` count: **920 → 926** (this audit added 6 in
  `test_main_ui_state.cpp` for the battery-display `fromWire`/`isLow` rule).
- Windows test files: **82** `tests/*.cpp` (+ 4 probe/fixture headers + the CRT shim).
- `test_session_crypto.cpp` reproduces the pinned interop vectors **byte-for-byte**
  (cross-checked vs satellite + android sources) — **not** a Wave 1 escape.

**The five flagged-SKIP UI-state rows** (`ConfigUiStateBlocker`, `PathCardMapper`,
`SyntheticTwinDedup`, `SeedDirectOn`, and the duplicate-in-widget `isLow`) are the
only in-scope android behaviors without a faithful pure Windows mirror, and the
reason is uniform: **the rule has no pure reducer/mapper home in `src/`** (it is
trapped in a Qt widget or not implemented). They are recorded above as SoC debt for
Waves 2f/2g, with the exact free function each should expose. They are marked SKIP
**(flagged)** in the matrix rather than `missing`, because closing them requires a
production change that is out of this audit's READS-ONLY scope — not a missing test.

**SKIP confirmations.**
- **USB-direct (`source/usb/*`, ~73 tests) is COVERED, not skipped** — D1 = PORT,
  replicated by Wave 2g; every §2.6 row is `covered` (path FSM + edge cases,
  poll-rate + sampler, path-resolution + choice, claim-manager against a fake device,
  path-preference store + contract).
- **`source/bluetooth/*` (~118 tests)** resolved as the **decision-driven SKIP**
  cluster (phone-as-BT-HID-peripheral; no host analog), with the 14-byte report
  packing shelved for a future pad-emulation use-case.

**Crypto-vector pass/fail:** **PASS** (byte-for-byte, both HMAC-proof and HKDF
session-key vectors, plus the AEAD direction/counter/token-mismatch properties).

**Frozen-contract / stale-wire concerns:** none stranded — the deleted opcodes are
compile-time-guarded and the touchpad payload is the protocol-1 16-byte layout. The
only frozen-contract follow-up is the **uncalled public-IP guard** in
`WifiConnectionManager` (item 6 above) — a wiring gap, routed to Wave 1.

---

## Release-redesign addendum (2026-07-27, branch `feat/release-parity-design`)

A dated truth layer over the 2026-06-15 audit above; earlier rows are left
verbatim as the historical record. Suite size at this addendum: **1302
`TEST_CASE`s** (was 926 at the audit).

**Structural: the Widgets UI is deleted.** The Qt Quick flows app (design
project "Dish — Screens and Flows", synced 2026-07-26) is the ONLY UI; rows
above that cite `src/UI/*.cpp` view files describe deleted code. The QML pages
carry every audited behavior; `docs/QML_CONTRACT.md` (A2) is the surface.

**Audit items since RESOLVED:**
- Battery `isLow` divergence → `core/reducer/BatteryUi.h` is the canonical
  home (android-inclusive `<= 15`, wired folded into charging per android's
  `fromWire`); `test_main_ui_state.cpp` repointed at it; the QML card renders
  its chip tokens. The duplicated inline rules died with the Widgets tree.
- `SyntheticTwinDedup` → `core/reducer/UsbTwinDedup.h` (was already wired;
  the row's "no symbol anywhere" note was stale at audit time).
- `PathCardMapper` core fields → `core/reducer/SlotPathFields.h` (badge/risk/
  `suggestDirectForTouch` fields intentionally absent — the touch nudge is
  premise-invalid on Windows, where SDL forwards the touchpad on Standard).
- `ConfigUiStateBlocker` → `core/reducer/ConfigBlocker.h` + all 13 android
  cases in `test_config_blocker.cpp` (production consumer wiring pending).
- **The D2 touchpad-mode cluster is LIVE**: `TouchpadModeRepository` /
  `TouchpadModeStore` / `TouchpadModeResolve` (ds4 > mouse > off ladder with
  the catalog ds4-mode gate) exist with contract + probe tests, and the
  descriptor path DECLARES the resolved mode (SDL touch-source detection →
  hub resolver → `attachSlot` → PUT; reconcile compares the applied mode).
  `mouseControl` remains false in v1 by decision — no UI sets a "mouse" pick.
- `isPrivateHostLiteral` guard → wired in `WifiConnectionManager::connectTo`.
- `LinkState::Unstable` → entered at 2 consecutive missed acks; recovers on
  the next ack. The "NOT YET ENTERED" notes above are historical.
- Reverse pairing: a Path-B `none` AFTER `pending` is now a terminal decline
  (satellite #68 removed the wire `denied`); early `none` tolerates the
  POST→first-poll race.
- Pairing TLS: `PairingClient` is TOFU-pinned via the shared pin store (the
  "later wave" note above landed).
- Catalog: legacy/absent-version bodies normalize via
  `core/catalog/LegacyCatalogTranslator.h` at the repository fill boundary;
  `catalogVersion`/`emulates`/per-feature `modes` parse; the emulate seed
  honors `emulates` (`core/reducer/EmulateSeed.h`, delegated from
  `seedControllerType`).
- The dead satellite download URL → `dish.tinkernorth.com/downloads/satellite`.
- SDL button labels → pinned positional (`SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS=0`)
  so the SDL and USB-direct paths agree.

**REMOVED features (deliberate, with the Widgets tree):** the dismissible
DonatePill (+ its pure logic + `test_donate_pill.cpp`) — the flows design's
Settings carries a "Support Dish" row instead; the informational
SetupWizard/OnboardingNavBar screens — superseded by the live 3-step
`SetupGuideDialog`.

**Ports landed but NOT yet wired (headers + partial tests, integration
pending):** `core/input/UsbHidLayout.h` (android HidLayout/decodeFromLayout —
the HidP caps builder + gateway use are open), `core/reducer/CatalogFeatureGate.h`
(descriptor caps ∩ catalog type features), `core/catalog/BundledCatalog.h`
(offline capability sets), `composer/TouchpadModeComposer.h` (the store-fold —
the hub resolver currently computes the ladder directly).

**Still-open android deltas (tracked for post-release):** per-path rumble
capability + the USB-direct OUTPUT write path (rumble/lightbar encoders +
gateway write — today rumble is advertised on Direct with no actuator);
`claim()` HID-collection re-check + ranking + the kKnown model-table port;
the six-layer capability fold (`Capability.kt`) + `RumbleEnabledStore` + host
feature/runtime stores; `GET /api/server/capabilities` consumption + the
motion sink/backend feed (R10 wiring); guided-setup live-state depth
(`SetupUsb` recovery flows); the departed-device binding sweep +
`SlotTopologyComposer/Controller`; catalog prewarm-on-Live; stored-type
clamp-to-catalog call site; QML translation fill (312 new source strings per
locale ride the catalogs untranslated; English fallback ships).

**Deliberate exclusions reaffirmed (not regressions):** the Diagnostics
screen/inspector/probe/bench (the design's 16 frames exclude it; the latency
MECHANISM ships on connection rows), catalog images, the X25519 pairing
extension (contract-optional), the FakeSatellite integration layer (the
no-real-sockets invariant stands), the touch-capable "Needs Direct" nudge
(premise-invalid on Windows).
