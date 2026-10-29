/** 2019-12-01 12:00:00.000 UTC */
export function utc(ms: number): string {
  return `${new Date(ms).toISOString().replace('T', ' ').replace('Z', '')} UTC`;
}
