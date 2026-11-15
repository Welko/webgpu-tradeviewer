/**
 * Anonymous telemetry (see design/telemetry.md): nothing is sent, and PostHog
 * isn't even loaded, until the user agrees. The viewer builds the events
 * (Viewer::Telemetry); this hands them to PostHog, adds the page's own
 * errors and how loading files went, and remembers the user's choice.
 *
 * Without a PostHog key at build time (VITE_POSTHOG_KEY), as in local builds,
 * forks and CI, there's no consent toggle and nothing can ever be sent.
 */
import type { Viewer } from './viewer/viewerModule';

const Key: string | undefined = import.meta.env.VITE_POSTHOG_KEY;
// The EU cloud, so the data stays in the EU.
const Host = 'https://eu.i.posthog.com';
const ConsentStorageKey = 'telemetry-consent';
// Repeats past this aren't sent: one bug can throw every frame.
const MaxErrors = 10;
// How much of an error message is sent: enough to recognize it, not enough to carry data.
const MaxMessageLength = 200;

type Properties = Record<string, unknown>;
type Send = (name: string, properties: Properties) => void;

/** Where events go, once loaded: PostHog, or a test's list. */
interface Connection {
  send: Send;
  resume(): void;
  pause(): void;
}

declare global {
  interface Window {
    /** Set before the page loads, by tests: events are kept here instead of sent, and the toggle shows without a key. */
    recordTelemetry?: boolean;
    telemetryEvents?: { name: string; properties: Properties }[];
  }
}

const recording = typeof window !== 'undefined' && window.recordTelemetry === true;
let consent = readConsent();
// Loaded on first consent, and kept: revoking pauses it.
let connection: Promise<Connection> | null = null;
// While sending.
let send: Send | null = null;
let common: Properties = {};
let errorsSent = 0;
const viewers = new Set<Viewer>();
const listeners = new Set<() => void>();

function readConsent(): boolean {
  try {
    return localStorage.getItem(ConsentStorageKey) === 'yes';
  } catch {
    // Storage can be off, as in private windows: then the choice lasts the page.
    return false;
  }
}

/** Whether this build can send telemetry at all. Without, the page doesn't ask. */
export function isTelemetryAvailable(): boolean {
  return Boolean(Key) || recording;
}

export function hasTelemetryConsent(): boolean {
  return consent && isTelemetryAvailable();
}

/** For useSyncExternalStore. */
export function subscribeTelemetryConsent(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

/** Records the user's choice and starts or stops sending at once. */
export function setTelemetryConsent(given: boolean) {
  consent = given;
  try {
    localStorage.setItem(ConsentStorageKey, given ? 'yes' : 'no');
  } catch {
    // As in readConsent().
  }
  for (const listener of listeners) {
    listener();
  }
  if (given) {
    void start();
  } else {
    stop();
  }
}

/** Loads PostHog, only now that the user agreed. */
async function connect(): Promise<Connection> {
  if (recording) {
    window.telemetryEvents ??= [];
    return { send: (name, properties) => window.telemetryEvents!.push({ name, properties }), resume() {}, pause() {} };
  }
  const { default: posthog } = await import('posthog-js');
  posthog.init(Key!, {
    api_host: Host,
    // Our own events only: no cookies or storage, no clicks or page views, no recordings or surveys.
    persistence: 'memory',
    autocapture: false,
    capture_pageview: false,
    capture_pageleave: false,
    disable_session_recording: true,
    disable_surveys: true,
    advanced_disable_flags: true,
    person_profiles: 'never',
  });
  return {
    send: (name, properties) => posthog.capture(name, properties),
    resume: () => posthog.opt_in_capturing(),
    pause: () => posthog.opt_out_capturing(),
  };
}

async function start() {
  if (!hasTelemetryConsent()) {
    return;
  }
  const connected = await (connection ??= connect());
  // The user may have changed their mind while it loaded, or another call started it.
  if (!consent || send) {
    return;
  }
  connected.resume();
  send = connected.send;
  errorsSent = 0;
  for (const viewer of viewers) {
    enable(viewer);
  }
}

function stop() {
  if (!send) {
    return;
  }
  send = null;
  for (const viewer of viewers) {
    viewer.setTelemetry(null, {});
  }
  void connection?.then((connected) => connected.pause());
}

/** What only the page knows, for the first event: no more than a broad picture of the machine. */
function getEnvironment(): Properties {
  const agent = navigator.userAgent;
  const browser = /Edg\//.test(agent)
    ? 'Edge'
    : /Firefox\//.test(agent)
      ? 'Firefox'
      : /Chrome\//.test(agent)
        ? 'Chrome'
        : /Safari\//.test(agent)
          ? 'Safari'
          : 'other';
  const os = /Windows/.test(agent)
    ? 'Windows'
    : /Android/.test(agent)
      ? 'Android'
      : /iPhone|iPad/.test(agent)
        ? 'iOS'
        : /Mac OS/.test(agent)
          ? 'macOS'
          : /Linux/.test(agent)
            ? 'Linux'
            : 'other';
  const width = screen.width;
  const screenClass = width < 800 ? 'small' : width < 1400 ? 'medium' : width < 2200 ? 'large' : 'huge';
  return { browser, os, screenClass, devicePixelRatio: Math.round(devicePixelRatio * 100) / 100 };
}

function enable(viewer: Viewer) {
  viewer.setTelemetry((name: string, properties: Properties) => send?.(name, properties), getEnvironment());
  common = (viewer.telemetryProperties() as Properties | null) ?? {};
}

/**
 * Sends `viewer`'s telemetry while the user agrees, from now on. Returns a
 * function to call when it's gone.
 */
export function attachViewer(viewer: Viewer): () => void {
  viewers.add(viewer);
  if (hasTelemetryConsent()) {
    if (send) {
      enable(viewer);
    } else {
      void start();
    }
  }
  return () => viewers.delete(viewer);
}

/**
 * Sends an error the page caught, of `kind`: "viewer" (logged by the C++),
 * "page" (JavaScript nothing expected), "crash" (the wasm module stopped) or
 * "render" (drawing the page failed). Works when the viewer itself can't.
 */
export function reportErrorEvent(kind: string, message: string) {
  if (!send || errorsSent >= MaxErrors) {
    return;
  }
  errorsSent += 1;
  send('error', { ...common, kind, message: message.slice(0, MaxMessageLength) });
}

/** Rounded to hundredths, as the viewer rounds its own. */
function rounded(value: number): number {
  return Math.round(value * 100) / 100;
}

/**
 * Sends that trades from `source` ("file", "binance") loaded: `bytes` of
 * text read in `loadMs`, `rows` trades over `spanMs`.
 */
export function reportDataLoaded(source: string, bytes: number, loadMs: number, rows: number, spanMs: number) {
  send?.('data_loaded', {
    ...common,
    source,
    bytes,
    loadMs: rounded(loadMs),
    parseMBps: loadMs > 0 ? rounded(bytes / 1e3 / loadMs) : 0,
    rows,
    spanHours: rounded(spanMs / 3.6e6),
  });
}

/** Sends that loading from `source` failed at `stage` ("fetch", "decompress", "parse"). Only the stage: never the file's content. */
export function reportDataLoadFailed(source: string, stage: string) {
  send?.('data_load_failed', { ...common, source, stage });
}

// What happened since the last summary goes out as the page closes.
if (typeof window !== 'undefined') {
  window.addEventListener('pagehide', () => {
    for (const viewer of viewers) {
      viewer.flushTelemetry();
    }
  });
}
