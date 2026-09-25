'use strict';
const $ = id => document.getElementById(id);
let scenario, result, presetData, frameIndex = 0, dirty = false, editorDirty = false;
const names = {reject: 'Reject all', older: 'Favor older', work: 'Retained work', arbitrate: 'Older + oracle'};
const fmt = value => value == null ? '—' : Number(value).toLocaleString();
const clone = value => JSON.parse(JSON.stringify(value));

async function api(path, data) {
  const response = await fetch(path, data === undefined ? {} : {
    method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(data)
  });
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || response.statusText);
  return body;
}

function showError(error) {
  $('error').hidden = false;
  $('error').textContent = error.message || String(error);
}

function changed() {
  dirty = true;
  $('status').textContent = 'Scenario changed. Run to update the results below.';
}

function loadScenario(value) {
  const candidate = clone(value.scenario || value);
  if (!candidate.shards || !candidate.transactions) throw new Error('Expected a scenario or exported replay.');
  scenario = candidate;
  scenario.policy ||= {};
  $('policy').value = scenario.policy.yield || 'older';
  $('reserve').value = scenario.policy.reserve_after ?? 3;
  $('backoff').value = scenario.policy.backoff ?? 4;
  $('delay').value = scenario.control_delay_us ?? 100;
  $('horizon').value = scenario.horizon_us ?? 8000;
  $('scenario').value = JSON.stringify(scenario, null, 2);
  editorDirty = false;
  changed();
}

function editedScenario() {
  if (editorDirty) throw new Error('Apply the edited scenario before running. Your JSON edits are still in the editor.');
  const s = clone(scenario);
  s.policy = {yield: $('policy').value, reserve_after: Number($('reserve').value), backoff: Number($('backoff').value)};
  s.control_delay_us = Number($('delay').value);
  s.horizon_us = Number($('horizon').value);
  $('scenario').value = JSON.stringify(s, null, 2);
  scenario = s;
  return s;
}

async function busy(task) {
  $('error').hidden = true;
  const controls = ['run', 'compare', 'generate', 'apply', 'preset', 'import', 'policy', 'reserve', 'backoff', 'delay', 'horizon', 'count', 'hot', 'seed', 'span', 'scenario'];
  for (const id of controls) $(id).disabled = true;
  $('status').textContent = 'Running the deterministic model…';
  try { await task(); } catch (error) { showError(error); $('status').textContent = 'Run failed.'; }
  finally { for (const id of controls) $(id).disabled = false; }
}

async function runScenario() {
  await busy(async () => {
    result = await api('/api/run', {scenario: editedScenario(), max_steps: 2000});
    dirty = false;
    frameIndex = result.frames.length - 1;
    $('results').hidden = false;
    $('comparison').hidden = true;
    $('export').disabled = false;
    $('scenario-title').textContent = `${result.scenario.name || 'Authored scenario'} · ${names[result.scenario.policy.yield]}`;
    $('model-id').textContent = `Model ${result.model_files_sha256['model.py'].slice(0, 10)} · scenario ${result.scenario_sha256.slice(0, 10)}`;
    $('frame').max = result.frames.length - 1;
    // Offer waiting transactions first, but keep identities stable when stepping.
    const pending = result.final.transactions.filter(t => t.state !== 'complete');
    const rest = result.final.transactions.filter(t => t.state === 'complete');
    $('transaction').replaceChildren(...[...pending, ...rest].map(t => {
      const o = document.createElement('option'); o.value = t.id; o.textContent = `${t.id} · ${t.kind}`; return o;
    }));
    $('status').textContent = `Stopped: ${result.stop_reason.replaceAll('_', ' ')} · ${fmt(result.summary.epochs)} shard epochs · invariants held on this run`;
    render();
  });
}

function cell(text, tag = 'td') { const e = document.createElement(tag); e.textContent = text; return e; }
function row(values) { const r = document.createElement('tr'); r.append(...values.map(v => cell(v))); return r; }

