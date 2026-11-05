/** 2019-12-01 12:00:00.000 UTC */
export function utc(ms: number): string {
  const date = new Date(ms);
  // Panning can go past the last date there is.
  if (Number.isNaN(date.getTime())) {
    return 'beyond any date';
  }
  return `${date.toISOString().replace('T', ' ').replace('Z', '')} UTC`;
}

/** 2019-12-01 12:00:00.000123 UTC: to the microsecond, as trade times are recorded. */
export function utcMicroseconds(us: number): string {
  const ms = Math.floor(us / 1000);
  const extra = String(us - ms * 1000).padStart(3, '0');
  return utc(ms).replace(' UTC', `${extra} UTC`);
}
