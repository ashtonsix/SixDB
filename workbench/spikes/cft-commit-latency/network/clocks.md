# Clocks, one-way delay and directional asymmetry

The UDP studies measure each leg separately, rather than dividing
RTT by two. This guide owns the shared clock model and timestamp boundary;
[controlled comparisons](method.md) own the host/tuple sampling design.
The [original TCP method](studies/tcp-method.md) timed application RTT and uses
a different boundary, so its numbers are not directly interchangeable.

## What clock synchronization can establish

If B's clock is ahead of A by θ, uncorrected one-way observations are
`forward + θ` and `reverse − θ`. Their sum identifies the round trip, but their
difference cannot identify directional asymmetry separately from θ. Reversing
the initiator, repeating probes, choosing minimum RTTs, or fitting offsets from
these same links cannot remove this ambiguity without additional assumptions.

We collect **independent local clock references** on every host, without
synchronizing hosts over the cross-AZ links being measured:

- Supported M7i hosts read the ENA PTP hardware clock (PHC). Each read is
  bracketed by `CLOCK_MONOTONIC_RAW` and followed by its Nitro-reported
  `phc_error_bound`. The sampler refreshes PHC **before** fetching its bound,
  maps the PHC to its own PCI device and retries after failures.
- Every host samples the link-local Amazon Time Sync NTP endpoint. Az3's I4i
  hosts lack PHC and use NTP alone. Each exchange retains local raw send/receive
  times, server receive/send times, stratum, leap state, root delay, root
  dispersion and precision. No symmetry is assumed for the NTP path bounds.
- The corrected sampler requests PHC at 20 Hz on a separate physical core;
  NTP runs in a separate thread at no more than 1 Hz, with no catch-up bursts
  and a fresh socket per request. NTP waits cannot stall PHC, and a late reply
  cannot leave subsequent requests permanently one exchange behind. Actual
  sample timestamps, rather than the target rate, govern coverage and bounds.
  The system realtime clock is also bracketed. The study requests no clock
  steps or changes to the system's discipline.

