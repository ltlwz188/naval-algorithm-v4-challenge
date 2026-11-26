import {observation, unit, obj, token} from './project.mjs';

const eventKinds = [
  'ShotReleased', 'Impact', 'PlaneDamage', 'ShipLost', 'Ownership',
  'ScoreChange', 'FlakPoint', 'Spotted', 'LostContact', 'ScoreAward',
  'SkillUsed', 'ShoreImpact', 'ShieldAbsorbed', 'ReloadAdvanced', 'TorpedoCleared', 'ShellBlocked', 'AirCrash'
];

export const fields = {
  observation: '本拍选手实际输入。side 是 teamOrder 的下标，具名数组长度对应 SDK Header 的各项计数。own、score、points 取拍初，敌情按 observedAt 采样。',
  actions: '本拍按提交顺序排列的 SDK Action。非有限浮点值以 NaN、Infinity、-Infinity 字符串表示。',
  feedback: 'observation.feedback 按 feedbackForTick 和从零起的 action 下标关联上一拍动作。actionFeedback 是裁判对本拍 actions 的受理结果，含终局。',
  truth: '赛后全图。units 为拍初全部单位，包括已沉没舰艇和甲板中队，contactId 对应观测里的匿名编号。objects 为拍初投射物，bornTick 为发射拍。公共据点见 observation.points。',
  score: '本拍结算后双方积分，顺序见 teamOrder。',
  events: '按事件实际结算 tick 归行。unitId 对应 truth.units[].id，team 和 point 均从零起。objectId 是裁判记录的投射物或匿名接触编号。detail 与 value 含义见 README。'
};

function actions(buffer) {
  const result = [];
  for (let i = 0; i < buffer.length; i += 40) {
    const number = off => {
      const value = buffer.readDoubleLE(i + off);
      return Number.isFinite(value) ? value : String(value);
    };
    result.push({
      unit: buffer.readInt32LE(i), verb: buffer.readInt32LE(i + 4),
      slot: buffer.readInt32LE(i + 8), count: buffer.readInt32LE(i + 12),
      x: number(16), y: number(24), spacing: number(32)
    });
  }
  return result;
}

function world(buffer, unitBytes) {
  const units = [], objects = [];
  let i = 0;
  for (let index = 0; index < 36; index++, i += unitBytes + 8) {
    const state = unit(buffer, i, Math.floor(index / 18));
    units.push({...state, contactId: token(buffer, i + unitBytes)});
  }
  for (; i < buffer.length; i += 48) {
    const {observedAt, ...object} = obj(buffer, i);
    objects.push({...object, bornTick: observedAt});
  }
  return {units, objects};
}

function events(buffer, units) {
  const result = [];
  for (let i = 0; i < buffer.length; i += 48) {
    const kind = buffer.readInt32LE(i), subject = buffer.readInt32LE(i + 8);
    const event = {
      kind: eventKinds[kind], tick: buffer.readInt32LE(i + 4),
      detail: buffer.readInt32LE(i + 12), value: buffer.readDoubleLE(i + 16)
    };
    if (kind === 4) event.point = subject;
    else if ([5, 7, 8, 9].includes(kind)) event.team = subject;
    else event.unitId = units[subject].id;
    if (buffer.readBigUInt64LE(i + 24) !== 0n)
      event.objectId = token(buffer, i + 24);
    if (event.kind === 'SkillUsed') event.targetId = units[event.value].id;
    if ([11, 15, 16].includes(kind)) {
      event.island = event.detail;
      event.x = buffer.readDoubleLE(i + 32);
      event.y = buffer.readDoubleLE(i + 40);
    }
    result.push(event);
  }
  return result;
}

// Buffer a single frame so next tick's actual feedback can be attached to
// the submitted actions. Movement can generate events stamped one tick ahead.
export function writeFrames(match, side, write) {
  let pending, futureEvents = [];
  const {unitBytes} = match.manifest;
  while (!match.result) {
    match.step();
    const raw = match.latest, input = raw[side];
    const view = observation(input, unitBytes);
    if (pending) write({...pending, actionFeedback: view.feedback});
    const tick = view.tick;
    const truth = world(raw[2], unitBytes);
    const settled = futureEvents;
    futureEvents = [];
    for (const event of events(raw[5], truth.units)) {
      if (event.tick === tick) settled.push(event);
      else if (event.tick === tick + 1) futureEvents.push(event);
      else throw Error('unexpected event tick ' + event.tick);
    }
    pending = {
      type: 'frame', tick,
      observation: {
        version: input.readUInt32LE(0), tick, side, seed: input.readUInt32LE(12),
        score: view.score, points: view.points, smokes: view.smokes,
        own: view.units.filter(u => !u.contact), contacts: view.units.filter(u => u.contact),
        incoming: view.objects, flashes: view.flashes, destroyed: view.losses,
        feedbackForTick: tick === 0 ? null : tick - 1, feedback: view.feedback
      },
      actions: actions(raw[3 + side]), truth,
      score: [match.status.blue, match.status.red], events: settled
    };
  }
  if (futureEvents.length) throw Error('event beyond final tick');
  // The judge evaluates final actions but never calls the player again.
  const final = observation(match.observation(side), unitBytes);
  write({...pending, actionFeedback: final.feedback});
}
