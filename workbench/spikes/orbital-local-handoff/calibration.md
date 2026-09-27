# Why a size-only local latency model fails

The retired Python simulator's calibration challenge now lives beside its native
inputs. [local_calibration.py](local_calibration.py) fits preparation-to-consumption
median using only payload size, trained on three low-load spinning cases.
The historical [result](evidence/local-calibration.json) gives approximately
`171 ns + 0.0531 ns/byte`, within 2.6% of its training medians.

Held-out batching cases take roughly 7–15 times the prediction; sparse atomic
wait takes about 33 times it. Repeat ranges at the same payload size do not
overlap. A size-only curve therefore cannot explain these conditions, independently
of an arbitrary fit-error threshold. Per-run quantiles are not pooled or added.

The useful modeling boundary separates active preparation and consumption,
publication readiness, queue admission, polling occupancy and wakeup. A spinner
occupies nearly a core at low message rates. Preparing 32 × 16 KiB before
publication delays a 512 KiB group. Timestamp instrumentation also changes queue
balance and refusal rates. Neither these coefficients nor a subtracted clock-call
constant supplies production service costs; [findings](FINDINGS.md) retain the
matched native comparisons and their limits.

Reproduce without workers, from Linux (prefix `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/orbital-local-handoff/local_calibration.py \
  --output build/experiments/orbital-local-handoff/local-calibration.json
```

The retained JSON is unchanged from the old location. The relocated analysis
has a new source hash; its input hashes and all numerical results are unchanged.
This challenges a restricted candidate model; it does not calibrate the maintained
simulator's synthetic same-host TX/RX path.
