// SixDB's editable centreline, pressure and timing live in sixdb-pen.mjs.
// Orbital and Consurgent retain their SVG masters.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderSixDB } from './sixdb-pen.mjs';

const directory = dirname(fileURLToPath(import.meta.url));
for (const [name, options] of [
  ['sixdb', {}], ['sixdb-reversed', { reversed: true }],
  ['favicon', { icon: true }], ['sixdb-animated', { animated: true }],
]) writeFileSync(join(directory, `${name}.svg`), renderSixDB(options));
const orbital = readFileSync(join(directory, 'orbital.svg'), 'utf8');
writeFileSync(join(directory, 'orbital-reversed.svg'),
  orbital.replaceAll('fill="#1b1c18"', 'fill="#f6f5f1"'));

// The corrected site-v1 animation reveals each gusset with its side stroke.
// Preserve its geometry from the static master and its original draw timing.
const consurgent = readFileSync(join(directory, 'consurgent.svg'), 'utf8');
const paths = consurgent.match(/<path\b[^>]*\/>/g);
if (paths?.length !== 5) throw new Error('Expected two Consurgent gussets and three strokes');
const [gussetLeft, gussetRight, left, trunk, right] = paths;
const reveal = (path, strand) => path
  .replace('<path', `<path class="reveal ${strand}" pathLength="1"`)
  .replaceAll('#1b1c18', 'currentColor');
const mask = (path, strand) => reveal(path, strand)
  .replace('stroke="currentColor"', 'stroke="#fff"')
  .replace(/stroke-width="[^"]*"/, 'stroke-width="16"');
const gusset = (path, strand) => path
  .replace('<path', `<path mask="url(#consurgent-${strand})"`)
  .replaceAll('#1b1c18', 'currentColor');

writeFileSync(join(directory, 'consurgent-animated.svg'), `<svg xmlns="http://www.w3.org/2000/svg" class="consurgent-animated" viewBox="44 22 112 168" role="img">
  <title>Consurgent</title>
  <desc>Three strands rise from a shared origin. The tapered gussets are revealed together with their strands.</desc>
  <style>
    :root { color: #1b1c18; }
    @media (prefers-color-scheme: dark) { :root { color: #f6f5f1; } }
    .consurgent-animated .reveal {
      stroke-dasharray: 1 1.1;
      stroke-dashoffset: 1.05;
      animation: consurgent-draw 1.4s cubic-bezier(0.4, 0.05, 0.15, 1) forwards;
    }
    .consurgent-animated .reveal.b { animation-delay: 0.08s; }
    .consurgent-animated .reveal.c { animation-delay: 0.16s; }
    @keyframes consurgent-draw { to { stroke-dashoffset: 0; } }
    @media (prefers-reduced-motion: reduce) {
      .consurgent-animated .reveal {
        animation: none;
        stroke-dasharray: none;
        stroke-dashoffset: 0;
      }
    }
  </style>
  <defs>
    <mask id="consurgent-left" maskUnits="userSpaceOnUse" x="0" y="0" width="200" height="212">
      ${mask(left, 'a')}
    </mask>
    <mask id="consurgent-right" maskUnits="userSpaceOnUse" x="0" y="0" width="200" height="212">
      ${mask(right, 'c')}
    </mask>
  </defs>
  ${gusset(gussetLeft, 'left')}
  ${gusset(gussetRight, 'right')}
  ${reveal(left, 'a')}
  ${reveal(trunk, 'b')}
  ${reveal(right, 'c')}
</svg>
`);
