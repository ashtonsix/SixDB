export const fmt = (n, places = 4) => Number(n.toFixed(places)).toString();
export const clamp = n => Math.max(0, Math.min(1, n));
export const smooth = n => { const x = clamp(n); return x ** 3 * (10 - 15 * x + 6 * x * x); };

export function bezier(points, t) {
  const z = 1 - t;
  return [0, 1].map(i => z ** 3 * points[0][i]
    + 3 * z * z * t * points[1][i]
    + 3 * z * t * t * points[2][i] + t ** 3 * points[3][i]);
}

export function tangent(points, t) {
  const z = 1 - t;
  return [0, 1].map(i => 3 * (z * z * (points[1][i] - points[0][i])
    + 2 * z * t * (points[2][i] - points[1][i])
    + t * t * (points[3][i] - points[2][i])));
}

export function parameterAtLength(table, length) {
  let lo = 0, hi = table.length - 1;
  while (lo + 1 < hi) {
    const mid = (lo + hi) >> 1;
    if (table[mid].s < length) lo = mid; else hi = mid;
  }
  const a = table[lo], b = table[hi];
  return a.t + (b.t - a.t) * (length - a.s) / (b.s - a.s);
}

// A route contains its start point followed by cubic controls and endpoints.
// Measurements happen at export time, never in the browser's animation loop.
export function measureRoute(route) {
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
  return { curves, totalLength };
}

export function pointAtDistance(measured, progress) {
  const s = clamp(progress) * measured.totalLength;
  const curve = measured.curves.find(c => c.start + c.length >= s) ?? measured.curves.at(-1);
  return bezier(curve.points, parameterAtLength(curve.table, Math.max(0, Math.min(curve.length, s - curve.start))));
}

export const routePath = route => `M${route[0].join(' ')}${route.slice(1).map(c => `C${c.join(' ')}`).join('')}`;

// Extend a pen route backwards on its first tangent so its moving tip can
// enter the fixed ink gradually, without an independently appearing start cap.
export function leadIn(route, distance) {
  const delta = route[1].slice(0, 2).map((v, i) => v - route[0][i]);
  const direction = delta.map(v => v / Math.hypot(...delta));
  const behind = length => route[0].map((v, i) => v - direction[i] * length);
  return { direction, route: [behind(distance),
    [...behind(distance * 2 / 3), ...behind(distance / 3), ...route[0]], ...route.slice(1)] };
}

// Fit a circular reveal tip to both sides of a filled outline. The drawing
// route may be slightly off-centre; a fixed radius would expose mask shoulders.
// Optional forward edge tangents let shaped caps join the ink smoothly.
// This intersection work runs only when exporting the animation.
export function outlineNib(outline, route, padding = 0.7, { edgeTangents = false } = {}) {
  const measured = measureRoute(route), edges = [];
  let previous = outline[0];
  for (const coordinates of outline.slice(1)) {
    const controls = [previous, coordinates.slice(0, 2), coordinates.slice(2, 4), coordinates.slice(4, 6)];
    for (let i = 1; i <= 100; i++) {
      const next = bezier(controls, i / 100);
      edges.push([previous, next]); previous = next;
    }
  }
  return u => {
    const p = pointAtDistance(measured, u);
    const a = pointAtDistance(measured, u - 0.00001), b = pointAtDistance(measured, u + 0.00001);
    const length = Math.hypot(b[0] - a[0], b[1] - a[1]);
    const t = [(b[0] - a[0]) / length, (b[1] - a[1]) / length], n = [-t[1], t[0]];
    let low = -Infinity, high = Infinity, lowEdge, highEdge;
    for (const [a, b] of edges) {
      const d = [b[0] - a[0], b[1] - a[1]], denominator = d[0] * t[0] + d[1] * t[1];
      if (Math.abs(denominator) < 1e-9) continue;
      const v = ((p[0] - a[0]) * t[0] + (p[1] - a[1]) * t[1]) / denominator;
      if (v < 0 || v >= 1) continue;
      const distance = (a[0] + v * d[0] - p[0]) * n[0] + (a[1] + v * d[1] - p[1]) * n[1];
      if (distance < 0 && distance > low) { low = distance; lowEdge = d; }
      if (distance >= 0 && distance < high) { high = distance; highEdge = d; }
    }
    if (!Number.isFinite(low + high)) throw new Error('Reveal route must stay inside its outline');
    const result = { p: p.map((v, i) => v + n[i] * (low + high) / 2), radius: (high - low) / 2 + padding };
    if (edgeTangents) result.tangents = [lowEdge, highEdge].map(d => {
      const direction = Math.sign(d[0] * t[0] + d[1] * t[1]);
      return d.map(v => v * direction / Math.hypot(...d));
    });
    return result;
  };
}

export function simplify(points, tolerance = 0.01) {
  const first = points[0], last = points.at(-1);
  const dx = last[0] - first[0], dy = last[1] - first[1], length2 = dx * dx + dy * dy;
  let farthest = 0, split = 0;
  for (let i = 1; i < points.length - 1; ++i) {
    const [x, y] = points[i];
    const t = length2 ? clamp(((x - first[0]) * dx + (y - first[1]) * dy) / length2) : 0;
    const distance = Math.hypot(x - first[0] - t * dx, y - first[1] - t * dy);
    if (distance > farthest) { farthest = distance; split = i; }
  }
  return farthest > tolerance
    ? [...simplify(points.slice(0, split + 1), tolerance).slice(0, -1), ...simplify(points.slice(split), tolerance)]
    : [first, last];
}
