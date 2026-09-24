import test from 'node:test';
import assert from 'node:assert/strict';
import { measureRoute, outlineNib, pointAtDistance } from './geometry.mjs';
import { retimeStroke, simplifyFrames, speedProfile, strokeReveal, strokeState } from './motion.mjs';
import { renderMark } from './svg.mjs';
import { mark as sixdb } from './sixdb-pen.mjs';
import { mark as orbital } from './orbital-pen.mjs';
import { mark as consurgent } from './consurgent-pen.mjs';

test('arc-length sampling advances uniformly on a nonuniformly parameterized curve', () => {
  const route = measureRoute([[0, 0], [0, 1, 0, 2, 0, 10]]);
  for (const u of [0, .1, .25, .5, .9, 1]) {
    const [x, y] = pointAtDistance(route, u);
    assert.equal(x, 0);
    assert.ok(Math.abs(y - 10 * u) < 0.00001);
  }
  assert.deepEqual(pointAtDistance(route, -1), [0, 0]);
  assert.deepEqual(pointAtDistance(route, 2), [0, 10]);
});

test('speed integration is monotone, bounded and handles a nonzero initial speed', () => {
  for (const stations of [[[0, 0], [.3, 1.5], [.7, .5], [1, 0]], [[0, 2], [1, 2]]]) {
    const motion = speedProfile(stations);
    assert.equal(motion.progress(-1), 0);
    assert.ok(Math.abs(motion.progress(2) - 1) < 1e-12);
    let previous = 0;
    for (let i = 0; i <= 1000; i++) {
      const p = motion.progress(i / 1000);
      assert.ok(p >= previous - 1e-12 && p <= 1);
      previous = p;
    }
    for (const distance of [.01, .2, .5, .9, .99]) {
      assert.ok(Math.abs(motion.progress(motion.timeAtDistance(distance)) - distance) < 1e-10);
    }
  }
});

test('retiming preserves stroke geometry and stagger on a shared clock', () => {
  const timeline = { at: t => t * t / 2, inverse: t => Math.sqrt(2 * t) };
  for (const delay of [0, .2]) {
    const original = strokeReveal({ route: [[0, 0], [0, 3, 0, 7, 0, 10]],
      width: 4, radius: u => 1 + u, startRadius: 0, duration: .8, delay,
      progress: speedProfile([[0, 0], [.4, 1], [1, 0]]).progress });
    const retimed = retimeStroke(original, timeline);
    assert.equal(retimed.delay, timeline.inverse(delay));
    assert.ok(Math.abs(retimed.delay + retimed.duration - timeline.inverse(delay + .8)) < 1e-12);
    for (const fraction of [0, .15, .5, .85, 1]) {
      const time = retimed.delay + fraction * retimed.duration;
      const actual = strokeState(retimed, time), expected = strokeState(original, timeline.at(time));
      assert.ok(Math.abs(actual.progress - expected.progress) < 1e-12);
      assert.ok(Math.hypot(...actual.p.map((p, i) => p - expected.p[i])) < 1e-10);
      assert.ok(Math.abs(actual.radius - expected.radius) < 1e-12);
    }
  }
});

test('an off-centre reveal route fits its tip to both edges of the ink', () => {
  const outline = [[0, -4], [3, -4, 7, -4, 10, -4], [10, -1, 10, 1, 10, 4],
    [7, 4, 3, 4, 0, 4], [0, 1, 0, -1, 0, -4]];
  const route = [[2, 2], [4, 2, 6, 2, 8, 2]];
  const stroke = strokeReveal({ route, width: 16, nib: outlineNib(outline, route),
    duration: 1, progress: t => t });
  for (const u of [0, .2, .5, .8, 1]) {
    const { p, radius } = strokeState(stroke, u);
    assert.ok(Math.abs(p[0] - (2 + 6 * u)) < 1e-8);
    assert.ok(Math.abs(p[1]) < 1e-8);
    assert.ok(Math.abs(radius - 4.7) < 1e-8);
  }
  const fitted = outlineNib(outline, route, 0, { edgeTangents: true })(.5);
  for (const tangent of fitted.tangents) {
    assert.equal(tangent[0], 1);
    assert.equal(Math.abs(tangent[1]), 0);
  }
});

test('shaped reveal caps interpolate their paths and retain a complete fallback', () => {
  const cap = u => `M0 ${u}C0 ${u + 2} 4 ${u + 2} 4 ${u}Z`;
  const stroke = strokeReveal({ route: [[2, 0], [2, .3, 2, .7, 2, 1]], width: 4,
    duration: 1, progress: t => t, nib: u => ({ p: [2, u], radius: 2, d: cap(u) }) });
  const mark = { id: 'cap', title: 'Cap', description: 'Test', viewBox: [0, 0, 4, 4],
    ink: () => '<rect width="4" height="4"/>',
    revealLayers: () => [{ ink: '<rect width="4" height="4"/>', strokes: [stroke] }] };
  const animated = renderMark(mark, { animated: true });
  assert.ok(animated.includes('and (d:path("M0 0"))'));
  assert.ok(animated.includes(`d:path('${cap(0)}')`));
  assert.ok(animated.includes(`<path class="nib" d="${cap(1)}"`));
  assert.ok(renderMark(mark, { time: .5 }).includes(`<path class="nib" d="${cap(.5)}"`));
});

