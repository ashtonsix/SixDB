// Engine-spike range reader over the qualified header-free logical16/F32 graph.
// Request/return/counter layout is declared in graph_scan.h. All borrows end here.
#if defined(__aarch64__)
.text
.p2align 4
.globl idx_asm_graph_scan64
.type idx_asm_graph_scan64,%function
idx_asm_graph_scan64:
.cfi_startproc
    // Five saved register pairs and six 32-byte iterator frames.
    sub sp,sp,#272
.cfi_def_cfa_offset 272
    stp x19,x20,[sp]
    stp x21,x22,[sp,#16]
    stp x23,x24,[sp,#32]
    stp x25,x26,[sp,#48]
    stp x27,x28,[sp,#64]
.cfi_offset 19,-272
.cfi_offset 20,-264
.cfi_offset 21,-256
.cfi_offset 22,-248
.cfi_offset 23,-240
.cfi_offset 24,-232
.cfi_offset 25,-224
.cfi_offset 26,-216
.cfi_offset 27,-208
.cfi_offset 28,-200
    mov x19,x0                       // Request.
    mov x20,x1                       // Optional counters.
    mov x21,xzr                      // Sum modulo 2^64.
    mov x22,xzr                      // Selected rows.
    mov x23,xzr                      // Present in-range dirents.
    mov x24,xzr                      // Visited final logical groups.
    mov x25,xzr                      // Entered graph nodes.
    cbz x19,.Lgs_finish
    ldr x2,[x19,#8]
    cbz x2,.Lgs_range
    ldr x3,[x19,#24]
    cbz x3,.Lgs_finish               // Empty explicit mask never touches root.
.Lgs_range:
    ldp w2,w3,[x19,#32]
    cmp w2,w3
    b.hi .Lgs_finish
    ldr x4,[x19]
    cbz x4,.Lgs_finish
    mov x25,#1
    lsr w27,w2,#4
    lsr w28,w3,#4
    ldp w5,w6,[x4,#8]
    cmp w27,w6
    b.hi .Lgs_finish
    cmp w28,w5
    b.lo .Lgs_finish
    cmp w27,w5
    csel w27,w27,w5,hs
    cmp w28,w6
    csel w28,w28,w6,lo
    ldr x16,[x19,#40]                // Optional output cursor.
    ldrb w2,[x4,#2]
    add w3,w2,#5
    lsr w1,w5,w3
    lsl w1,w1,w3                    // Common high group bits omitted by root.
    lsr w3,w27,w2
    and w3,w3,#31
    add x26,sp,#80
    stp x4,x1,[x26]                 // {node, group-base, shift, next-slot}.
    stp w2,w3,[x26,#16]

.Lgs_seek:
    ldp x0,x1,[x26]
    ldp w2,w3,[x26,#16]
    cmp w3,#32
    b.hs .Lgs_pop
    lsl x4,x3,x2
    orr x4,x1,x4
    cmp x4,x28
    b.hi .Lgs_finish                // Ordered DFS has passed the final group.
    add w5,w3,#1
    str w5,[x26,#20]
    add x5,x0,#64
    add x5,x5,x3,lsl #4
    ldr x6,[x5]
    cbz x6,.Lgs_seek
    cbz w2,.Lgs_group
    sub w2,w2,#5
    mov w3,wzr
    cmp x4,x27
    b.hs .Lgs_child
    lsr w3,w27,w2
    and w3,w3,#31
.Lgs_child:
    add x26,x26,#32
    stp x6,x4,[x26]
    stp w2,w3,[x26,#16]
    add x25,x25,#1
    b .Lgs_seek
.Lgs_pop:
    add x0,sp,#80
    cmp x26,x0
    b.eq .Lgs_finish
    sub x26,x26,#32
    b .Lgs_seek

.Lgs_group:
    add x24,x24,#1
    lsl x7,x4,#4                    // Original full u32 prefix coordinates.
    ldrh w8,[x5,#8]                 // ORIGINAL membership, retained for rank.
    mov w9,#0xffff
    ldr w10,[x19,#32]
    cmp x10,x7
    b.ls .Lgs_upper
    sub w10,w10,w7
    lsl w9,w9,w10
    and w9,w9,#0xffff
.Lgs_upper:
    add x11,x7,#15
    ldr w10,[x19,#36]
    cmp x10,x11
    b.hs .Lgs_candidates
    sub w10,w10,w7
    add w10,w10,#1
    mov w11,#1
    lsl w11,w11,w10
    sub w11,w11,#1
    and w9,w9,w11
.Lgs_candidates:
    and w9,w9,w8
    fmov d0,x9
    cnt v0.8b,v0.8b
    addv b0,v0.8b
    umov w10,v0.b[0]
    add x23,x23,x10
    cbz w9,.Lgs_seek
    ldr x12,[x19,#8]
    cbz x12,.Lgs_selected
    ldr x10,[x19,#16]
    cmp x7,x10
    b.lo .Lgs_seek
    sub x10,x7,x10
    ldr x11,[x19,#24]
    cmp x10,x11
    b.hs .Lgs_seek
    lsr x13,x10,#6
    ldr x12,[x12,x13,lsl #3]
    and x13,x10,#63
    lsr x12,x12,x13
    and w9,w9,w12
    sub x11,x11,x10
    cmp x11,#16
    b.hs .Lgs_selected
    mov w12,#1
    lsl w12,w12,w11
    sub w12,w12,#1
    and w9,w9,w12                    // Clip the final admitted mask word.
.Lgs_selected:
    cbz w9,.Lgs_seek
.Lgs_row:
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
    ldr x11,[x11]                    // Actual locator, not the descriptor value.
    // LLVM-MCA-BEGIN scan_material64
    ldp q0,q1,[x11]
    ldp q2,q3,[x11,#32]
    eor v0.16b,v0.16b,v1.16b
    eor v2.16b,v2.16b,v3.16b
    eor v0.16b,v0.16b,v2.16b
    umov x12,v0.d[0]
    umov x13,v0.d[1]
    eor x12,x12,x13
    add x21,x21,x12
    add x22,x22,#1
    // LLVM-MCA-END scan_material64
    cbz x16,.Lgs_row_done
    add w13,w7,w10
    stp w13,wzr,[x16]
    str x12,[x16,#8]
    add x16,x16,#16
.Lgs_row_done:
    cbnz w9,.Lgs_row
    b .Lgs_seek

.Lgs_finish:
    cbz x20,.Lgs_return
    stp x23,x24,[x20]
    lsl x2,x22,#6
    stp x25,x2,[x20,#16]
.Lgs_return:
    mov x0,x21
    mov x1,x22
    ldp x19,x20,[sp]
    ldp x21,x22,[sp,#16]
    ldp x23,x24,[sp,#32]
    ldp x25,x26,[sp,#48]
    ldp x27,x28,[sp,#64]
    add sp,sp,#272
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
.size idx_asm_graph_scan64,.-idx_asm_graph_scan64
#else
#error This Engine scan control currently targets AArch64.
#endif
.section .note.GNU-stack,""
