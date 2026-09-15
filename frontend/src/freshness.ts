// A late reply must not replace the state of an edited form or a different endpoint.
export class Freshness {
  private values = new Map<string, number>();
  invalidate(key: string): number {
    const next = (this.values.get(key) ?? 0) + 1;
    this.values.set(key, next);
    return next;
  }
  current(key: string, version: number): boolean { return this.values.get(key) === version; }
}
