# Mapping changes and remote TLB work

This is one performance question within the [views spike](../README.md).
It does not select a Loom policy or change Orbital's safety conditions.

The useful distinction is **keeping a mapping** versus **changing its page
tables**, rather than simply copying versus taking a view. Returning a page to
a mapped pool and reusing its bytes can avoid invalidation. Replacing its backing
or changing hardware protection can require synchronous work on other CPUs.

## What Linux can predict

On the normal x86 software path, Linux tracks CPUs using an address space.
It does not maintain a per-page set of CPUs that touched each view. A worker in
the same process can receive a shootdown while executing unrelated work.
CPUs running another address space, or in lazy TLB mode, can often defer the
flush until they return. Freeing page tables and address-space transitions have
stricter paths. These are kernel decisions, not a promise that sleeping one
worker makes an operation local. See the Linux 7.0
[x86 implementation](https://github.com/torvalds/linux/blob/v7.0/arch/x86/mm/tlb.c).

There are two important hardware qualifications:

- AMD's `INVLPGB` broadcasts invalidation through hardware. Linux 7.0 can use
  it for processes assigned a global ASID. Assignment is sampled during remote
  flushing with at least four CPUs active in that address space; it is not
  implied by a Zen5 machine name. The feature must be exposed and enabled.
  See the [AMD architecture manual](https://docs.amd.com/api/khub/documents/sD1_QL~h4Afq2_tvzxqqSQ/content)
  and the x86 implementation above.
- AArch64 ordinarily uses `TLBI` with an Inner Shareable scope and a completion
  barrier. This produces cross-core maintenance without a software TLB IPI.
  Range support and kernel workarounds affect the sequence. See the
  [Arm instruction definition](https://support.arm.com/documentation/ddi0601/2023-09/AArch64-Instructions/TLBI-VAE1IS--TLBI-VAE1ISNXS--TLB-Invalidate-by-VA--EL1--Inner-Shareable)
  and [Linux implementation](https://github.com/torvalds/linux/blob/v7.0/arch/arm64/include/asm/tlbflush.h).

An invalidation is distinct from a page fault. A valid page-table entry can be
reloaded into the TLB by hardware without a fault. A fault may populate a missing
mapping, allocate physical memory, or perform COW. Neither event establishes
whether the data are in L2, LLC or DRAM. The [Linux TLB guide](https://docs.kernel.org/arch/x86/tlb.html)
also distinguishes invalidation time from later refill cost.

## Experiment

`probe.cpp` gives each cell a fresh address space. It compares:

- One CPU; another same-process CPU running without touching the target; another
  that touched it once; three remote CPUs running without touching it.
- A same-process worker that touched the target and blocked on a futex; the same
  case with a separate process actively occupying that CPU; an independent
  process with its own mapping of the shared physical bytes.
- One and 64 pages; initially unpopulated in the caller, or written once and then
  read after each operation; unchanged protection, protection down/up in one
  range or page by page, `MAP_FIXED` replacement, and `munmap` followed by mapping.

The target uses shared anonymous memory so creating the independent process
does not silently change the caller's pages to COW. Helpers stop touching the
target before measurement. An independent expected-byte check runs afterward.
`populated=0` means the caller does not prepare or refault the target; a
`remote_seen` thread can still populate its shared page tables. In replacement
cases a helper's one initial touch does not follow the new mappings.

There are six forward/reverse trials, 20 warmup operations and 100 timed
operations per cell. Timings isolate the syscall sequence; post-operation reads
are separate. Fault deltas cover the measured process and include small fixed
measurement overhead. `/proc/interrupts` TLB deltas count remotely invoked flush
handlers, not invalidated pages, hardware broadcast messages, or precise time
spent in interrupt handlers. They are system-wide observations and can include
background work. Unavailable counters are `-1`, not zero. See the
[counter definition](https://github.com/torvalds/linux/blob/v7.0/arch/x86/kernel/irq.c).

Run from the repository root on Linux:

```sh
bash workbench/spikes/orbital-views/tlb/run.sh
python3 workbench/tools/worker.py run workbench/spikes/orbital-views/tlb/run.sh \
  --machine zen5 --instance-type c8a.xlarge --idle-seconds 0
```

Four allowed CPUs are required. The first command uses the pinned compiler and
does not build unrelated spike targets. Prefix Linux commands with
`orb -m ubuntu` from the Mac. `summarize.py` checks complete cell coverage and
produces medians and ranges of trial means.

## Observed on the four-vCPU Zen5 guest

Job `20261005T114640Z-23ec48a5`: fresh Spot c8a.xlarge, AMD EPYC 9R45,
Clang 21.1.8, Linux 7.0.0-1012-aws, 4 KiB pages. The caller ran on CPU 0;
helpers ran on distinct CPUs 1–3. These are medians of six trial means.

| One populated page, RO then RW | Syscall sequence | Remote TLB receipts per 100 cycles |
| --- | ---: | ---: |
| Local | 0.654 µs | 0 |
| Same-mm remote, target unseen | 4.544 µs | 200 |
| Same-mm remote, target seen once | 4.522 µs | 200 |
| Same-mm remote, now sleeping | 0.678 µs | 0 |
| Remote CPU switched to another mm | 0.680 µs | 0 |
| Independent process, shared bytes | 0.654 µs | 0 |
| Three same-mm remotes, target unseen | 5.377 µs | 600 |

With three remotes and 64 populated pages, protecting the range in two calls
took **7.760 µs**, with 600 remote receipts. Protecting each page individually
took **355.030 µs**, with a median 38,365 receipts. When neither caller nor
remote workers populated the target, those same two range calls took 0.501 µs
and recorded no remote receipts. Unchanged-protection calls likewise recorded
no receipts. The syscall name alone is therefore a poor predictor.

Unmapping and recreating 64 populated pages with three remotes took 14.699 µs
in the syscall sequence, followed by **51.372 µs** to read the new shared
anonymous pages. The latter caused 6,400 minor faults per 100 cycles, in addition
to three fixed overhead faults. That refault cost is separate from the 300
remote receipts. These mappings allocate zeroed shared pages on first read;
the result is not a cost estimate for reading an existing file mapping.

The guest kernel enables `CONFIG_BROADCAST_TLB_FLUSH` and reports a 33-page
single-page flush ceiling. The cells demonstrate the software path, including
its context dependence. **No direct INVLPGB CPUID or global-ASID receipt was
collected.** Linux does not print this feature in `/proc/cpuinfo`, so absence
from its flags is not evidence that the hypervisor hides it. Bare-metal
broadcast performance remains unmeasured.

[Selected raw trials and context](evidence/README.md) reproduce the full
140-cell comparison. Trial means and system-wide receipts do not bound tails
or measure interference with a useful remote workload.

## Patterns for Loom and Orbital

| Information already available | Useful inference | Limit |
| --- | --- | --- |
| Existing mapped allocation with compatible protection and backing | Reuse can avoid a mapping transition | New contents and authority still need preparation; captured users must have finished |
| Prepared-page bitmap and requested mapping action | Empty reservations differ from populated mappings | Actual residency, page-table freeing and external mapping users can differ |
| Active CPUs executing in SixDB's process | Estimate software shootdown fan-out | Includes unrelated views; Loom does not see every scheduler transition or non-Loom thread |
| Host features, kernel configuration and page size | Choose software or broadcast cost class | Hardware name alone is insufficient; global-ASID adoption is dynamic |
| Adjacent ranges with the same protection | Batch compatible changes | A large range can flush more unrelated TLB entries than many small ranges |
| Release age, pressure and available mapped inventory | Defer cold reclamation and favor reuse | Cache-temperature hints do not predict TLB state |

These inputs support a rough cost class, not an exact IPI oracle. Userspace does
not directly observe the kernel's active address-space mask, lazy state or ASID
generation. Kernel scheduling races make an exact prediction unsuitable as a
correctness condition.

For trusted views, keeping hardware protection stable while changing semantic
authority avoids needless `mprotect` work. For enforced worker protection,
prepare a private projection before handing it over and batch compatible page
ranges. Do not defer a required protection change or reuse a mapping still held
by a reader or port. Copying a few resident bytes into prepared storage can beat
a mapping transition, particularly with many same-process workers.

Move cold reclamation off the releasing thread, but also consider **when** it
runs. A background thread in the same process can still interrupt the foreground
workers. Pooling, coarse reclamation batches and natural quiescent periods may
reduce disturbance; pausing one owner thread does not quiesce the address space.
A separate process isolates its page-table changes, but adds communication and
ownership costs and is not justified by this experiment alone.

Also include likely **COW writes** in a cost estimate. Linux's
[`wp_page_copy`](https://github.com/torvalds/linux/blob/v7.0/mm/memory.c)
clears and flushes an existing translation before installing the private copy;
[`ptep_clear_flush`](https://github.com/torvalds/linux/blob/v7.0/mm/pgtable-generic.c)
can call `flush_tlb_page`. No explicit mapping syscall in the caller therefore
does not imply no shootdown. A missing-page write fault and a read-then-write
COW fault take different paths. This supports distinguishing access intention
when selecting acquisition methods; their comparative cost was not measured
by this matrix.

The probe does not measure bare-metal broadcast paths, Neoverse V2, page-table
freeing at aligned large boundaries, NUMA distance, huge pages, PMU walk counters,
UFFD, KVM secondary mappings or sustained tail latency. Those mechanisms need
their own host/context evidence before a calibrated policy uses them.
