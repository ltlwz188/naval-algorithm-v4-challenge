export class Metrics {
  constructor() {
    this.firstSeen = null;
    this.firstHit = null;
    this.sources = [ [ 0, 0, 0, 0, 0 ], [ 0, 0, 0, 0, 0 ] ];
    this.warned = [ new Map(), new Map() ];
    this.warnCount = 0;
    this.warnSum = 0;
    this.unwarned = 0;
  }
  add(fields) {
    for (let side = 0; side < 2; side++) {
      const b = fields[side], t = b.readInt32LE(4);
      const start = 104 + b.readUInt32LE(72) * 496 + b.readUInt32LE(76) * 64;
      const count = b.readUInt32LE(80);
      const history = this.warned[side];
      // Torpedoes have a maximum lifetime of 90T.
      for (const [id, seen] of history)
        if (t - seen > 91)
          history.delete(id);
      for (let i = 0; i < count; i++) {
        const off = start + i * 48;
        if (b.readInt32LE(off + 8) === 1) {
          const id = b.readBigUInt64LE(off);
          if (!history.has(id))
            history.set(id, t);
        }
      }
    }
    const b = fields[5];
    for (let i = 0; i < b.length; i += 48) {
      const type = b.readInt32LE(i), t = b.readInt32LE(i + 4),
            unit = b.readInt32LE(i + 8), kind = b.readInt32LE(i + 12);
      if (type === 7)
        this.firstSeen ??= t;
      if (type === 1 || type === 2)
        this.firstHit = this.firstHit === null ? t : Math.min(t, this.firstHit);
      if (type === 9)
        this.sources[unit][kind] += b.readDoubleLE(i + 16);
      if (type === 1 && (kind === 1 || kind === 2)) {
        const first =
            this.warned[Math.floor(unit / 18)].get(b.readBigUInt64LE(i + 24));
        if (first !== undefined) {
          this.warnSum += t - first;
          this.warnCount++;
        } else
          this.unwarned++;
      }
    }
  }
  result() {
    return {
      firstSeen : this.firstSeen,
      firstHit : this.firstHit,
      scoreSources : this.sources.map(v => v.slice()),
      warnedHits : this.warnCount,
      meanWarningT : this.warnCount ? this.warnSum / this.warnCount : null,
      unwarnedTorpedoHits : this.unwarned
    };
  }
}
