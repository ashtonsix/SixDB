import { leadIn, measureRoute, outlineNib, routePath, smooth } from './geometry.mjs';
import { speedProfile, strokeReveal } from './motion.mjs';

// Approved filled silhouette. Pen routes below control its reveal independently.
const outline = [
  [169.125, 12.847],
  [140.608, 15.921, 123.783, 47.569, 138.966, 72.535],
  [141.639, 76.931, 145.507, 80.863, 149.672, 83.873],
  [156.078, 88.503, 163.321, 89.858, 170.782, 91.81],
  [182.823, 94.959, 194.828, 98.252, 206.753, 101.824],
  [246.305, 113.671, 285.622, 126.866, 323.772, 142.707],
  [418.057, 181.856, 514.801, 237.382, 563.02, 331.485],
  [570.91, 346.882, 577.142, 362.999, 581.952, 379.605],
  [587.448, 398.583, 591.097, 418.208, 591.618, 438],
  [592.213, 460.64, 588.783, 483.207, 582.365, 504.892],
  [549.191, 616.97, 446.82, 685.761, 347.695, 735.201],
  [326.988, 745.529, 305.851, 755.14, 284.456, 763.96],
  [257.606, 775.029, 230.693, 786.283, 203.217, 795.725],
  [164.676, 808.968, 125.9, 820.673, 86.853, 832.292],
  [73.187, 836.358, 59.455, 840.232, 45.757, 844.186],
  [39.041, 846.124, 31.488, 847.512, 25.254, 850.741],
  [10.929, 858.16, 7.707, 879.5, 20.588, 889.96],
  [28.479, 896.368, 37.718, 895.603, 47, 893.552],
  [90.526, 883.934, 133.36, 871.798, 176.288, 859.846],
  [213.163, 849.579, 249.521, 837.016, 285.498, 823.987],
  [422.67, 774.311, 572.763, 702.675, 637.541, 563.039],
  [645.915, 544.987, 652.534, 526.154, 657.203, 506.808],
  [664.009, 478.612, 665.969, 449.895, 663.581, 421],
  [661.752, 398.878, 657.009, 377.095, 650.68, 355.853],
  [645.04, 336.927, 637.526, 318.53, 628.547, 300.943],
  [568.099, 182.533, 449.441, 109.934, 329.659, 61.884],
  [293.846, 47.518, 257.08, 35.363, 220.12, 24.324],
  [210.582, 21.476, 201.031, 18.53, 191.39, 16.055],
  [183.97, 14.15, 176.89, 12.011, 169.125, 12.847],
];

const planet = { centre: [428.579, 474.407], radius: 78.662 };
const route = [
  [36, 870],
  [284, 807, 628, 688, 628, 447],
  [628, 251, 392, 113, 174, 53],
];
const motion = speedProfile([[0, 0], [.10, 1.2], [.47, 1.55], [.68, 1.35], [.86, .95], [1, 0]]);
// Reach about 80% in the first 300ms, then taper into the final size and opacity.
const planetEntrance = speedProfile([[0, 0], [.30, 1.5], [.65, .8], [1, 0]]).progress;
const fittedNib = outlineNib(outline, route);
const routeLength = measureRoute(route).totalLength;
const lead = 56;
// Enter the silhouette from behind its origin, then continue along the same
// route. The cap is drawn by the advancing tip rather than a separate pop.
const { route: revealRoute, direction } = leadIn(route, lead);
const firstTip = fittedNib(0);
const arcInk = icon => `<path d="${routePath(outline)}Z"${icon ? ' stroke="currentColor" stroke-width="12" stroke-linejoin="round"' : ''}/>`;
const planetInk = icon => `<circle cx="${planet.centre[0]}" cy="${planet.centre[1]}" r="${planet.radius + (icon ? 4 : 0)}"/>`;

const layers = new Map([false, true].map(icon => [icon, [
    { ink: planetInk(icon), appear: { origin: planet.centre, duration: .47, delay: .30, progress: planetEntrance } },
    { ink: arcInk(icon), appear: { duration: .26, scale: 1 }, strokes: [strokeReveal({
      route: revealRoute, width: 132, duration: 1.8, progress: motion.progress,
      startRadius: 0,
      nib: u => {
        const distance = u * (routeLength + lead) - lead, inkProgress = distance / routeLength;
        const tip = distance < 0
          ? { p: firstTip.p.map((v, i) => v + direction[i] * distance), radius: firstTip.radius }
          : fittedNib(inkProgress);
        // The stationary terminal needs a little extra coverage beyond the
        // cross-section used by the moving tip.
        return { ...tip, radius: tip.radius + (icon ? 6 : 0) + 3 * smooth((inkProgress - .96) / .04) };
      },
    })] },
  ]]));

export const mark = {
  id: 'orbital', title: 'Orbital', viewBox: [0, 0, 677, 908],
  description: 'A tapered gravitational flyby around a planet, with a long approach and shorter departure.',
  ink: ({ icon }) => planetInk(icon) + arcInk(icon),
  revealLayers: ({ icon = false }) => layers.get(Boolean(icon)),
};