function render() {
  if (!result) return;
  frameIndex = Math.max(0, Math.min(frameIndex, result.frames.length - 1));
  const frame = result.frames[frameIndex], s = frame.summary;
  $('frame').value = frameIndex;
  $('completed').textContent = `${fmt(s.completed)} / ${fmt(s.completed + s.pending)}`;
  $('pending').textContent = `${fmt(s.pending)} pending · ${fmt(s.future)} future · oldest pending ${fmt(s.oldest_pending_us)} µs`;
  $('latency').textContent = `${fmt(s.p50_completed_us)} / ${fmt(s.p99_completed_us)}`;
  $('retries').textContent = `${fmt(s.counts.retry || 0)} / ${fmt(s.counts.discarded_work || 0)}`;
  $('cycle-status').textContent = s.cycles.length ? `Wait cycles: ${s.cycles.map(c => c.join(' ↔ ')).join('; ')}` : 'No wait cycle in this frame';
  $('time').textContent = `${fmt(frame.time_us)} µs · epoch ${fmt(frame.step)}`;
  $('frame-note').textContent = `Frames every ${result.frame_stride} shard epochs; ${frame.last_epoch ? `last: ${frame.last_epoch.shard} / ${frame.last_epoch.epoch} / ${frame.last_epoch.kind}` : 'initial state'}. Step buttons move between saved frames.`;
  $('start').disabled = $('prev').disabled = frameIndex === 0;
  $('next').disabled = $('end').disabled = frameIndex === result.frames.length - 1;
  drawTimeline(frame);
  renderTransaction(frame);
  const waits = frame.wait_edges;
  $('wait-summary').textContent = `${waits.length} part-to-part wait${waits.length === 1 ? '' : 's'}${s.cycles.length ? ' · cycle members: ' + s.cycles.flat().join(', ') : ''}`;
  $('waits').replaceChildren(...waits.slice(0, 100).map(w => {
    const d = document.createElement('div'); d.className = 'wait';
    d.textContent = `${w.from}.${w.part} → ${w.to}.${w.blocked_by} · shard ${w.shard}`; return d;
  }));
  if (waits.length > 100) $('waits').append(cell(`Showing 100 of ${waits.length} waits.`, 'p'));
  // Preserve the complete trace in export; the display is a bounded recent tail.
  const available = result.trace.slice(0, frame.log_position).filter(e => e.type !== 'epoch');
  const shown = available.slice(-60);
  $('trace-note').textContent = `last ${shown.length} of ${available.length} non-epoch transitions`;
  $('trace').replaceChildren(...shown.map(e => {
    const d = document.createElement('div'); d.className = 'trace-line';
    const time = document.createElement('span'); time.className = 'trace-time'; time.textContent = `${e.time_us} µs`;
    const detail = {...e}; delete detail.time_us; delete detail.type;
    d.append(time, document.createTextNode(`${e.type} ${JSON.stringify(detail)}`)); return d;
  }));
}

function renderTransaction(frame) {
  const tx = frame.transactions.find(t => t.id === $('transaction').value);
  if (!tx) return;
  $('tx-details').replaceChildren(cell(`${tx.state} · coordinator knows ${tx.known.length}/${tx.parts.length} parts · reports: ${Object.keys(tx.reported).join(', ') || 'none'}`, 'p'));
  $('parts').replaceChildren(...tx.parts.map(p => {
    const r = row([p.id, `${p.shard}: ${Object.entries(p.locks).map(([k,v]) => `${k} ${v}`).join(', ')}`, '', p.generation, p.attempts]);
    const badge = document.createElement('span'); badge.className = `state ${p.state}`; badge.textContent = p.state;
    r.children[2].append(badge);
    if (p.reserved) r.children[2].append(document.createTextNode(' · reserved'));
    if (p.state === 'ready' && p.retry_epoch > frame.epochs[p.shard]) r.children[2].append(document.createTextNode(` · retry ≥ epoch ${p.retry_epoch}`));
    return r;
  }));
}

function svgElement(tag, attrs, text) {
  const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [key, value] of Object.entries(attrs)) e.setAttribute(key, value);
  if (text !== undefined) e.textContent = text;
  return e;
}

