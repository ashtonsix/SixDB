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
const fmt = (n, places = 4) => Number(n.toFixed(places)).toString();

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

function bezier(points, t) {
  const z = 1 - t;
  return [0, 1].map(i => z ** 3 * points[0][i]
    + 3 * z * z * t * points[1][i]
    + 3 * z * t * t * points[2][i] + t ** 3 * points[3][i]);
}
function tangent(points, t) {
  const z = 1 - t;
  return [0, 1].map(i => 3 * (z * z * (points[1][i] - points[0][i])
    + 2 * z * t * (points[2][i] - points[1][i])
    + t * t * (points[3][i] - points[2][i])));
}
function parameterAtLength(table, length) {
  let lo = 0, hi = table.length - 1;
  while (lo + 1 < hi) {
    const mid = (lo + hi) >> 1;
    if (table[mid].s < length) lo = mid; else hi = mid;
  }
  const a = table[lo], b = table[hi];
  return a.t + (b.t - a.t) * (length - a.s) / (b.s - a.s);
}

let previous = route[0], totalLength = 0;
const curves = route.slice(1).map(coordinates => {
  const points = [previous, coordinates.slice(0, 2), coordinates.slice(2, 4), coordinates.slice(4, 6)];
  const table = [{ t: 0, s: 0 }];
  let p = previous, length = 0;
  for (let i = 1; i <= 1000; ++i) {
    const q = bezier(points, i / 1000);
    length += Math.hypot(q[0] - p[0], q[1] - p[1]);
    table.push({ t: i / 1000, s: length }); p = q;
  }
  const start = totalLength; totalLength += length; previous = points[3];
  return { points, table, length, start };
});

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

