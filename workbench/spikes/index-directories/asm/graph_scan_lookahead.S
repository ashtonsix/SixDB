// Engine-spike selected-material lookahead, not a Loom scheduling API.
// Same GraphScanRequest/ScanResult/ScanCounters contract as graph_scan.h.
#if defined(__aarch64__)
.text

// Generic ORIGINAL-mask rank in both controls. Each selected row stores one
// actual pointer; the no-output path does not touch the separate prefix bank.
.macro LA_CAPTURE hints
    rbit w10,w9
    clz w10,w10
    sub w11,w9,#1
    and w9,w9,w11
    mov w11,#1
    lsl w11,w11,w10
    sub w11,w11,#1
    and w11,w11,w8
    fmov d0,x11
    cnt v0.8b,v0.8b
    addv b0,v0.8b
    umov w11,v0.b[0]
    add x11,x6,x11,lsl #6
    ldr x11,[x11]
.if \hints
    prfm pldl1keep,[x11]
.endif
    str x11,[x14,x17,lsl #3]
.endm

.macro LA_DIGEST
    ldp q0,q1,[x11]
    ldp q2,q3,[x11,#32]
    eor v0.16b,v0.16b,v1.16b
    eor v2.16b,v2.16b,v3.16b
    eor v0.16b,v0.16b,v2.16b
    umov x12,v0.d[0]
    umov x13,v0.d[1]
    eor x12,x12,x13
    add x21,x21,x12
.endm

.macro LA_SCAN symbol, hints
.p2align 4
.globl \symbol
.type \symbol,%function
\symbol:
.cfi_startproc
    // Saved80 + frames192 + pointer-bank512 + optional-prefix-bank256.
    sub sp,sp,#1040
.cfi_def_cfa_offset 1040
    stp x19,x20,[sp]
    stp x21,x22,[sp,#16]
    stp x23,x24,[sp,#32]
    stp x25,x26,[sp,#48]
    stp x27,x28,[sp,#64]
.cfi_offset 19,-1040
.cfi_offset 20,-1032
.cfi_offset 21,-1024
.cfi_offset 22,-1016
.cfi_offset 23,-1008
.cfi_offset 24,-1000
.cfi_offset 25,-992
.cfi_offset 26,-984
.cfi_offset 27,-976
.cfi_offset 28,-968
    mov x19,x0
    mov x20,x1
    mov x21,xzr                     // SUM(row XOR8).
    mov x22,xzr                     // Drained selected rows.
    mov x23,xzr
    mov x24,xzr
    mov x25,xzr
    mov x17,xzr                     // Pointer-bank occupancy0..64.
    cbz x19,.Lla_finish\@
    ldr x2,[x19,#8]
    cbz x2,.Lla_range\@
    ldr x3,[x19,#24]
    cbz x3,.Lla_finish\@
.Lla_range\@:
    ldp w2,w3,[x19,#32]
    cmp w2,w3
    b.hi .Lla_finish\@
    ldr x4,[x19]
    cbz x4,.Lla_finish\@
    mov x25,#1
    lsr w27,w2,#4
    lsr w28,w3,#4
    ldp w5,w6,[x4,#8]
    cmp w27,w6
    b.hi .Lla_finish\@
    cmp w28,w5
    b.lo .Lla_finish\@
    cmp w27,w5
    csel w27,w27,w5,hs
    cmp w28,w6
    csel w28,w28,w6,lo
    ldr x16,[x19,#40]
    add x14,sp,#272
    add x15,sp,#784
    ldrb w2,[x4,#2]
    add w3,w2,#5
    lsr w1,w5,w3
    lsl w1,w1,w3
    lsr w3,w27,w2
    and w3,w3,#31
    add x26,sp,#80
    stp x4,x1,[x26]
    stp w2,w3,[x26,#16]

.Lla_seek\@:
    ldp x0,x1,[x26]
    ldp w2,w3,[x26,#16]
    cmp w3,#32
    b.hs .Lla_pop\@
    lsl x4,x3,x2
    orr x4,x1,x4
    cmp x4,x28
    b.hi .Lla_finish\@
    add w5,w3,#1
    str w5,[x26,#20]
    add x5,x0,#64
    add x5,x5,x3,lsl #4
    ldr x6,[x5]
    cbz x6,.Lla_seek\@
    cbz w2,.Lla_group\@
    sub w2,w2,#5
    mov w3,wzr
    cmp x4,x27
    b.hs .Lla_child\@
    lsr w3,w27,w2
    and w3,w3,#31
.Lla_child\@:
    add x26,x26,#32
    stp x6,x4,[x26]
    stp w2,w3,[x26,#16]
    add x25,x25,#1
    b .Lla_seek\@
.Lla_pop\@:
    add x0,sp,#80
    cmp x26,x0
    b.eq .Lla_finish\@
    sub x26,x26,#32
    b .Lla_seek\@

.Lla_group\@:
    add x24,x24,#1
    lsl x7,x4,#4
    ldrh w8,[x5,#8]
    mov w9,#0xffff
    ldr w10,[x19,#32]
    cmp x10,x7
    b.ls .Lla_upper\@
    sub w10,w10,w7
    lsl w9,w9,w10
    and w9,w9,#0xffff
.Lla_upper\@:
    add x11,x7,#15
    ldr w10,[x19,#36]
    cmp x10,x11
    b.hs .Lla_candidates\@
    sub w10,w10,w7
    add w10,w10,#1
    mov w11,#1
    lsl w11,w11,w10
    sub w11,w11,#1
    and w9,w9,w11
.Lla_candidates\@:
    and w9,w9,w8
    fmov d0,x9
    cnt v0.8b,v0.8b
    addv b0,v0.8b
    umov w10,v0.b[0]
    add x23,x23,x10
    cbz w9,.Lla_seek\@
    ldr x12,[x19,#8]
    cbz x12,.Lla_selected\@
    ldr x10,[x19,#16]
    cmp x7,x10
    b.lo .Lla_seek\@
    sub x10,x7,x10
    ldr x11,[x19,#24]
    cmp x10,x11
    b.hs .Lla_seek\@
    lsr x13,x10,#6
    ldr x12,[x12,x13,lsl #3]
    and x13,x10,#63
    lsr x12,x12,x13
    and w9,w9,w12
    sub x11,x11,x10
    cmp x11,#16
    b.hs .Lla_selected\@
    mov w12,#1
    lsl w12,w12,w11
    sub w12,w12,#1
    and w9,w9,w12
.Lla_selected\@:
    cbz w9,.Lla_seek\@
    cbz x16,.Lla_fill_none\@
    b .Lla_fill_output\@

.Lla_fill_none\@:
    LA_CAPTURE \hints
    add x17,x17,#1
    cmp x17,#64
    b.eq .Lla_full_none\@
.Lla_continue_none\@:
    cbnz w9,.Lla_fill_none\@
    b .Lla_seek\@
.Lla_fill_output\@:
    LA_CAPTURE \hints
    add w12,w7,w10
    str w12,[x15,x17,lsl #2]
    add x17,x17,#1
    cmp x17,#64
    b.eq .Lla_full_output\@
.Lla_continue_output\@:
    cbnz w9,.Lla_fill_output\@
    b .Lla_seek\@

.Lla_full_none\@:
    mov x5,xzr                      // Resume the current group after draining.
    b .Lla_drain_none\@
.Lla_full_output\@:
    mov x5,xzr
    b .Lla_drain_output\@
.Lla_finish\@:
    cbz x17,.Lla_finished\@
    mov x5,#1                       // Tail drain returns directly to epilogue.
    cbz x16,.Lla_drain_none\@
    b .Lla_drain_output\@

// These loops preserve current group{x6,x7,w8,w9} and every saved iterator frame.
.Lla_drain_none\@:
    add x22,x22,x17
    mov x0,x17
    mov x1,x14
.Lla_drain_none_row\@:
    ldr x11,[x1],#8
    LA_DIGEST
    subs x0,x0,#1
    b.ne .Lla_drain_none_row\@
    mov x17,xzr
    cbnz x5,.Lla_finished\@
    b .Lla_continue_none\@
.Lla_drain_output\@:
    add x22,x22,x17
    mov x0,x17
    mov x1,x14
    mov x2,x15
.Lla_drain_output_row\@:
    ldr x11,[x1],#8
    LA_DIGEST
    ldr w3,[x2],#4
    stp w3,wzr,[x16]
    str x12,[x16,#8]
    add x16,x16,#16
    subs x0,x0,#1
    b.ne .Lla_drain_output_row\@
    mov x17,xzr
    cbnz x5,.Lla_finished\@
    b .Lla_continue_output\@

.Lla_finished\@:
    cbz x20,.Lla_return\@
    stp x23,x24,[x20]
    lsl x2,x22,#6
    stp x25,x2,[x20,#16]
.Lla_return\@:
    mov x0,x21
    mov x1,x22
    ldp x19,x20,[sp]
    ldp x21,x22,[sp,#16]
    ldp x23,x24,[sp,#32]
    ldp x25,x26,[sp,#48]
    ldp x27,x28,[sp,#64]
    add sp,sp,#1040
.cfi_restore 19
.cfi_restore 20
.cfi_restore 21
.cfi_restore 22
.cfi_restore 23
.cfi_restore 24
.cfi_restore 25
.cfi_restore 26
.cfi_restore 27
.cfi_restore 28
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size \symbol,.-\symbol
.endm

LA_SCAN idx_asm_graph_scan64_lookahead,1
LA_SCAN idx_asm_graph_scan64_lookahead_none,0
#else
#error This Engine lookahead control currently targets AArch64.
#endif
.section .note.GNU-stack,""
