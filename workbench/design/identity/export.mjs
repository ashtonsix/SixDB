import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mark as sixdb } from './sixdb-pen.mjs';
import { mark as orbital } from './orbital-pen.mjs';
import { mark as consurgent } from './consurgent-pen.mjs';
import { renderMark } from './svg.mjs';

const directory = dirname(fileURLToPath(import.meta.url));
for (const mark of [sixdb, orbital, consurgent]) {
  for (const [suffix, options] of [
    ['', {}], ['-reversed', { reversed: true }], ['-animated', { animated: true }],
  ]) writeFileSync(join(directory, `${mark.id}${suffix}.svg`), renderMark(mark, options));
}
for (const mark of [sixdb, orbital]) {
  writeFileSync(join(directory, `${mark.id}-icon.svg`), renderMark(mark, { icon: true }));
}
