import fs from 'node:fs';
import path from 'node:path';
import {randomUUID} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';

import {Match} from './tools/match.mjs';
import {fields, writeFrames} from './tools/frames.mjs';

const root = path.dirname(fileURLToPath(import.meta.url));
const teams = ['blue', 'red'];

function run() {
  const {values: {opponent, seed: rawSeed, map, side: requestedSide}} = parseArgs({options: {
    map: {type:'string', default:'1'}, side: {type:'string'}, opponent: {type: 'string'}, seed: {type: 'string', default: '11'}
  }});
  if (opponent !== 'strong' && opponent !== 'weak')
    throw Error('--opponent 需要指定 strong 或 weak');
  if (!['1','2','3','4'].includes(map)) throw Error('--map 需要指定 1 至 4');
  if (requestedSide !== undefined && !teams.includes(requestedSide))
    throw Error('--side 需要指定 blue 或 red');
  const seed = Number(rawSeed);
  if (!/^\d+$/.test(rawSeed) || !Number.isSafeInteger(seed) || seed > 2147483647)
    throw Error('--seed 需要 0 到 2147483647 之间的整数');

  const release = JSON.parse(fs.readFileSync(path.join(root, 'release.json'), 'utf8'));
  const bot = release.opponents[opponent];
  const playerSide = requestedSide ?? (bot.side === 'red' ? 'blue' : 'red');
  const side = teams.indexOf(playerSide);
  const botSide = teams[1-side];
  const programs = {[playerSide]: path.join(root, 'player.wasm'), [botSide]: path.join(root, bot.file)};
  const match = new Match({...programs, seed, map: path.join(root,'maps','公开'+map+'.json')});
  const meta = {
    formatVersion: 2, scenario:match.manifest.scenario, teamOrder: teams, opponent, playerSide: teams[side], seed,
    programs: Object.fromEntries(teams.map((team, i) => [team, match.manifest.programs[i]])),
    rulesSHA256: match.manifest.rules, scenarioSHA256: match.manifest.scenarioDigest,
    engineSHA256: match.manifest.engine
  };
  if (meta.programs[botSide] !== bot.sha256) throw Error('陪练文件摘要与发布配置不符');

  const runs = path.join(root, 'runs');
  fs.mkdirSync(runs, {recursive: true});
  const name = opponent + '-' + seed + '-' + randomUUID();
  const dest = path.join(runs, name);
  const staging = fs.mkdtempSync(path.join(runs, '.partial-'));
  try {
    const fd = fs.openSync(path.join(staging, 'replay.jsonl'), 'wx');
    try {
      const write = row => fs.writeFileSync(fd, JSON.stringify(row) + '\n');
      write({type: 'meta', ...meta, chart:match.manifest.chart, fields});
      writeFrames(match, side, write);
    } finally {
      fs.closeSync(fd);
    }
    const r = match.result;
    const summary = {
      ...meta, outcome: r.winner === -1 ? 'draw' : r.winner === side ? 'win' : 'loss',
      winner: teams[r.winner] ?? null, reason: r.reason, endTick: r.tick,
      score: [r.blue, r.red], frames: r.frames, metrics: r.metrics, traceSHA256: r.traceSHA256
    };
    fs.writeFileSync(path.join(staging, 'result.json'), JSON.stringify(summary, null, 2) + '\n');
    fs.renameSync(staging, dest);
    return {...summary, resultFile: path.join(dest, 'result.json'),
            replayFile: path.join(dest, 'replay.jsonl')};
  } finally {
    fs.rmSync(staging, {recursive: true, force: true});
  }
}

try {
  console.log(JSON.stringify(run()));
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
