import { bezier, tangent, parameterAtLength, measureRoute, simplify, fmt, clamp, smooth, leadIn } from './geometry.mjs';
import { speedProfile, strokeReveal } from './motion.mjs';

// One continuous pen route: start point, then cubic control points and endpoints.
// The tiny apex loop fills through overlapping ink.
const route = [
  [18, 144],
  [64, 134, 120, 82, 166, 49],
  [189, 32.5, 206, 20, 212, 20],
  [216, 20, 216, 21, 212, 23],
  [196, 31, 176, 47, 158, 65],
  [140, 83, 128, 103, 126, 121],
  [123.666667, 142, 137, 151, 156, 149],
  [175, 147, 201, 128, 205, 109],
  [209, 90, 194, 71, 169, 59],
  [119, 35, 70, 35, 31, 48],
];
const duration = 2.6;
const gaussian = (x, centre, spread) => Math.exp(-(((x - centre) / spread) ** 2));

function pressure(u, icon = false) {
  const width = 11.6
    + 3.3 * gaussian(u, 0.16, 0.15)
    - 2.5 * gaussian(u, 0.339, 0.064)
    + 4.8 * gaussian(u, 0.535, 0.10)
    + 1.1 * gaussian(u, 0.735, 0.16)
    - 4.1 * gaussian(u, 0, 0.045)
    - 3.2 * gaussian(u, 1, 0.055);
  return icon ? 9.2 + width * 0.46 : width;
}

const { curves, totalLength } = measureRoute(route);

// SVG has no variable-width stroke. Static assets use short pieces that bound
// pressure changes to 0.05 units; the animation uses compact filled contours.
const pieces = curves.flatMap((curve, curveIndex) => {
  const count = Math.ceil(curve.length / 0.8);
  const intervals = [];
  const add = (s0, s1) => {
    if (Math.abs(pressure((curve.start + s1) / totalLength)
      - pressure((curve.start + s0) / totalLength)) > 0.05) {
      const mid = (s0 + s1) / 2; add(s0, mid); add(mid, s1);
    } else intervals.push([s0, s1]);
  };
  for (let i = 0; i < count; ++i) add(curve.length * i / count, curve.length * (i + 1) / count);
  return intervals.map(([s0, s1]) => {
    const t0 = parameterAtLength(curve.table, s0);
    const t1 = parameterAtLength(curve.table, s1);
    const a = bezier(curve.points, t0), d = bezier(curve.points, t1);
    const ta = tangent(curve.points, t0), td = tangent(curve.points, t1);
    const b = a.map((v, k) => v + ta[k] * (t1 - t0) / 3);
    const c = d.map((v, k) => v - td[k] * (t1 - t0) / 3);
    const start = (curve.start + s0) / totalLength;
    const length = (s1 - s0) / totalLength;
    return { curveIndex, points: [a, b, c, d], tangents: [ta, td], middle: start + length / 2,
      d: `M${a.map(v => fmt(v)).join(' ')}C${[...b, ...c, ...d].map(v => fmt(v)).join(' ')}` };
  });
});

// Preserve the tight apex as overlapping capsules with consistent winding.
function outline(piece) {
  const radius = pressure(piece.middle) / 2;
  const normals = piece.tangents.map(([x, y]) => {
    const scale = radius / Math.hypot(x, y);
    return [-y * scale, x * scale];
  });
  const side = sign => piece.points.map((point, i) =>
    point.map((value, axis) => value + sign * normals[i < 2 ? 0 : 1][axis]));
  const left = side(1), right = side(-1);
  const xy = points => points.flat().map(v => fmt(v)).join(' ');
  return `M${xy([left[0]])}C${xy(left.slice(1))}A${fmt(radius)} ${fmt(radius)} 0 0 0 ${xy([right[3]])}C${xy(right.slice(0, 3).reverse())}A${fmt(radius)} ${fmt(radius)} 0 0 0 ${xy([left[0]])}Z`;
}