The [initial cohort's audit](studies/oneway-initial.md#clock-and-instrumentation-audit)
retains its PHC cached-bound failure and slower sampling. The
[fresh confirmation](studies/oneway-confirmation.md#calibration-and-capture-audit)
tests the PHC/scheduling fix and records the remaining stale-NTP issue. Later
captures use the corrected socket behavior. Historical gaps retain their full
uncertainty; current source is not substituted for captured source.

PHC/error-bound association assumes that the study is the only PHC reader on
these single-NIC workers between the two userspace calls. The driver does not
provide an atomic userspace timestamp/bound pair. Captures show one ENA PHC and
one bound path per supported host, with stock systemd network time discipline;
concurrent reader exclusion was not independently instrumented. The current
sampler explicitly maps each PHC to its own PCI device's bound instead of
globally collecting NIC bounds. This is a stated capture assumption, not a
reduction in the advertised error.

The pinned Ubuntu kernel did not expose PHC with its stock ENA driver. The study
builds ENA 2.16.0 with PHC support, verifies the downloaded archive's SHA-256, and
records actual driver and device capabilities. Unsupported hardware remains
unsupported after loading that driver. This setup changes temporary study
workers only.

For an NTP exchange with local raw timestamps a,b and server UTC timestamps u,v,
the clock offset is bracketed by `[v−b−e, u−a+e]`, where e includes server root
distance, precision and conservative rounding of its 16.16 fields. Neither
local NTP direction is assumed symmetric for these bounds. A PHC read h bracketed
by a,b gives `[h−b−e, h−a+e]`.

Reference intervals are transferred to the event time and intersected within
±250 ms, always including both surrounding references when a gap is longer.
The main analysis permits **100 ppm** change in UTC-minus-raw-clock
offset per elapsed second; rate sensitivity is analyzed separately. This is an
explicit oscillator-rate envelope, not a hardware guarantee. Raw time avoids
system-clock steps and slews; observed independent-reference slopes and fit
residuals diagnose drift and reference noise. Contradictory intervals or missing
calibration coverage fail analysis rather than being silently fitted away.

The September 24 point estimates use a continuous, globally weighted affine fit to independent
reference centers. Its slope must fit the rate envelope, and its trajectory
must lie inside **every** reference interval; otherwise analysis fails. The
point model's smoothness is an additional modeling choice. Interval results
separately allow non-affine drift within the stated rate envelope and combine
all valid PHC/NTP anchors, including NTP fallback during a PHC outage.
On az3 the point fit remains sensitive to local NTP path asymmetry.
The interval, not the smoothness of the fit, governs
whether a directional difference is resolved. Thousands of packets never divide
systematic clock uncertainty by the square root of the sample count. A bias
at a leader shifts all its outgoing point estimates together and can favor
that host in selection. A packet holdout does not remove a clock bias shared
by training and validation; the error intervals still govern the claim.

A moving local point fit was rejected during analysis: changing which noisy NTP
anchors entered the window could create artificial discontinuities between the
two legs of an exchange. The retained continuous point models pass the
four-timestamp closure check. No cross-AZ observations are used to estimate
the clock corrections.

The longer [September 25 port capture](studies/port-sampling.md) exposed an
affine-model failure on three hosts: maximum reference violations of 6.80,
4.26 and 18.36 µs. Its secondary directional analysis therefore uses an explicit
`--clock-point-model feasible` alternative, chosen after that failure. Forward
and backward passes intersect reference intervals with the rate envelope; a
forward projection stays near the affine target while preserving reachability
of future intervals. Linear interpolation between those feasible anchor values
is continuous, satisfies every reference, and obeys the specified rate bound.
No measured link delay enters this construction, and the reference error/rate
envelopes are not widened. The old affine mode remains the default for historical
reproduction. A feasible curve is a permitted point trajectory, not proof of
the clock's actual fine-grained motion or smaller systematic error.

During packet measurement, the largest corrections to the affine target were
2.85 µs on az2b and 16.92 µs on az4a; az1b's violation occurred outside that
window. The **primary RTT policy results are unchanged by this choice**.
All 50/100/1000 ppm reductions pass reference, coverage and packet-causality
checks. `clock-model.json` records the deviation and `clocks.csv` records
the curve diagnostics in the new cohort's evidence.

## Timestamp boundaries and protocol controls

Linux `SO_TIMESTAMPING` records software TX and RX timestamps for each datagram.
TX timestamps are drained from the socket error queue with one outstanding send
per socket. RX comes from the packet's ancillary metadata. Missing timestamps,
truncated messages and mismatched exchange identities fail the probe. Every
timestamp retains a nearby raw/realtime/raw bracket, avoiding dependence on
long-interval interpolation through system-clock discipline. Mapping uncertainty
includes that bracket and a 1000 ppm possible realtime slew over timestamp age.
For realtime age A and rate limit s, the raw-position margin is half the bracket
plus `s*abs(A)/(1-s)` and rounding; UTC transfer expands this by `1+rho`.
Observable violations between wall/raw calibration brackets fail analysis.
This still assumes no undetected realtime step during an event-to-bracket gap;
finite sampling cannot rule out every transient step. The retained UDP cohorts
pass the observable check. The confirmation's fastest observed wall slew occurs during
startup, more than four minutes before packet measurement.

Software TX is near driver handoff, while software RX follows packet delivery
into the kernel; neither is an on-wire hardware timestamp. They exclude much
userspace scheduling overhead but retain guest/network/interrupt effects.
Application send/receive timestamps separately quantify syscall/scheduling
overhead. Where supported, hardware RX timestamps diagnose the additional
receive-side delay. There is no hardware TX timestamp on these devices.

Each request receives a same-size reply. Its four timestamps expose both legs,
server turnaround, and an offset-independent check: source elapsed time minus
destination turnaround. The sum of calibrated legs should agree with this check
up to clock rate and mapping effects. This closure check detects implementation
mistakes; it cannot prove that a constant cross-host offset is correct.

The [capture design](method.md) records packet counts, warmup, pacing, peer
matchings and role controls. The initial one-way cohorts opened new ephemeral
tuples between passes; only the later controls preserved explicit endpoint
ports across roles and rounds. This distinction is necessary before attributing
observed changes to time, connection reopening or flow identity.

Two hosts per AZ reveal host/path sensitivity. They do not independently sample
all racks, network routes or Nitro time references. In particular, shared clock
bias can be correlated, so replication does not justify averaging away the
advertised clock error. Same-AZ controls also have real path delay; they are not
zero-latency clock calibration links.

## Reports and interpretation

All timing tables use microseconds and type-7 quantiles. For each packet the
destination/source clock intervals give a delay interval. We also intersect
these with packet causality: both legs are nonnegative and their sum is the
offset-independent RTT, allowing its mapping and clock-rate error. This makes
the useful limit on poorly synchronized links explicit: either leg can occupy
almost the whole round trip. Clock-only median endpoints remain in the CSVs. Quantiles of lower and upper
endpoints bound the corresponding quantile of these observations. The
same construction bounds the median of paired forward-minus-reverse differences.
These are **conditional clock-error intervals, not statistical confidence
intervals** or population guarantees. Negative clock-only lower endpoints are
retained as diagnostics; the main intervals include nonnegative-delay constraints.
A point outside the causal interval fails analysis rather than being
silently clamped. Event joins validate endpoint role, exchange identity and
sequence range, and reject duplicates. The retained cohorts pass these checks.

Reports retain host-pair/tuple/round observations and distinguish request from
reply role. The first two cohorts also pool equal-budget AZ directions; the
fixed-tuple study instead assesses training-selected candidates on held-out
packets. A pooled difference does not establish the same asymmetry on every
host pair, and independently selected opposite directions cannot be subtracted
to obtain paired asymmetry.

## What the clocks actually resolved

At 100 ppm, the [initial cohort](studies/oneway-initial.md) resolved nonzero
median paired asymmetry in **9 of 240** cross-AZ host/pass blocks; the
[fresh confirmation](studies/oneway-confirmation.md) resolved **12 of 240**.
Neither resolved a pooled directional bias for any of the fifteen AZ pairs.
That is a limit on the evidence, not proof of symmetry. At the deliberately
wider 1000 ppm envelope the counts fell to **0 and 5**; at 50 ppm they were
**11 and 12**. Every envelope's results remain in the evidence.

With corrected PHC sampling, the fresh confirmation had typical median error
intervals of ±45–50 µs, compared with roughly ±60 µs in the initial cohort.
This also changed hosts and capture conditions, rather than isolating the
sampler change on the same machines. That still cannot
certify a 110 µs edge from a ~100–110 µs point estimate or decide which side
has a few-microsecond advantage. NTP-only az3 often permits almost the entire
RTT in either leg. Clock fit smoothness and sub-microsecond closure are not
independent evidence of absolute timing accuracy.

The much larger **same-host port-dependent RTT differences** in the
[selection study](selection.md) need no inter-host offset estimate. That is why
finding and retaining a fast pair has stronger evidence than assigning its
precise one-way split. The reported RTT still carries local rate and timestamp
mapping error; offset-independent does not mean error-free.

## Sources

- [AWS local clock access and supported instances](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/configure-ec2-ntp.html).
- [AWS PHC error bounds](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/compare-timestamps-with-clockbound.html).
- [Pinned ENA driver PHC documentation](https://github.com/amzn/amzn-drivers/blob/ena_linux_2.16.0/kernel/linux/ena/README.rst).
- [ENA cached-bound `EBUSY` behavior](https://github.com/amzn/amzn-drivers/blob/ena_linux_2.16.0/kernel/linux/common/ena_com/ena_com.c#L2037).
- [Linux timestamping boundaries and ancillary data](https://docs.kernel.org/networking/timestamping.html).
- [RFC 7679: one-way delay and measurement uncertainty](https://www.rfc-editor.org/rfc/rfc7679.html).