function simplify(points, tolerance = 0.01) {
  const first = points[0], last = points.at(-1);
  const dx = last[0] - first[0], dy = last[1] - first[1], length2 = dx * dx + dy * dy;
  let farthest = 0, split = 0;
  for (let i = 1; i < points.length - 1; ++i) {
    const [x, y] = points[i];
    const t = length2 ? Math.max(0, Math.min(1, ((x - first[0]) * dx + (y - first[1]) * dy) / length2)) : 0;
    const distance = Math.hypot(x - first[0] - t * dx, y - first[1] - t * dy);
    if (distance > farthest) { farthest = distance; split = i; }
  }
  return farthest > tolerance
    ? [...simplify(points.slice(0, split + 1), tolerance).slice(0, -1), ...simplify(points.slice(split), tolerance)]
    : [first, last];
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

// Relative pen speeds at points in elapsed time. Quintic transitions join
// with zero first and second derivatives: speed, acceleration and jerk are
// continuous, and the pen never stops at an internal station.
const speeds = [
  [0, 0], [0.12, 1.5], [0.20, 1.45], [0.32, 0.33],
  [0.43, 1.3], [0.565, 0.96], [0.73, 1.0], [0.87, 1.2], [1, 0],
];
const speedArea = speeds.slice(1).reduce((sum, [t, v], i) =>
  sum + (t - speeds[i][0]) * (v + speeds[i][1]) / 2, 0);
const ramps = speeds.slice(1).map(([end, velocity], i) => ({
  start: speeds[i][0], end, span: end - speeds[i][0], delta: velocity - speeds[i][1],
}));

// Bake the shared speed curve into native SVG stroke animations. Sampling at
// 480 Hz keeps interpolation finer than display frames without per-frame math.
function progress(time) {
  let distance = 0;
  for (const ramp of ramps) {
    const x = Math.max(0, Math.min(1, (time - ramp.start) / ramp.span));
    distance += ramp.delta * (ramp.span * x ** 4 * (2.5 - 3 * x + x * x)
      + Math.max(0, time - ramp.end));
  }
  return Math.max(0, Math.min(1, distance / speedArea));
}
function timeAtDistance(distance) {
  let lo = 0, hi = 1;
  for (let i = 0; i < 40; ++i) {
    const mid = (lo + hi) / 2;
    if (progress(mid) < distance) lo = mid; else hi = mid;
  }
  return (lo + hi) / 2;
}
const reveals = curves.map((curve, i) => {
  const samples = pieces.filter(p => p.curveIndex === i);
  const width = Math.max(...samples.map(p => pressure(p.middle))) + 1;
  const start = curve.start / totalLength, length = curve.length / totalLength;
  const from = i ? timeAtDistance(start) : 0;
  const to = i === curves.length - 1 ? 1 : timeAtDistance(start + length);
  const steps = Math.ceil((to - from) * duration * 480);
  const easing = Array.from({ length: steps + 1 }, (_, j) =>
    fmt(j === 0 ? 0 : j === steps ? 1 : (progress(from + (to - from) * j / steps) - start) / length, 7));
  const min = [0, 1].map(axis => Math.min(...curve.points.map(p => p[axis])) - width / 2 - 1);
  const max = [0, 1].map(axis => Math.max(...curve.points.map(p => p[axis])) + width / 2 + 1);
  const nibSteps = Math.ceil((to - from) * duration * 240);
  const nib = Array.from({ length: nibSteps + 1 }, (_, j) => {
    const u = progress(from + (to - from) * j / nibSteps);
    const s = Math.max(0, Math.min(curve.length, (u - start) * totalLength));
    const [x, y] = bezier(curve.points, parameterAtLength(curve.table, s));
    return `      ${fmt(j / nibSteps * 100, 6)}% { cx:${fmt(x)}px; cy:${fmt(y)}px; r:${fmt(pressure(u) / 2)}px;${j === 0 ? ' visibility:visible; opacity:0;' : j === nibSteps ? ' visibility:visible; opacity:1;' : ''} }${j === 0 ? '\n      0.1% { opacity:1; }' : ''}`;
  }).join('\n');
  return { curve, width, from, to, easing, min, max, nib, contour: contour(curve, samples, i) };
});

export function renderSixDB({ animated = false, icon = false, reversed = false } = {}) {
  const color = animated || icon ? 'currentColor' : reversed ? '#f6f5f1' : '#1b1c18';
  const palette = '    :root { color: #1b1c18; }\n    @media (prefers-color-scheme: dark) { :root { color: #f6f5f1; } }';
  const css = animated ? `  <style>
${palette}
    .sixdb-animated .reveal {
      stroke-dasharray: 1 2;
    }
    @supports (animation-timing-function: linear(0, 1)) {
      .sixdb-animated .reveal {
        visibility: hidden;
        animation: sixdb-write var(--duration) var(--easing) var(--delay) forwards;
      }
      .sixdb-animated .nib, .sixdb-animated .start-cap {
        visibility: hidden;
        animation: var(--nib) var(--duration) linear var(--delay) forwards;
      }
      .sixdb-animated .start-cap { animation-name: sixdb-cap; animation-duration: 0.001s; }
    }
    @keyframes sixdb-write {
      from { visibility: visible; stroke-dashoffset: 1; }
      to { visibility: visible; stroke-dashoffset: 0; }
    }
    @keyframes sixdb-cap { from { visibility: visible; opacity: 0; } to { visibility: visible; opacity: 1; } }
${reveals.map(({ nib }, i) => `    @keyframes sixdb-nib-${i} {\n${nib}\n    }`).join('\n')}
    @media (prefers-reduced-motion: reduce) {
      .sixdb-animated .reveal { animation: none; visibility: visible; stroke-dashoffset: 0; }
      .sixdb-animated .nib, .sixdb-animated .start-cap { animation: none; visibility: visible; }
    }
  </style>\n` : icon ? `  <style>\n${palette}\n  </style>\n` : '';
  const paths = samples => samples.map(piece =>
    `    <path stroke-width="${fmt(pressure(piece.middle, icon))}" d="${piece.d}"/>`).join('\n');
  // Only the nine masks animate; ink stays static. Separate masks preserve
  // stroke order at crossings. Tight bounds reduce the surfaces being painted.
  // Butt-ended mask paths let the moving round nib define the leading edge.
  const masks = animated ? `  <defs>\n${reveals.map(({ curve, width, from, to, easing, min, max }, i) =>
    `    <mask id="sixdb-reveal-${i}" maskUnits="userSpaceOnUse" x="${fmt(min[0])}" y="${fmt(min[1])}" width="${fmt(max[0] - min[0])}" height="${fmt(max[1] - min[1])}" style="mask-type:alpha;--duration:${fmt((to - from) * duration, 7)}s;--delay:${fmt(from * duration, 7)}s;--easing:linear(${easing.join(',')});--nib:sixdb-nib-${i}">
      <path class="reveal" fill="none" stroke="white" stroke-width="${fmt(width)}" stroke-linecap="butt" pathLength="1" d="M${curve.points[0].join(' ')}C${curve.points.slice(1).flat().join(' ')}"/>
      <circle class="start-cap" cx="${fmt(curve.points[0][0])}" cy="${fmt(curve.points[0][1])}" r="${fmt(pressure(curve.start / totalLength) / 2)}" fill="white" stroke="none"/>
      <circle class="nib" cx="${fmt(curve.points[3][0])}" cy="${fmt(curve.points[3][1])}" r="${fmt(pressure((curve.start + curve.length) / totalLength) / 2)}" fill="white" stroke="none"/>
    </mask>`).join('\n')}\n  </defs>\n` : '';
  const ink = animated ? reveals.map(({ contour }, i) => `  <path mask="url(#sixdb-reveal-${i})" fill="${color}" stroke="none" d="${contour}"/>`).join('\n') : `  <g>\n${paths(pieces)}\n  </g>`;
  return `<svg xmlns="http://www.w3.org/2000/svg"${animated ? ' class="sixdb-animated"' : ''} viewBox="${icon ? '0 -30 240 240' : '0 0 240 180'}" fill="none" stroke="${color}" stroke-linecap="round" role="img">
  <title>SixDB</title>
  <desc>A signature-like mark sweeping from bottom left through a tight apex loop and slanted bowl to upper left.</desc>
${css}${masks}${ink}
</svg>\n`;
}
