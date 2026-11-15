import { useSyncExternalStore } from 'react';
import { hasTelemetryConsent, isTelemetryAvailable, setTelemetryConsent, subscribeTelemetryConsent } from './telemetry';

/** Asks whether to send anonymous usage statistics, and says what they are. Nothing without a key at build time. */
export function TelemetryConsent() {
  const consent = useSyncExternalStore(subscribeTelemetryConsent, hasTelemetryConsent);
  if (!isTelemetryAvailable()) {
    return null;
  }
  return (
    <details className="telemetry">
      <summary>
        <label>
          <input
            type="checkbox"
            checked={consent}
            onChange={(event) => setTelemetryConsent(event.target.checked)}
            data-testid="telemetry-consent"
          />
          Send anonymous usage statistics
        </label>
      </summary>
      <p>
        Off unless you turn it on. It helps us see which GPUs and browsers the viewer runs on, how fast it is, and
        what goes wrong.
      </p>
      <ul>
        <li>Sent: your browser, system and GPU, how fast frames are, how many trades a file has and how long it took to load, how far you zoom, and errors.</li>
        <li>Never sent: the trades themselves, prices, file names, your IP address, or anything that identifies you. No cookies.</li>
        <li>About a dozen summaries an hour, to PostHog in the EU.</li>
      </ul>
    </details>
  );
}
