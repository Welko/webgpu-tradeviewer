# Telemetry (plan, rev. 2)

Status: implemented on the web (2026-11-15). The native build has the events but no sink yet.

Changes from rev. 1, as built:
- The event catalog is `Viewer::Telemetry` (apps/ViewerApp), which App owns and feeds, not App itself.
- No `NullSink`: no sink means off. `Client::setSink(nullptr)` revokes; a new sink starts a new session id.
- Loads report their source through `beginLoad(fileName, source)`; failures before the viewer sees the bytes (fetch, decompress) through `noteLoadFailed`.
- Errors are sent from the page (`web/src/telemetry.ts`: notices, crashes, ErrorBoundary), which works after the module aborts, with the common properties the viewer gave it.
- GPU timing is sampled: 1 frame in 30, picked at random (`FrameStats::UntimedReason::NotSampled`); CPU times every frame.
- Web code is one file, `web/src/telemetry.ts`, plus `TelemetryConsent.tsx`.
- Web tests are Playwright tests (`tests/telemetry.spec.ts`): `window.recordTelemetry` keeps events in the page instead of sending them.
- Only main's CI build gets the key (`vars.POSTHOG_KEY`).

## What we want to learn

1. **Crashes and errors:** wasm aborts, uncaught JS errors, WebGPU device loss and validation errors.
2. **Performance on different GPUs:** which adapters people run on, and how fast loading, frames and GPU passes are on each.
3. **How the visualization is used:** data sources and sizes, time ranges viewed, zoom depth, aggregation levels, picking. *Not* UI clickstreams.

## Principles

- **Easy to switch off, at runtime and at build time.**
  - At runtime, consent is the switch.
  - At build time, CMake option `TRADEVIEWER_TELEMETRY` (default ON) replaces `Core::Telemetry::Client` with an inline stub. The web build then also leaves out posthog-js (Vite drops the import when `VITE_TELEMETRY` is false).
  - CI builds the OFF configuration too, so it keeps compiling.

- **Opt-in, off by default.** Nothing is sent until the user turns it on. Turning it off stops sending immediately.
- **Anonymous.** Random per-session id, no user id, no IP or geolocation, no cookies.
- **No market data.** Events describe *shape*, not content: row counts, ranges as durations, never prices or trades.
- **Aggregate, then send.** Summaries on a timer and at page close, not an event per interaction. A hard cap per session.
- **Vendor behind an interface.** PostHog is one sink; replacing or adding one (own endpoint, Sentry) touches only the sink.
- **Same events on web and native.** Defined once in C++; platforms only transport them.

## Architecture

```
Core::Telemetry  (GPU-free, platform-free)
  Event          name + flat properties (int64 | double | string | bool), schemaVersion
  Sink           interface: send(Event), flush()
  NullSink       default; consent off; tests
  RecordingSink  tests: keeps events in memory for assertions
  Client         consent gate, session id, rate limits, per-session cap,
                 periodic summary timer, forwards to the active Sink

apps/ViewerApp
  App            owns a Telemetry::Client and calls typed helpers
                 (dataLoaded, viewSummary, perfSummary, error …)
                 that build Events — the event catalog lives here
  WebMain        WebSink: hands Events to JS through one embind callback
  NativeMain     NullSink for now; HTTP sink later (same PostHog API)

web/src/telemetry/
  posthog.ts     loads posthog-js only after consent; maps Events to capture() calls
  consent.tsx    toggle + short "what we collect" text; choice stored in localStorage
```

The typed helpers in `App` are the event catalog: one function per event, so field names can't drift between call sites. `Core::Telemetry` knows nothing about markets.

### Web flow

1. React shows the consent toggle (off). The PostHog library is **not loaded** until consent is given.
2. On consent: load posthog-js, then call `viewer.setTelemetryEnabled(true)`. The C++ `Client` switches from `NullSink` to `WebSink`.
3. C++ emits Events → embind callback → `posthog.capture(name, properties)`.
4. On page close: `Client::flush()` sends the pending summary. posthog-js sends it with `navigator.sendBeacon`, which works during unload.
5. On revoke: C++ switches back to `NullSink`, and `posthog.opt_out_capturing()` stops anything still queued in JS.

