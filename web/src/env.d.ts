/** What the build passes in (see .github/workflows/ci.yml). */
interface ImportMetaEnv {
  /** PostHog's project key. Without it, telemetry is off and the page never asks (see telemetry.ts). */
  readonly VITE_POSTHOG_KEY?: string;
}
