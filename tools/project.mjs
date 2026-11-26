// Decoder for the little-endian SDK ABI. The binary replay retains exact
// doubles.
export function unit(b, i, team) {
  const n = j => b.readInt32LE(i + j), d = j => b.readDoubleLE(i + j);
  return {
    id : n(0),
    kind : n(4),
    mode : n(8),
    mother : n(12),
    x : d(16),
    y : d(24),
    heading : d(32),
    speed : d(40),
    hp : d(48),
    team,
    oxygen : n(56),
    transitionUntil : n(60),
    readyAt : n(64),
    nextLaunch : n(68),
    memberHP : Array.from({length : 8}, (_, k) => d(72 + 8 * k)),
    ammo : Array.from({length : 8}, (_, k) => n(136 + 4 * k)),
    repairAt : Array.from({length : 8}, (_, k) => n(168 + 4 * k)),
    mainAt : Array.from({length : 12}, (_, k) => n(200 + 4 * k)),
    secondaryAt : Array.from({length : 20}, (_, k) => n(248 + 4 * k)),
    torpedoAt : Array.from({length : 8}, (_, k) => n(328 + 4 * k)),
    aaAt : Array.from({length : 4}, (_, k) => n(360 + 4 * k)),
    burstAt : n(376),
    skillReady:Array.from({length:11},(_,k)=>n(380+4*k)),
    activeUntil:Array.from({length:11},(_,k)=>n(424+4*k)),
    torpedoMask:n(468), slowMask:n(472), lockedUntil:n(476),
    shieldUntil:n(480),shield:d(488)
  };
}
export const token = (b, i) =>
    'c-' + b.readBigUInt64LE(i).toString(16).padStart(16, '0');
export function obj(b, i) {
  return {
    id : token(b, i),
    kind : b.readInt32LE(i + 8),
    observedAt : b.readInt32LE(i + 12),
    x : b.readDoubleLE(i + 16),
    y : b.readDoubleLE(i + 24),
    heading : b.readDoubleLE(i + 32),
    speed : b.readDoubleLE(i + 40)
  };
}
function header(obs, unitBytes,chart) {
  if (unitBytes !== 496)
    throw Error('unsupported unit ABI');
  const h = i => obs.readInt32LE(i);
  if (h(0) !== 5 && h(0) !== 6)
    throw Error('unsupported observation ABI');
  const frame = {
    tick : h(4),
    score : [ h(16), h(20) ],
    chart, smokes: [],
    points : [],
    units : [],
    objects : [],
    flashes : [],
    losses : [],
    feedback : [],
    events : []
  };
  for (let i = 24; i < 72; i += 16)
    frame.points.push({
      owner : h(i),
      attacker : h(i + 4),
      progress : h(i + 8),
      contested : h(i + 12)
    });
  let offset=104+h(72)*unitBytes+h(76)*64+h(80)*48+h(84)*24+h(88)*8+h(92)*16;
  for(let i=0;i<h(96);i++,offset+=24)
    frame.smokes.push({x:obs.readDoubleLE(offset),y:obs.readDoubleLE(offset+8),
      from:h(offset+16),until:h(offset+20)});
  return frame;
}
export function project(fields, mode, unitBytes,chart) {
  if (mode !== 'truth')
    return observation(fields[mode === 'red' ? 1 : 0], unitBytes,chart);
  const frame = header(fields[0], unitBytes,chart);
  const b = fields[2];
  let pos = 0;
  for (let j = 0; j < 36; j++, pos += unitBytes + 8) {
    const u = unit(b, pos, Math.floor(j / 18));
    if (u.hp > 0)
      frame.units.push(u);
  }
  for (; pos < b.length; pos += 48)
    frame.objects.push(obj(b, pos));
  const ev = fields[5];
  for (let i = 0; i < ev.length; i += 48)
    frame.events.push({
      kind : ev.readInt32LE(i),
      tick : ev.readInt32LE(i + 4),
      unit : ev.readInt32LE(i + 8),
      detail : ev.readInt32LE(i + 12),
      value : ev.readDoubleLE(i + 16)
    });

  return frame;
}
export function observation(obs, unitBytes,chart) {
  const frame = header(obs, unitBytes,chart);
  const h = i => obs.readInt32LE(i);
  const side = h(8);
  let pos = 104;
  for (let j = 0; j < h(72); j++, pos += unitBytes)
    frame.units.push(unit(obs, pos, side));
  for (let j = 0; j < h(76); j++, pos += 64) {
    const e = obj(obs, pos);
    frame.units.push({
      ...e,
      team : 1 - side,
      hp : obs.readDoubleLE(pos + 48),
      count : h(pos + 56),
      contact : true
    });
  }
  for (let j = 0; j < h(80); j++, pos += 48)
    frame.objects.push(obj(obs, pos));
  for (let j = 0; j < h(84); j++, pos += 24)
    frame.flashes.push({
      x : obs.readDoubleLE(pos),
      y : obs.readDoubleLE(pos + 8),
      observedAt : h(pos + 16)
    });
  for (let j = 0; j < h(88); j++, pos += 8)
    frame.feedback.push({action : h(pos), accepted : h(pos + 4)});
  for (let j = 0; j < h(92); j++, pos += 16)
    frame.losses.push({id : token(obs, pos), observedAt : h(pos + 8)});
  return frame;
}