### PostHog configuration

Option names below were checked against posthog-js 1.449's types.

- `api_host`: EU cloud (`https://eu.i.posthog.com`), to keep data in the EU.
- `persistence: 'memory'`: no cookies or localStorage.
- `autocapture: false`, `capture_pageview: false`, session recording disabled. We send only our own events.
- Turn off IP and geolocation enrichment in the project settings, and set the matching per-event property.
- Person profiles off: anonymous events only.

**The project API key is public by design** (it can only write events). It's injected at build time as `VITE_POSTHOG_KEY` from a GitHub repository variable. When the key is absent (local dev, forks, CI tests), the consent toggle is hidden and nothing is ever loaded.

## Event catalog (schema v1)

Every event carries `schema: 1`, `session` (random id), `platform` (`web` | `native`), `build` (git sha) and `appVersion`.

| Event | When | Properties |
|---|---|---|
| `session_started` | consent given, or app start with consent | `browser`, `os`, `gpuVendor`, `gpuArchitecture`, `gpuName` (WebGPU or WebGL fallback, with `gpuNameSource`), `backend`, `timestampQuery` (bool), `devicePixelRatio`, `screenClass` (bucketed) |
| `data_loaded` | a dataset finished loading | `source` (`synthetic` \| `binance` \| `file`), `format` (`tardis` \| `binance`), `rows`, `bytesCompressed`, `loadMs`, `parseMBps`, `spanHours` |
| `data_load_failed` | loading failed | `source`, `format`, `stage` (`fetch` \| `decompress` \| `parse`), `reason` (short code, no free text) |
| `view_summary` | every 60 s while active, and at close | `viewChanges`, `minVisibleSpanS`, `maxVisibleSpanS`, `medianVisibleSpanS`, `fullDayUsed` (bool), `aggregatedFraction` (share of frames drawn aggregated), `picks`, `resets` |
| `perf_summary` | every 60 s while frames were drawn, and at close | `frames`, `frameMsP50/P95/P99`, `cpuEncodeMsP95`, `gpuMsP50/P95` per pass (`gpu_<pass>_p95`), `timingsDropped`, `gpuBytesPeak`, `visibleTradesP95` |
| `error` | caught error | `kind` (`wasm_abort` \| `js_error` \| `device_lost` \| `webgpu_validation`), `code`, `message` (truncated, sanitized), `stack` (truncated), plus the last `perf_summary` numbers |

Durations are bucketed or rounded where exact values add nothing, so fewer distinct values are stored.

## Crash capture (web)

- Emscripten's `onAbort` (wasm `abort()`, including a `throw`, which aborts in our build).
- `window.onerror` and `unhandledrejection` for JS errors.
- WebGPU: the device-lost callback and the uncaptured-error callback in `Context`, forwarded as `error` events. Rate-limited, because a validation error can repeat every frame.
- The crash path must not depend on the wasm module still working. JS sends `error` directly when the module has aborted.

Native crash dumps (e.g. Sentry's native library) are out of scope for now.

## Limits

- At most 1 `view_summary` and 1 `perf_summary` per minute, and only if something happened.
- At most 10 `error` events per session (repeats are counted, not sent).
- A hard cap of 200 events per session. When reached, `Client` stops and keeps a counter, sent with the final summary.

That keeps a long session to a few dozen events, well inside PostHog's free tier.

## Testing

- `Core::Telemetry::Client` unit tests with `RecordingSink`, run natively and in wasm:
  - consent off → no events;
  - rate limits and session cap hold;
  - revoking consent mid-session stops sending;
  - every event has the common properties and `schema`.
- `App` event helpers: build each event from fixed inputs and compare against expected properties. This catches accidental renames.
- Web: a fake `posthog` object in a TypeScript unit test checks the mapping and that nothing loads before consent.
- CI never has a key, so CI can never send real events.

## Decisions

- No benchmark telemetry. Users never run benchmarks; the only user interaction is the consent toggle.
- Performance data for consenting users is collected passively. GPU timing samples about one frame in 30, so its cost stays negligible.
- EU hosting (PostHog EU cloud).
- The manual "Report a problem" link (pre-filled GitHub issue with diagnostics) comes later, not in the first version.
