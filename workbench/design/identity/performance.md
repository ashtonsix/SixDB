# Reveal performance

The exporter keeps the approved pen geometry and timing, but reduces redundant
keyframes within a bounded interpolation error, omits zero-radius start caps,
and scopes each mark's CSS. Standalone reveals contain no JavaScript.

## Playback check · 24 September 2026

Chrome 153 on the development Mac, 1041 × 875 CSS-pixel viewport, DPR 2,
300px-high stage, warm assets, DevTools open. Each mark and the combined case
ran three times at 1×, 6× and 20× CPU slowdown. Other browser tabs and extensions
were left in their existing state. These are local stress measurements, not
mobile-device benchmarks.

| Case | 1× p95 interval | 6× p95 interval | 20× rate before cleanup | 20× rate after cleanup |
| --- | ---: | ---: | ---: | ---: |
| SixDB | 9.2 ms | 9.2 ms | 30.2/s | 39.8/s |
| Consurgent | 9.2 ms | 9.3 ms | 18.3/s | 31.6/s |
| Orbital | 9.0 ms | 9.1 ms | 30.0/s | 42.2/s |
| Together | 9.1 ms | 14.8 ms | 18.2/s | 28.1/s |

Rates are mean `requestAnimationFrame` callbacks per second; intervals are the
median of the three per-run p95 values. They are not presented-frame counts.
The longest combined interval at 20× fell from 285.1ms to 106.5ms. Export sizes
fell by 27% for SixDB, 30% for Consurgent and 37% for Orbital.

Separate one-pass Performance recordings at 6× and 20× checked compositor
telemetry, with screenshots and JavaScript sampling disabled. Events were
counted inside each `reveal:…:start` / `reveal:…:end` interval:

| Case | 6× draw / partial / dropped events | 20× draw / partial / dropped events |
| --- | ---: | ---: |
| SixDB | 329 / 1 / 0 | 160 / 193 / 3 |
| Consurgent | 225 / 1 / 1 | 90 / 114 / 0 |
| Orbital | 236 / 1 / 0 | 109 / 132 / 2 |
| Together | 351 / 1 / 0 | 125 / 212 / 1 |

Here “partial” means `DroppedFrame` with `hasPartialUpdate: true`; “dropped”
means that flag is false. Partial frames can still miss the main-thread SVG
update, so neither category is treated as smooth logo playback. See Chrome's
[frame definitions](https://developer.chrome.com/docs/devtools/performance/reference#frames).
Recording changes the measurement conditions; these single-pass event counts
must not be mixed with the callback rates above. At 6×, interruptions are rare;
at 20×, the animations remain usable but do not keep up with every display slot.

## Reproduction and evidence

Serve this folder and open [performance.html](performance.html). Set DevTools
CPU throttling and the matching page label. Run with the tab visible, then save
measurements. Record a separate trace to inspect actual frame events. Setup time
is reported separately from playback. CPU slowdown does not emulate mobile GPU
performance or a complete phone.

Raw intervals, both compressed traces and the interpolation/raster checks are
retained in the workspace cache at `../.cache/sixdb-mark-review/final-pass/`
(relative to the repository root). Dense checks found unchanged static pixels
and unchanged deterministic reveal snapshots after cleanup, including 240Hz
sampling across Consurgent's critical 0.24–0.40s opening. Added tip-coordinate
interpolation error stayed below 0.02 SVG units against the dense reference.
