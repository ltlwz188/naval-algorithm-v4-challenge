import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

export const defaultChart = fileURLToPath(
  new URL('../maps/公开1.json', import.meta.url),
);
const size = 100000;
const spawnX = [
  -3000, 3000, -3000, 0, 3000, -4000, 0, 4000, -6000, -2000, 2000, 6000, -9000,
  9000,
];
const spawnY = [
  -8000, -8000, -3000, -3000, -3000, 1000, 1000, 1000, 6000, 6000, 6000, 6000,
  0, 0,
];

function islandKey([ax, ay, bx, by, r]) {
  if (ax > bx || (ax === bx && ay > by)) return [bx, by, ax, ay, r].join(',');
  return [ax, ay, bx, by, r].join(',');
}
function shoreDistance(x, y, [ax, ay, bx, by, r]) {
  const dx = bx - ax,
    dy = by - ay;
  const k = Math.max(
    0,
    Math.min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy || 1)),
  );
  return Math.hypot(x - ax - k * dx, y - ay - k * dy) - r;
}
// A connected chain of clear water cells certifies a route even while turning
// the largest hull. The cell half-diagonal covers the space between centers.
function certifyRoutes({ islands, zones }) {
  const pitch = 500,
    n = size / pitch,
    hull = Math.hypot(360, 70) / 2;
  const clearance = hull + pitch / Math.SQRT2;
  const water = new Uint8Array(n * n);
  for (let y = 0; y < n; y++)
    for (let x = 0; x < n; x++) {
      const px = (x + 0.5) * pitch,
        py = (y + 0.5) * pitch;
      if (
        Math.min(px, py, size - px, size - py) > clearance &&
        islands.every((i) => shoreDistance(px, py, i) > clearance)
      )
        water[y * n + x] = 1;
    }
  const queue = new Int32Array(n * n),
    component = new Int32Array(n * n);
  const reached = new Map();
  for (const sign of [1, -1])
    for (let slot = 0; slot < 14; slot++) {
      const x = 50000 + sign * spawnX[slot],
        y = (sign === 1 ? 15000 : 85000) + sign * spawnY[slot];
      const start = Math.floor(y / pitch) * n + Math.floor(x / pitch);
      if (!water[start]) throw Error('出生位附近缺少大舰转向净空');
      if (!component[start]) {
        let head = 0,
          tail = 1,
          mask = 0;
        const id = start + 1;
        queue[0] = start;
        component[start] = id;
        while (head < tail) {
          const p = queue[head++],
            cx = ((p % n) + 0.5) * pitch,
            cy = (Math.floor(p / n) + 0.5) * pitch;
          for (let j = 0; j < 3; j++)
            if (Math.hypot(cx - zones[j][0], cy - zones[j][1]) <= zones[j][2])
              mask |= 1 << j;
          for (const q of [p - n, p + n, p - 1, p + 1])
            if (water[q] && !component[q]) {
              queue[tail++] = q;
              component[q] = id;
            }
        }
        reached.set(id, mask);
      }
      if (reached.get(component[start]) !== 7)
        throw Error('无法验证出生位至全部据点的大舰航路');
    }
}
export function loadChart(source = defaultChart) {
  const chart =
    typeof source === 'string'
      ? JSON.parse(fs.readFileSync(source, 'utf8'))
      : source;
  const { id, name, zones, islands } = chart;
  if (
    typeof id !== 'string' ||
    !id.length ||
    typeof name !== 'string' ||
    !name.length ||
    !Array.isArray(zones) ||
    zones.length !== 3 ||
    !Array.isArray(islands) ||
    islands.length > 256
  )
    throw Error('海图字段或容量不合法');
  function tuple(v, n) {
    return Array.isArray(v) && v.length === n && v.every(Number.isFinite);
  }
  const within = (x, y, r) =>
    r > 0 && x >= r && y >= r && x <= size - r && y <= size - r;
  if (
    zones.some(
      (v, j) =>
        !tuple(v, 3) || v[2] !== [7000, 10000, 7000][j] || !within(...v),
    ) ||
    islands.some(
      (v) =>
        !tuple(v, 5) || !within(v[0], v[1], v[4]) || !within(v[2], v[3], v[4]),
    )
  )
    throw Error('占领圈或岛屿几何不合法');
  const heights = chart.heights === undefined ? islands.map(() => '低') : chart.heights;
  if (!Array.isArray(heights) || heights.length !== islands.length ||
      !heights.every(h => ['低', '中', '高'].includes(h)))
    throw Error('岛高必须与岛屿一一对应，取低、中或高');
  const sorted = (a) => JSON.stringify(a.sort());
  const zonesKey = sorted(zones.map((v) => v.join(','))),
    landKey = sorted(islands.map((s,i) => islandKey(s) + heights[i]));
  const symmetric = [false, true].some((rotate) => {
    const x = (v) => (rotate ? size - v : v);
    return (
      zonesKey ===
        sorted(zones.map(([a, b, r]) => [x(a), size - b, r].join(','))) &&
      landKey ===
        sorted(
          islands.map(([a, b, c, d, r], i) =>
            islandKey([x(a), size - b, x(c), size - d, r]) + heights[i],
          ),
        )
    );
  });
  if (!symmetric) throw Error('海图缺少可交换南北出生的对称变换');
  if (islands.length) certifyRoutes(chart);
  return { id, name, zones, islands, heights };
}
export function encodeChart({ zones, islands, heights }, abi = 6) {
  const stride = abi === 5 ? 40 : 48;
  const b = Buffer.alloc(80 + islands.length * stride);
  zones.flat().forEach((v, i) => b.writeDoubleLE(v, 8 * i));
  b.writeUInt32LE(islands.length, 72);
  islands.forEach((s,i) => {
    const start = 80 + i * stride;
    s.forEach((v,j) => b.writeDoubleLE(v, start+j*8));
    if (abi !== 5) b.writeUInt32LE(['低','中','高'].indexOf(heights?.[i] ?? '低'), start+40);
  });
  return b;
}
