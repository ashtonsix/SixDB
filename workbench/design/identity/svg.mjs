import { clamp, fmt, smooth } from './geometry.mjs';
import { revealCSS, revealMask } from './motion.mjs';

export const palette = { ink: '#1b1c18', paper: '#f6f5f1' };

function appearanceAttributes(appear, time) {
  const { duration, delay = 0, origin = [0, 0], scale: initialScale = .82, progress = smooth } = appear;
  if (time === undefined) {
    const easing = Array.from({ length: 61 }, (_, j) => fmt(progress(j / 60), 7)).join(',');
    return ` class="appear" style="--duration:${duration}s;--delay:${delay}s;--initial-scale:${initialScale};--appear-easing:linear(${easing});transform-origin:${origin.map(x => `${x}px`).join(' ')}"`;
  }
  const p = progress(clamp((time - delay) / duration)), scale = initialScale + (1 - initialScale) * p;
  return ` opacity="${fmt(p)}" transform="translate(${origin.join(' ')}) scale(${fmt(scale)}) translate(${origin.map(x => -x).join(' ')})"`;
}

// All exports share palette, motion fallback and masking. Each mark supplies
// its approved ink, pen routes and timing; generated SVGs need no JavaScript.
export function renderMark(mark, { animated = false, icon = false, reversed = false, time, id = mark.id } = {}) {
  if (!/^[A-Za-z_][\w-]*$/.test(id)) throw new Error('SVG id must be a CSS-safe identifier');
  const motion = animated || time !== undefined;
  const layers = motion ? mark.revealLayers({ icon }) : [{ ink: mark.ink({ icon }) }];
  const viewBox = icon && mark.iconViewBox ? mark.iconViewBox : mark.viewBox;
  const color = reversed ? palette.paper : palette.ink;
  const paletteCSS = (animated || icon) && !reversed
    ? `    #${id} { color:${palette.ink}; }\n    @media (prefers-color-scheme:dark) { #${id} { color:${palette.paper}; } }\n` : '';
  const css = paletteCSS + (animated && time === undefined ? revealCSS(id, layers) : '');
  const masks = layers.flatMap((layer, i) => layer.strokes ? [revealMask(id, i, layer.strokes, time)] : []);
  const ink = layers.map((layer, i) => {
    const mask = layer.strokes ? ` mask="url(#${id}-reveal-${i})"` : '';
    const appear = motion && layer.appear ? appearanceAttributes(layer.appear, time) : '';
    return `  <g${mask}${appear}>${layer.ink}</g>`;
  }).join('\n');
  const body = motion && mark.revealAppear
    ? `  <g${appearanceAttributes(mark.revealAppear, time)}>\n${ink}\n  </g>` : ink;
  return `<svg id="${id}" xmlns="http://www.w3.org/2000/svg"${animated ? ' class="mark-animated"' : ''} viewBox="${viewBox.join(' ')}" color="${color}" fill="currentColor" role="img">
  <title>${mark.title}</title>
  <desc>${mark.description}</desc>
${css ? `  <style>\n${css}\n  </style>\n` : ''}${masks.length ? `  <defs>\n${masks.join('\n')}\n  </defs>\n` : ''}${body}
</svg>\n`;
}
