import {createHash} from 'node:crypto';
import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';
import {performance} from 'node:perf_hooks';
import {fileURLToPath} from 'node:url';

import {loadChart,encodeChart} from './chart.mjs';
import {Metrics} from './metrics.mjs';
import {ReplayWriter} from './replay.mjs';
import {matchSeed, rulesFile} from './competition.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const native = createRequire(import.meta.url)('../build/naval.node');
const digest = data => createHash('sha256').update(data).digest('hex');
function player(binary, map) {
  const wasm =
      new WebAssembly.Instance(new WebAssembly.Module(binary), {}).exports;
  for (const name of ['input', 'output', 'step'])
    if (typeof wasm[name] !== 'function')
      throw Error('missing WASM export ' + name);
  if (!(wasm.memory instanceof WebAssembly.Memory))
    throw Error('missing WASM memory');
  const abi = wasm.naval_abi?.();
  if (![5, 6].includes(abi) || typeof wasm.chart !== 'function')
    throw Error('需要使用 v4 SDK 重新构建选手 WASM');
  const chart = encodeChart(map, abi);
  new Uint8Array(wasm.memory.buffer,wasm.chart() >>> 0,chart.length).set(chart);
  const input = wasm.input() >>> 0, output = wasm.output() >>> 0;
  return obs => {
    if (obs.length > 2 * 1024 * 1024 ||
        input + obs.length > wasm.memory.buffer.byteLength)
      throw Error('observation exceeds SDK buffer');
    new Uint8Array(wasm.memory.buffer, input, obs.length).set(obs);
    if (abi === 5) new DataView(wasm.memory.buffer).setUint32(input, 5, true);
    const count = wasm.step();
    if (!Number.isInteger(count) || count < 0 || count > 512)
      throw Error('invalid action count');
    return Buffer.from(new Uint8Array(wasm.memory.buffer, output, count * 40));
  };
}
function timing(a) {
  const sorted = a.slice().sort((x, y) => x - y);
  const avg = a.reduce((x, y) => x + y, 0) / a.length;
  return {
    mean : avg,
    p95 : sorted[Math.floor(.95 * (a.length - 1))],
    max : sorted.at(-1)
  };
}
export class Match {
  constructor({
    blue = path.join(root, 'player.wasm'),
    red = path.join(root, 'opponents/strong.wasm'),
    seed, map,
    replay,
    replayTemp,
    release
  } = {}) {
    seed = matchSeed(map, seed);
    if (!Number.isInteger(seed) || seed < 0 || seed > 2147483647)
      throw Error('invalid seed');
    this.metrics = new Metrics();
    this.start = performance.now();
    const programs =
        [ blue, red ].map(p => Buffer.isBuffer(p) ? p : fs.readFileSync(p));
    const chart = loadChart(map);
    const chartBytes = encodeChart(chart);
    this.w = native.create(seed,chartBytes);
    this.decide = programs.map(binary=>player(binary,chart));
    this.status = native.status(this.w);
    this.times = [];
    this.blocks = [];
    this.blockBytes = 0;
    this.blockObjects = 0;
    this.totals = {observe : 0, blue : 0, red : 0, judge : 0, replay : 0};
    this.inputMax = 0;
    this.hash = createHash('sha256');
    this.manifest = {
      format : 1,
      abi : 6, seed, chart,
      unitBytes : this.status.unitSize,
      scenario : chart.id,
      scenarioDigest : digest(JSON.stringify(chart)),
      rules : release?.rules ?? digest(fs.readFileSync(rulesFile)),
      engine : digest(fs.readFileSync(path.join(root, 'build/naval.node'))),
      programs : programs.map(digest),
      fields : [ 'blue', 'red', 'truth', 'blueActions', 'redActions', 'events' ]
    };
    if (replay)
      this.writer = new ReplayWriter(replay, this.manifest, replayTemp);
    this.file = replay;
  }
  observation(side) { return native.frame(this.w, side); }
  step() {
    const start = performance.now();
    const obs = [ this.observation(0), this.observation(1) ];
    const truth = native.frame(this.w, 2);
    const ready = performance.now();
    const a = this.decide[0](obs[0]);
    const mid = performance.now();
    const b = this.decide[1](obs[1]);
    const decided = performance.now();
    native.advance(this.w, a, b);
    this.status = native.status(this.w);
    const fields = [...obs, truth, a, b, native.frame(this.w, 3) ];
    const judged = performance.now();
    for (const field of fields) {
      this.hash.update(field);
    }
    this.metrics.add(fields);
    this.writer?.frame(fields);
    this.latest = fields;
    const end = performance.now();
    this.totals.observe += ready - start;
    this.totals.blue += mid - ready;
    this.totals.red += decided - mid;
    this.totals.judge += judged - decided;
    this.totals.replay += end - judged;
    const input = Math.max(obs[0].length, obs[1].length);
    this.inputMax = Math.max(this.inputMax, input);
    this.times.push(end - start);
    this.blockBytes += input;
    this.blockObjects += this.status.objects;
    if (this.times.length % 500 === 0 || this.status.winner !== -2) {
      const n = this.times.length - (this.blocks.at(-1)?.end ?? 0);
      this.blocks.push({
        end : this.times.length,
        count : n,
        ms : timing(this.times.slice(-n)).mean,
        inputBytes : this.blockBytes / n,
        objects : this.blockObjects / n
      });
      this.blockBytes = 0;
      this.blockObjects = 0;
    }
    if (this.status.winner !== -2) {
      this.result = {
        ...this.status,
        scenario: this.manifest.scenario, seed:this.manifest.seed, scenarioDigest:this.manifest.scenarioDigest,
        frames : this.times.length,
        metrics : this.metrics.result(),
        traceSHA256 : this.hash.digest('hex')
      };
      this.writer?.finish(this.result);
      this.report = {
        ...this.result,
        elapsedMs : performance.now() - this.start,
        stepMs : timing(this.times),
        phaseMs : this.totals,
        blocks : this.blocks,
        peakRSSMiB : process.resourceUsage().maxRSS / 1024,
        maxInputBytes : this.inputMax,
        replayBytes : this.file ? fs.statSync(this.file).size : 0
      };
    }
    return this.status;
  }
  abort() { this.writer?.abort(); }
}
