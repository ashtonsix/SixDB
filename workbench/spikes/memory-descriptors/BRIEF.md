Spike and design a memory descriptor from this starting brief.

Descriptors are pointers, with an arena identifier held in bits 62:57, masked by software where LAM/UAI/TBI are unavailable.

Reservation class palette and hardware affinity vary by arena. For example, on a platform with 4096 B pages, the palettes available might be:

```
A: {16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, virtual}
B: {256, 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072, virtual}
C: virtual
```

The allocator heurestic might select palette C for an initial reservation >3072 B, fallthrough to select B for an initial reservation >192 or 64 B alignment, and otherwise default to A. Virtual reservations are large and mmap as-required.

Palettes can offer either 1 or 16 reservation class choices. Single-choice descriptors point directly to their payload; multi-choice descriptors point to a one-byte trampoline. Keeping trampolines small improves cache hit rate, mitigating pointer indirection cost. Trampolines identify which of 256 slots (16 per reservation class) holds their payload; at most, 16 of these slots will be occupied, allowing conflict-free growth.

The payload address is computed as `((trampoline_address + page_size) & ~(page_size - 1)) + (trampoline_address & (page_size - 1) & ~15) * LUT_superstride[palette] + LUT_offset[(palette, trampoline & 15)] + LUT_stride[(palette, trampoline & 15)] * (trampoline >> 4)`. Shuffle instructions and fixed superstride may optimise LUT lookups within single-arena hot loops; page size can be static. These addresses only require mapping when at least one payload overlaps the relevant page; compaction may relocate payloads to release pages. ...
