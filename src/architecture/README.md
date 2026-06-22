# `src/architecture/` — the UDF kernel

The C++17 + Qt port of dish-android's architecture kernel (`AbstractStateSource`
/ `AbstractComposer` / `AbstractController` / `Repository`). **Pure, header-only,
Qt-free** — the rest of `core/` and these primitives compile and unit-test on the
host with no Qt/SDL/sodium dependency. See
[`migration-plan/analysis/android-architecture.md`](../../../migration-plan/analysis/android-architecture.md)
for the Kotlin originals and the full mapping.

## Pick the right primitive

| You have… | Use | File |
|---|---|---|
| a value that changes over time, observed by others | `Observable<S>` | `Observable.h` |
| state owned from a socket/timer/cache/setting | `StateSource<S>` (expose `state()`, mutate via `setState`) | `StateSource.h` |
| one value **purely derived** from other Observables | `Composer<Out, Ins...>` (one pure `transform`; no IO/events) | `Composer.h` / `Combiner.h` |
| a side effect driven by a state | `Controller<S>` (implement `apply`; `start()` idempotent; choose `stop()` teardown) | `Controller.h` |
| durable keyed storage | `Repository<K,V>` / `KeyedRepository<K,V>` (dumb, synchronous, thread-safe; wrap in a Source for reactive reads) | `Repository.h` |

For imperative cross-cutting commands over several sources, write a **Coordinator**
(a plain `QObject` service that re-exposes a child's `Observable` by reference —
never mirrors it). For an IO/native boundary, write a **Gateway** (no domain
state). For a `(state,event)->result` decision or a domain→UI shape, write a pure
**Reducer**/**Mapper** free function in `core/`. These conventions are classes you
write per feature, not base classes here.

## Semantics that matter

- **`Observable`** is hot and always has a value (like `StateFlow`). `set`/`update`
  notify **only on change** (`operator==`), so `S` must be equality-comparable.
  `subscribe(cb, emitCurrent=true)` replays the latest to a new subscriber. The
  returned `Subscription` is an RAII unsubscribe handle (move-only). `subscribe` is
  `const` so a `Controller` holding a `const Observable&` can still attach.
- **`Combiner`/`Composer`** compute **once eagerly at construction** and recompute
  on any upstream change — the `SharingStarted.Eagerly` guarantee. Keep the
  `transform` a free function (testable in isolation). Non-movable (captures
  `this`); own it as a member or via `make_unique`.
- **`Controller`** applies the current value on `start()` and is **idempotent**;
  `stop()` defaults to tearing down the subscription but is overridable for the
  deliberate "survive a restart" cases.

## Threading & the hot path

Notifications fire on the thread that called `set`. When binding to widgets from a
background thread, marshal to the GUI loop (`Qt::QueuedConnection` /
`QMetaObject::invokeMethod`). **Do not route the input hot path through this
kernel** — keep input→encode→`sendto` as plain C++ (see the plan §4.3).

## Tests

The probes in [`tests/`](../../tests/) (`StateSourceProbe.h`, `ComposerProbe.h`,
`ControllerProbe.h`, `RepositoryContract.h`) capture **emission sequences**, not
just final values, and `RepositoryContract` runs the 8 standard property tests
every concrete repository must pass. `tests/test_kernel.cpp` exercises all of the
above.
