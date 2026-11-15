/**
 * Everything the page has to tell the user about what went wrong, in one
 * list, which <Notices> shows: files that can't be read or look odd, the GPU,
 * the wasm module, and errors nothing expected.
 */

export interface Notice {
  id: number;
  /** An error means something failed; a warning, that it works but may not be what's expected. */
  severity: 'error' | 'warning';
  message: string;
  /** How many times it happened. */
  count: number;
  /** Whether only reloading the page can help. */
  needsReload: boolean;
  group: string;
}

type NewNotice = Pick<Notice, 'severity' | 'message'> & { needsReload?: boolean };

// So an error repeating with different details can't fill the page.
const MaxUnexpected = 5;
const Unexpected = 'unexpected';

let notices: readonly Notice[] = [];
let nextId = 1;
const listeners = new Set<() => void>();
const lastSet = new Map<string, string>();

function publish(next: readonly Notice[]) {
  notices = next;
  for (const listener of listeners) {
    listener();
  }
}

/** For useSyncExternalStore. */
export function subscribeNotices(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function getNotices(): readonly Notice[] {
  return notices;
}

/**
 * Replaces the notices of `group`, such as "file" or "gpu"; none clears it.
 * Setting the same ones again does nothing, so one the user dismissed stays gone.
 */
export function setNotices(group: string, items: NewNotice[]) {
  const key = JSON.stringify(items);
  if (lastSet.get(group) === (key === '[]' ? undefined : key)) {
    return;
  }
  if (key === '[]') {
    lastSet.delete(group);
  } else {
    lastSet.set(group, key);
  }
  publish([
    ...notices.filter((notice) => notice.group !== group),
    ...items.map((item) => ({ needsReload: false, ...item, id: nextId++, count: 1, group })),
  ]);
}

export function dismissNotice(id: number) {
  publish(notices.filter((notice) => notice.id !== id));
}

/** Adds an error nothing expected, or counts it if it's already there. */
export function reportError(message: string) {
  const known = notices.find((notice) => notice.group === Unexpected && notice.message === message);
  if (known) {
    publish(notices.map((notice) => (notice === known ? { ...notice, count: notice.count + 1 } : notice)));
    return;
  }
  const others = notices.filter((notice) => notice.group === Unexpected).length;
  const shown = others < MaxUnexpected ? message : "There are more errors. The details are in the browser's console.";
  if (others > MaxUnexpected) {
    return;
  }
  publish([...notices, { id: nextId++, severity: 'error', message: shown, count: 1, needsReload: true, group: Unexpected }]);
}

/** Whether a wasm crash, which viewerModule.ts handles by starting a new module, threw `error`. */
function isModuleCrash(error: unknown): boolean {
  return error instanceof WebAssembly.RuntimeError;
}

/** Shows every error nothing else caught, instead of leaving them to the console. */
export function installErrorReporting() {
  window.addEventListener('error', (event) => {
    // Harmless, and only the browser's way to say a layout took two passes.
    if (isModuleCrash(event.error) || event.message.startsWith('ResizeObserver loop')) {
      return;
    }
    reportError(`Something unexpected went wrong: ${event.error instanceof Error ? event.error.message : event.message}`);
  });
  window.addEventListener('unhandledrejection', (event) => {
    if (!isModuleCrash(event.reason)) {
      reportError(`Something unexpected went wrong: ${event.reason instanceof Error ? event.reason.message : String(event.reason)}`);
    }
  });
}