function drawTimeline(frame) {
  const svg = $('timeline'), width = $('timeline-wrap').clientWidth;
  const shards = Object.keys(result.scenario.shards), height = 52 + shards.length * 46;
  svg.style.height = `${height}px`; svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  const left = 54, right = width - 24, duration = Math.max(1, result.summary.time_us);
  const x = t => left + t / duration * (right - left);
  const nodes = [svgElement('title', {}, 'Shard epochs; horizontal axis is synthetic time in microseconds')];
  const colors = {L: 'var(--l)', C1: 'var(--c1)', C2: 'var(--c2)', idle: 'var(--line)'};
  shards.forEach((s, i) => {
    const y = 12 + i * 46;
    nodes.push(svgElement('text', {x: 4, y: y + 17}, s));
    nodes.push(svgElement('line', {x1: left, x2: right, y1: y + 25, y2: y + 25, class: 'axis'}));
    for (const e of result.trace.filter(e => e.type === 'epoch' && e.shard === s)) {
      const stepWidth = Math.max(1, Math.min(16, (right - left) * result.scenario.shards[s].period_us / duration - 1));
      const rect = svgElement('rect', {x: x(e.time_us), y, width: Math.min(stepWidth, right - x(e.time_us) + 1), height: e.kind === 'idle' ? 3 : 23,
        fill: colors[e.kind], opacity: e.time_us <= frame.time_us ? 1 : .35, class: 'epoch'});
      rect.append(svgElement('title', {}, `${s} epoch ${e.epoch}: ${e.kind}, ${e.time_us} µs`));
      rect.addEventListener('click', () => {
        frameIndex = result.frames.reduce((best, f, i) => Math.abs(f.time_us - e.time_us) < Math.abs(result.frames[best].time_us - e.time_us) ? i : best, 0); render();
      });
      nodes.push(rect);
    }
  });
  nodes.push(svgElement('line', {x1: x(frame.time_us), x2: x(frame.time_us), y1: 4, y2: height - 29, class: 'cursor'}));
  const ticks = width < 500 ? 2 : 4;
  for (let i = 0; i <= ticks; i++) nodes.push(svgElement('text', {x: x(duration * i / ticks), y: height - 8,
    'text-anchor': i === 0 ? 'start' : i === ticks ? 'end' : 'middle'}, `${fmt(Math.round(duration * i / ticks))} µs`));
  svg.replaceChildren(...nodes);
}

$('run').addEventListener('click', runScenario);
$('preset').addEventListener('change', () => { loadScenario(presetData[$('preset').value]); runScenario(); });
for (const id of ['policy', 'reserve', 'backoff', 'delay', 'horizon']) $(id).addEventListener('change', changed);
$('scenario').addEventListener('input', () => { editorDirty = true; changed(); });
$('apply').addEventListener('click', () => { try { loadScenario(JSON.parse($('scenario').value)); } catch (e) { showError(e); } });
$('generate').addEventListener('click', () => busy(async () => {
  loadScenario(await api('/api/generate', {count: Number($('count').value), hot_percent: Number($('hot').value), seed: Number($('seed').value), arrival_span_us: Number($('span').value)}));
}));
$('compare').addEventListener('click', () => busy(async () => {
  const data = await api('/api/compare', {scenario: editedScenario(), max_steps: 2000});
  $('comparison').hidden = false;
  $('comparison-rows').replaceChildren(...data.comparisons.map(r => row([names[r.policy], r.completed, `${r.pending} / ${r.future}`,
    `${fmt(r.p50_completed_us)} / ${fmt(r.p99_completed_us)}`, fmt(r.oldest_pending_us), r.counts.retry || 0, r.counts.discarded_work || 0, r.stop_reason])));
  $('status').textContent = 'Policy comparison complete. The trace above remains the last single-policy run.';
}));
$('import').addEventListener('change', async () => {
  try { const file = $('import').files[0]; if (file) loadScenario(JSON.parse(await file.text())); } catch (e) { showError(e); }
});
$('export').addEventListener('click', () => busy(async () => {
  if (!result) return;
  const saved = await api('/api/save', {scenario: result.scenario, max_steps: result.max_steps,
    trace_sha256: result.trace_sha256, model_files_sha256: result.model_files_sha256});
  $('saved').hidden = false;
  $('saved').textContent = `Saved displayed run: ${saved.relative_path}`;
  $('status').textContent = dirty ? 'Replay saved. Pending scenario edits are not included.' : 'Replay saved and verified against the displayed trace.';
}));
$('start').onclick = () => { frameIndex = 0; render(); };
$('prev').onclick = () => { frameIndex--; render(); };
$('next').onclick = () => { frameIndex++; render(); };
$('end').onclick = () => { frameIndex = result.frames.length - 1; render(); };
$('frame').oninput = () => { frameIndex = Number($('frame').value); render(); };
$('transaction').onchange = () => renderTransaction(result.frames[frameIndex]);
new ResizeObserver(() => { if (result) drawTimeline(result.frames[frameIndex]); }).observe($('timeline-wrap'));

api('/api/presets').then(data => {
  presetData = data;
  $('preset').replaceChildren(...Object.entries(data).map(([id, s]) => { const o = document.createElement('option'); o.value = id; o.textContent = s.name; return o; }));
  $('preset').value = 'cycle'; loadScenario(data.cycle); return runScenario();
}).catch(showError);