function contour(curve, samples, index) {
  // At the tiny apex, the brush is wider than its turn radius. Keep overlapping
  // capsules there; elsewhere a disk envelope gives a compact, smooth outline.
  if (index === 2) return samples.map(outline).join('');
  const count = Math.ceil(curve.length / 0.4), left = [], right = [], ends = [];
  for (let i = 0; i <= count; ++i) {
    const s = curve.length * i / count, t = parameterAtLength(curve.table, s);
    const p = bezier(curve.points, t), velocity = tangent(curve.points, t);
    const direction = velocity.map(v => v / Math.hypot(...velocity));
    const u = (curve.start + s) / totalLength, radius = pressure(u) / 2;
    const slope = (pressure(u + 0.00001) - pressure(u - 0.00001)) / (0.00004 * totalLength);
    const normalScale = Math.sqrt(1 - slope * slope);
    for (const [side, sign] of [[left, 1], [right, -1]]) {
      side.push([p[0] + radius * (-slope * direction[0] - sign * normalScale * direction[1]),
        p[1] + radius * (-slope * direction[1] + sign * normalScale * direction[0])]);
    }
    if (i === 0 || i === count) ends.push({ p, radius });
  }
  const points = [...simplify(left), ...simplify(right).reverse()];
  const boundary = `M${points.map(p => p.map(v => fmt(v)).join(' ')).join('L')}Z`;
  return boundary + ends.map(({ p: [x, y], radius: r }) =>
    `M${fmt(x - r)} ${fmt(y)}a${fmt(r)} ${fmt(r)} 0 1 0 ${fmt(2 * r)} 0a${fmt(r)} ${fmt(r)} 0 1 0 ${fmt(-2 * r)} 0Z`).join('');
}

const speeds = [
  [0, 0], [0.12, 1.5], [0.20, 1.45], [0.32, 0.33],
  [0.43, 1.3], [0.565, 0.96], [0.73, 1.0], [0.87, 1.2], [1, 0],
];
const motion = speedProfile(speeds);
const layers = curves.map((curve, i) => {
  const samples = pieces.filter(p => p.curveIndex === i);
  const start = curve.start / totalLength, length = curve.length / totalLength;
  const from = i ? motion.timeAtDistance(start) : 0;
  const to = i === curves.length - 1 ? 1 : motion.timeAtDistance(start + length);
  const lead = i === 0 ? 9 : 0;
  const penRoute = [curve.points[0], curve.points.slice(1).flat()];
  const stroke = strokeReveal({
    route: lead ? leadIn(penRoute, lead).route : penRoute,
    width: Math.max(...samples.map(p => pressure(p.middle))) + 1,
    startRadius: lead ? 0 : undefined,
    radius: u => pressure(lead ? clamp((u * (curve.length + lead) - lead) / totalLength) : start + u * length) / 2,
    duration: (to - from) * duration,
    delay: from * duration,
    progress: t => {
      const phase = from + (to - from) * t;
      if (!lead) return clamp((motion.progress(phase) - start) / length);
      // Catch the original motion by 280ms without a change in speed at the join.
      return clamp((motion.progress(phase) * totalLength + lead * smooth(phase * duration / .28)) / (curve.length + lead));
    },
  });
  return { ink: `<path d="${contour(curve, samples, i)}"/>`, strokes: [stroke],
    ...(lead ? { appear: { duration: .18, scale: 1 } } : {}) };
});

export const mark = {
  id: 'sixdb', title: 'SixDB',
  description: 'A signature-like mark sweeping from bottom left through a tight apex loop and slanted bowl to upper left.',
  viewBox: [0, 0, 240, 180], iconViewBox: [0, -30, 240, 240],
  ink: ({ icon }) => `<g fill="none" stroke="currentColor" stroke-linecap="round">\n${pieces.map(piece =>
    `    <path stroke-width="${fmt(pressure(piece.middle, icon))}" d="${piece.d}"/>`).join('\n')}\n  </g>`,
  revealLayers: () => layers,
};
