import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';

const native = createRequire(import.meta.url)('../build/naval.node');
import {deflateRawSync, inflateRawSync} from 'node:zlib';
import {randomUUID} from 'node:crypto';
const magic = Buffer.from('NAV4RPL1');
const u32 = n => {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(n);
  return b;
};
const groupSize = 64;
function encode(field, old) {
  if (!old || old.length !== field.length || !field.length)
    return [ u32(field.length), field ];
  const packed = native.delta(field, old);
  const tag = packed === field ? field.length : 0x80000000 + packed.length;
  return [ u32(tag), packed ];
}
function decode(raw, pos, old) {
  const tag = raw.readUInt32LE(pos);
  pos += 4;
  const n = tag & 0x7fffffff, end = pos + n;
  if (end > raw.length)
    throw Error('truncated field');
  if (!(tag >>> 31))
    return [ raw.subarray(pos, end), end ];
  if (!old)
    throw Error('missing base');
  if (!n)
    return [ old, end ];
  const field = Buffer.from(old);
  while (pos < end) {
    const offset = 32 * raw.readUInt16LE(pos);
    pos += 2;
    const length = Math.min(32, field.length - offset);
    if (length <= 0 || pos + length > end)
      throw Error('bad block');
    raw.copy(field, offset, pos, pos + length);
    pos += length;
  }
  return [ field, end ];
}
export class ReplayWriter {
  constructor(file, manifest, tmp) {
    this.file = file;
    this.tmp = tmp ?? (file + '.' + randomUUID() + '.tmp');
    this.parts = [];
    this.prev = [];
    this.count = 0;
    fs.mkdirSync(path.dirname(file), {recursive : true});
    this.fd = fs.openSync(this.tmp, 'wx');
    const meta = Buffer.from(JSON.stringify(manifest));
    fs.writeSync(this.fd, Buffer.concat([ magic, u32(meta.length), meta ]));
  }
  frame(fields) {
    for (let i = 0; i < fields.length; i++) {
      this.parts.push(...encode(fields[i], this.prev[i]));
    }
    this.prev = fields;
    if (++this.count === groupSize)
      this.flush();
  }
  flush() {
    if (!this.count)
      return;
    const compressed = deflateRawSync(Buffer.concat(this.parts), {level : 1});
    fs.writeSync(
        this.fd,
        Buffer.concat([ u32(this.count), u32(compressed.length), compressed ]));
    this.parts = [];
    this.prev = [];
    this.count = 0;
  }
  finish(result) {
    this.flush();
    const tail = Buffer.from(JSON.stringify(result));
    fs.writeSync(this.fd, Buffer.concat([ u32(0), u32(tail.length), tail ]));
    fs.closeSync(this.fd);
    fs.renameSync(this.tmp, this.file);
  }
  abort() {
    try {
      fs.closeSync(this.fd);
    } catch {
    }
    fs.rmSync(this.tmp, {force : true});
  }
}
export class ReplayReader {
  constructor(file) {
    this.fd = fs.openSync(file, 'r');
    this.cache = [];
    this.groups = [];
    this.length = 0;
    try {
      const head = this.read(0, 12);
      if (!head.subarray(0, 8).equals(magic))
        throw Error('unknown replay');
      const n = head.readUInt32LE(8);
      this.manifest = JSON.parse(this.read(12, n));
      let pos = 12 + n;
      while (true) {
        const h = this.read(pos, 8);
        let count = h.readUInt32LE(), size = h.readUInt32LE(4);
        pos += 8;
        if (!count) {
          this.result = JSON.parse(this.read(pos, size));
          break;
        }
        this.groups.push({start : this.length, count, pos, size});
        this.length += count;
        pos += size;
      }
    } catch (e) {
      this.close();
      throw e;
    }
  }
  read(pos, n) {
    const b = Buffer.allocUnsafe(n);
    if (fs.readSync(this.fd, b, 0, n, pos) !== n)
      throw Error('incomplete replay');
    return b;
  }
  get(i) {
    if (!Number.isInteger(i) || i < 0 || i >= this.length)
      throw RangeError('frame');
    const g = this.groups[Math.floor(i / groupSize)];
    if (g !== this.cached) {
      this.cache = [];
      let prev = [], pos = 0;
      const raw = inflateRawSync(this.read(g.pos, g.size));
      for (let j = 0; j < g.count; j++) {
        const fields = [];
        for (let k = 0; k < this.manifest.fields.length; k++) {
          const [field, next] = decode(raw, pos, prev[k]);
          fields.push(field);
          pos = next;
        }
        this.cache.push(fields);
        prev = fields;
      }
      if (pos !== raw.length)
        throw Error('bad frame length');
      this.cached = g;
    }
    return this.cache[i - g.start];
  }
  close() { fs.closeSync(this.fd); }
}
