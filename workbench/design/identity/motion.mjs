import { clamp, fmt, measureRoute, pointAtDistance, routePath } from './geometry.mjs';

export function timingCurve(progress) {
  const timeAtDistance = distance => {
    if (distance <= 0) return 0;
    if (distance >= 1) return 1;
    let lo = 0, hi = 1;
    for (let i = 0; i < 40; ++i) {
      const mid = (lo + hi) / 2;
      if (progress(mid) < distance) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2;
  };
  return { progress, timeAtDistance };
}

// Quintic transitions between speed stations keep speed, acceleration and
// jerk continuous. Integrating them gives a monotonically increasing draw.
export function speedProfile(speeds) {
  const area = speeds.slice(1).reduce((sum, [t, v], i) =>
    sum + (t - speeds[i][0]) * (v + speeds[i][1]) / 2, 0);
  const ramps = speeds.slice(1).map(([end, velocity], i) => ({
    start: speeds[i][0], end, span: end - speeds[i][0], delta: velocity - speeds[i][1],
  }));
  const progress = time => {
    time = clamp(time);
    let distance = speeds[0][1] * time;
    for (const ramp of ramps) {
      const x = clamp((time - ramp.start) / ramp.span);
      distance += ramp.delta * (ramp.span * x ** 4 * (2.5 - 3 * x + x * x)
        + Math.max(0, time - ramp.end));
    }
    return clamp(distance / area);
  };
  return timingCurve(progress);
}

// Remove samples only when linear interpolation stays within the error budget
// in every coordinate. Time remains the independent axis, including near stops.
export function simplifyFrames(frames, coordinates, tolerance) {
  const values = frames.map(coordinates), keep = new Set([0, frames.length - 1]);
  const spans = [[0, frames.length - 1]];
  while (spans.length) {
    const [from, to] = spans.pop();
    let worst = tolerance, split = -1;
    for (let i = from + 1; i < to; i++) {
      const t = (frames[i].offset - frames[from].offset) / (frames[to].offset - frames[from].offset);
      const error = Math.max(...values[i].map((v, axis) =>
        Math.abs(v - (values[from][axis] + t * (values[to][axis] - values[from][axis])))));
      if (error > worst) { worst = error; split = i; }
    }
    if (split >= 0) { keep.add(split); spans.push([from, split], [split, to]); }
  }
  return [...keep].sort((a, b) => a - b).map(i => frames[i]);
}

// A nib may supply a cap path (`d`) instead of a circle. Its commands must
// match between frames so the browser can interpolate it natively.
export function strokeReveal({ route, width, radius, nib, duration, delay = 0, progress, startRadius }) {
  const measured = measureRoute(route);
  nib ??= u => ({ p: pointAtDistance(measured, u), radius: radius(u) });
  startRadius ??= nib(0).radius;
  const steps = Math.ceil(duration * 480);
  const easing = simplifyFrames(Array.from({ length: steps + 1 }, (_, j) => ({
    offset: j / steps, value: j === 0 ? 0 : j === steps ? 1 : progress(j / steps),
  })), f => [f.value], 0.00002);
  const nibSteps = Math.ceil(duration * 240);
  const sampled = Array.from({ length: nibSteps + 1 }, (_, j) =>
    ({ offset: j / nibSteps, ...nib(progress(j / nibSteps)) }));
  const extent = Math.max(width / 2, startRadius, ...sampled.map(f => f.radius)) + 1;
  const points = measured.curves.flatMap(c => c.points);
  const min = [0, 1].map(axis => Math.min(...points.map(p => p[axis])) - extent);
  const max = [0, 1].map(axis => Math.max(...points.map(p => p[axis])) + extent);
  const frames = simplifyFrames(sampled, f => f.d
    ? f.d.match(/-?\d*\.?\d+(?:e[-+]?\d+)?/gi).map(Number) : [...f.p, f.radius], 0.02);
  return { route, d: routePath(route), measured, width, nib, startRadius,
    duration, delay, progress, easing, frames, min, max };
}

export function strokeState(stroke, time) {
  const elapsed = time - stroke.delay, progress = stroke.progress(clamp(elapsed / stroke.duration));
  return { visible: elapsed >= 0, progress, ...stroke.nib(progress) };
}

// Remap a shared clock into each track's local easing. Geometry and relative
// phase stay intact; the exported animation still runs entirely in CSS.
export function retimeStroke(stroke, { at, inverse }) {
  const delay = inverse(stroke.delay), end = inverse(stroke.delay + stroke.duration);
  return strokeReveal({ ...stroke, delay, duration: end - delay,
    progress: p => stroke.progress((at(delay + p * (end - delay)) - stroke.delay) / stroke.duration),
  });
}

export function revealCSS(id, layers) {
  const names = layers.flatMap((layer, i) => (layer.strokes ?? []).map((stroke, j) => {
    const frames = stroke.frames.map(({ offset, p: [x, y], radius: r, d }, k, all) =>
      `      ${fmt(offset * 100, 6)}% { ${d ? `d:path('${d}');` : `cx:${fmt(x)}px; cy:${fmt(y)}px; r:${fmt(r)}px;`}${k === 0 ? ' visibility:visible; opacity:0;' : k === all.length - 1 ? ' visibility:visible; opacity:1;' : ''} }${k === 0 ? '\n      0.1% { opacity:1; }' : ''}`).join('\n');
    return `    @keyframes ${id}-nib-${i}-${j} {\n${frames}\n    }`;
  }));
  return `    #${id}.mark-animated .reveal { stroke-dasharray: 1 2; }
    @supports (animation-timing-function: linear(0, 1))${layers.some(l => l.strokes?.some(s => s.frames[0].d)) ? ' and (d:path("M0 0"))' : ''} {
      #${id}.mark-animated .reveal { visibility:hidden; animation:${id}-write var(--duration) var(--easing) var(--delay) forwards; }
      #${id}.mark-animated .nib, #${id}.mark-animated .start-cap { visibility:hidden; animation:var(--nib) var(--duration) linear var(--delay) forwards; }
      #${id}.mark-animated .start-cap { animation-name:${id}-cap; animation-duration:.001s; animation-timing-function:step-start; }
      #${id}.mark-animated .appear { animation:${id}-appear var(--duration) var(--appear-easing) var(--delay) both; }
    }
    @keyframes ${id}-write { from { visibility:visible; stroke-dashoffset:1; } to { visibility:visible; stroke-dashoffset:0; } }
    @keyframes ${id}-cap { from { visibility:visible; opacity:0; } to { visibility:visible; opacity:1; } }
    @keyframes ${id}-appear { from { opacity:0; transform:scale(var(--initial-scale,.82)); } to { opacity:1; transform:scale(1); } }
${names.join('\n')}
    @media (prefers-reduced-motion: reduce) {
      #${id}.mark-animated .reveal { animation:none; visibility:visible; stroke-dashoffset:0; }
      #${id}.mark-animated .nib, #${id}.mark-animated .start-cap { animation:none; visibility:visible; }
      #${id}.mark-animated .appear { animation:none; opacity:1; transform:none; }
    }`;
}

export function revealMask(id, index, strokes, time) {
  const min = [0, 1].map(axis => Math.min(...strokes.map(s => s.min[axis])));
  const max = [0, 1].map(axis => Math.max(...strokes.map(s => s.max[axis])));
  const children = strokes.map((s, j) => {
    const last = s.frames.at(-1), state = time === undefined ? null : strokeState(s, time);
    const p = state?.p ?? last.p, r = state?.radius ?? last.radius;
    const style = state ? ''
      : `--duration:${fmt(s.duration, 7)}s;--delay:${fmt(s.delay, 7)}s;--easing:linear(${s.easing.map(f => `${fmt(f.value, 7)} ${fmt(f.offset * 100, 6)}%`).join(',')});--nib:${id}-nib-${index}-${j}`;
    return `      <g style="${style}">
        <path class="reveal" fill="none" stroke="white" stroke-width="${fmt(s.width)}" stroke-linecap="butt"${state ? ` visibility="${state.visible ? 'visible' : 'hidden'}" stroke-dasharray="${fmt(s.measured.totalLength)} ${fmt(s.measured.totalLength * 2)}" stroke-dashoffset="${fmt((1 - state.progress) * s.measured.totalLength, 7)}"` : ' pathLength="1"'} d="${s.d}"/>${s.startRadius > 0 ? `
        <circle class="start-cap" cx="${fmt(s.frames[0].p[0])}" cy="${fmt(s.frames[0].p[1])}" r="${fmt(s.startRadius)}"${state && !state.visible ? ' visibility="hidden"' : ''} fill="white"/>` : ''}
        <${last.d ? 'path' : 'circle'} class="nib"${state && !state.visible ? ' visibility="hidden"' : ''} ${last.d ? `d="${state?.d ?? last.d}"` : `cx="${fmt(p[0])}" cy="${fmt(p[1])}" r="${fmt(r)}"`} fill="white"/>
      </g>`;
  }).join('\n');
  return `    <mask id="${id}-reveal-${index}" maskUnits="userSpaceOnUse" x="${fmt(min[0])}" y="${fmt(min[1])}" width="${fmt(max[0] - min[0])}" height="${fmt(max[1] - min[1])}" style="mask-type:alpha">\n${children}\n    </mask>`;
}