test('a stroke layer can fade in without scaling its fixed ink', () => {
  const { duration, delay = 0 } = orbital.revealLayers({})[1].appear;
  const halfway = renderMark(orbital, { time: delay + duration / 2 });
  assert.match(halfway, /mask="url\(#orbital-reveal-1\)" opacity="0.5" transform="translate\(0 0\) scale\(1\) translate\(0 0\)"/);
});

test('each reveal has bounded monotone motion and a small number of animated strokes', () => {
  const expected = new Map([[sixdb, 9], [orbital, 1], [consurgent, 4]]);
  for (const [mark, count] of expected) {
    const strokes = mark.revealLayers({ icon: false }).flatMap(l => l.strokes ?? []);
    assert.equal(strokes.length, count);
    for (const stroke of strokes) {
      assert.ok(stroke.duration > 0 && stroke.width > 0);
      assert.equal(stroke.easing[0].value, 0);
      assert.equal(stroke.easing.at(-1).value, 1);
      stroke.easing.slice(1).forEach((p, i) => assert.ok(p.value >= stroke.easing[i].value - 1e-7));
      assert.ok(stroke.frames.every(f => Number.isFinite(f.radius) && f.radius >= 0 && f.p.every(Number.isFinite)));
    }
  }
});

test('SVGs have complete fallbacks and independent inline mask/keyframe names', () => {
  for (const mark of [sixdb, orbital, consurgent]) {
    const first = renderMark(mark, { animated: true, id: `${mark.id}-a` });
    const second = renderMark(mark, { animated: true, id: `${mark.id}-b` });
    assert.ok(first.includes('@supports (animation-timing-function: linear(0, 1))'));
    assert.ok(first.includes('@media (prefers-reduced-motion: reduce)'));
    assert.ok(first.includes('animation:none; visibility:visible; stroke-dashoffset:0;'));
    assert.ok(!first.includes('<script'));
    for (const [, id] of first.matchAll(/url\(#([^)]+)\)/g)) assert.ok(first.includes(`id="${id}"`));
    assert.ok(!second.includes(`url(#${mark.id}-a`));
    assert.ok(!second.includes(`@keyframes ${mark.id}-a`));
  }
});

test('Orbital optical correction retains framing and adds planet and arc weight', () => {
  const standard = renderMark(orbital), small = renderMark(orbital, { icon: true });
  assert.equal(standard.match(/viewBox="([^"]+)"/)[1], small.match(/viewBox="([^"]+)"/)[1]);
  assert.ok(+small.match(/ r="([^"]+)"/)[1] > +standard.match(/ r="([^"]+)"/)[1]);
  assert.ok(small.includes('stroke-width="12"'));
});

test('sample reduction respects coordinate error, time spacing and endpoints', () => {
  const samples = Array.from({ length: 401 }, (_, i) => {
    const offset = (i / 400) ** 2;
    return { offset, coordinates: [Math.sin(offset * 9), offset ** 3] };
  });
  const reduced = simplifyFrames(samples, f => f.coordinates, .001);
  assert.ok(reduced.length < samples.length / 2);
  assert.equal(reduced[0], samples[0]);
  assert.equal(reduced.at(-1), samples.at(-1));
  for (const sample of samples) {
    const i = Math.max(1, reduced.findIndex(f => f.offset >= sample.offset));
    const a = reduced[i - 1], b = reduced[i], t = (sample.offset - a.offset) / (b.offset - a.offset);
    sample.coordinates.forEach((v, axis) =>
      assert.ok(Math.abs(v - (a.coordinates[axis] + t * (b.coordinates[axis] - a.coordinates[axis]))) <= .001));
  }
});

test('inline styles and fallback support cannot leak between marks', () => {
  for (const mark of [sixdb, consurgent, orbital]) {
    const svg = renderMark(mark, { animated: true, id: 'instance' });
    assert.ok(svg.includes('<svg id="instance"'));
    assert.ok(!svg.includes(':root'));
    assert.ok(!svg.match(/(?<!#instance)\.mark-animated/));
    assert.ok([...svg.matchAll(/@keyframes ([^ ]+)/g)].every(([, name]) => name.startsWith('instance-')));
  }
  assert.throws(() => renderMark(sixdb, { id: 'bad id' }));
});
