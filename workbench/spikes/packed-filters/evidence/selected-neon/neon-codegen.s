	.file	"codegen.cpp"
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function series8_equal
.LCPI0_0:
	.byte	1                               // 0x1
	.byte	2                               // 0x2
	.byte	4                               // 0x4
	.byte	8                               // 0x8
	.byte	16                              // 0x10
	.byte	32                              // 0x20
	.byte	64                              // 0x40
	.byte	128                             // 0x80
	.byte	1                               // 0x1
	.byte	2                               // 0x2
	.byte	4                               // 0x4
	.byte	8                               // 0x8
	.byte	16                              // 0x10
	.byte	32                              // 0x20
	.byte	64                              // 0x40
	.byte	128                             // 0x80
	.text
	.globl	series8_equal
	.p2align	2
	.type	series8_equal,@function
series8_equal:                          // @series8_equal
	.cfi_startproc
// %bb.0:
	cmp	x1, #255
	b.ls	.LBB0_2
// %bb.1:
	mov	x0, xzr
	ret
.LBB0_2:
	dup	v0.16b, w1
	ldr	q1, [x0]
	adrp	x8, .LCPI0_0
	cmeq	v0.16b, v1.16b, v0.16b
	ldr	q1, [x8, :lo12:.LCPI0_0]
	and	v0.16b, v0.16b, v1.16b
	ext	v1.16b, v0.16b, v0.16b, #8
	addv	b0, v0.8b
	fmov	w0, s0
	addv	b1, v1.8b
	fmov	w8, s1
	bfi	w0, w8, #8, #8
	ret
.Lfunc_end0:
	.size	series8_equal, .Lfunc_end0-series8_equal
	.cfi_endproc
                                        // -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function fused8_equal
.LCPI1_0:
	.byte	1                               // 0x1
	.byte	2                               // 0x2
	.byte	4                               // 0x4
	.byte	8                               // 0x8
	.byte	16                              // 0x10
	.byte	32                              // 0x20
	.byte	64                              // 0x40
	.byte	128                             // 0x80
	.byte	1                               // 0x1
	.byte	2                               // 0x2
	.byte	4                               // 0x4
	.byte	8                               // 0x8
	.byte	16                              // 0x10
	.byte	32                              // 0x20
	.byte	64                              // 0x40
	.byte	128                             // 0x80
	.text
	.globl	fused8_equal
	.p2align	2
	.type	fused8_equal,@function
fused8_equal:                           // @fused8_equal
	.cfi_startproc
// %bb.0:
	cmp	x1, #255
	b.ls	.LBB1_2
// %bb.1:
	mov	x0, xzr
	ret
.LBB1_2:
	dup	v0.16b, w1
	ldr	q1, [x0]
	adrp	x8, .LCPI1_0
	cmeq	v0.16b, v1.16b, v0.16b
	ldr	q1, [x8, :lo12:.LCPI1_0]
	and	v0.16b, v0.16b, v1.16b
	ext	v1.16b, v0.16b, v0.16b, #8
	addv	b0, v0.8b
	fmov	w0, s0
	addv	b1, v1.8b
	fmov	w8, s1
	bfi	w0, w8, #8, #8
	ret
.Lfunc_end1:
	.size	fused8_equal, .Lfunc_end1-fused8_equal
	.cfi_endproc
                                        // -- End function
	.globl	series3_equal                   // -- Begin function series3_equal
	.p2align	2
	.type	series3_equal,@function
series3_equal:                          // @series3_equal
	.cfi_startproc
// %bb.0:
	cmp	x1, #7
	b.ls	.LBB2_2
// %bb.1:
	mov	x0, xzr
	ret
.LBB2_2:
	ubfx	w8, w1, #1, #1
	ldrb	w10, [x0, #4]
	ldrb	w12, [x0, #1]
	lsr	x9, x1, #2
	and	w11, w1, #0x1
	ldrb	w14, [x0]
	sub	w8, w8, #1
	ldrb	w13, [x0, #3]
	sub	w11, w11, #1
	eor	w10, w8, w10
	eor	w8, w8, w12
	ldrb	w12, [x0, #2]
	sub	w9, w9, #1
	ldrb	w15, [x0, #5]
	eor	w14, w14, w11
	eor	w11, w13, w11
	and	w8, w14, w8
	eor	w12, w9, w12
	and	w10, w11, w10
	eor	w9, w9, w15
	and	w8, w8, w12
	and	w9, w10, w9
	and	w0, w8, #0xff
	bfi	w0, w9, #8, #8
	ret
.Lfunc_end2:
	.size	series3_equal, .Lfunc_end2-series3_equal
	.cfi_endproc
                                        // -- End function
	.globl	packed3_equal                   // -- Begin function packed3_equal
	.p2align	2
	.type	packed3_equal,@function
packed3_equal:                          // @packed3_equal
	.cfi_startproc
// %bb.0:
	cmp	x1, #7
	b.ls	.LBB3_2
// %bb.1:
	mov	x0, xzr
	ret
.LBB3_2:
	ubfx	w10, w1, #1, #1
	ldrb	w8, [x0]
	and	w9, w1, #0x1
	ldrb	w11, [x0, #1]
	lsr	x13, x1, #2
	sub	w9, w9, #1
	add	w10, w10, #255
	ldrb	w12, [x0, #2]
	ldrb	w14, [x0, #3]
	ldrb	w15, [x0, #4]
	eor	w8, w8, w9
	eor	w11, w10, w11
	and	w8, w8, w11
	ldrb	w11, [x0, #5]
	sub	w13, w13, #1
	eor	w9, w14, w9
	eor	w10, w10, w15
	eor	w12, w13, w12
	and	w8, w8, w12
	and	w9, w9, w10
	eor	w10, w13, w11
	and	x0, x8, #0xff
	and	w8, w9, w10
	bfi	x0, x8, #8, #8
	ret
.Lfunc_end3:
	.size	packed3_equal, .Lfunc_end3-packed3_equal
	.cfi_endproc
                                        // -- End function
	.globl	series64_less                   // -- Begin function series64_less
	.p2align	2
	.type	series64_less,@function
series64_less:                          // @series64_less
	.cfi_startproc
// %bb.0:
	dup	v0.2d, x1
	ldp	q2, q1, [x0]
	ldp	q3, q4, [x0, #32]
	cmhi	v1.2d, v0.2d, v1.2d
	cmhi	v2.2d, v0.2d, v2.2d
	cmhi	v3.2d, v0.2d, v3.2d
	cmhi	v4.2d, v0.2d, v4.2d
	mov	x8, v1.d[1]
	mov	x9, v2.d[1]
	fmov	x10, d1
	fmov	x12, d2
	ldp	q1, q2, [x0, #64]
	mov	x11, v3.d[1]
	and	x8, x8, #0x2
	cmhi	v1.2d, v0.2d, v1.2d
	and	x9, x9, #0x2
	sub	x8, x8, x10
	fmov	x10, d3
	sub	x9, x9, x12
	mov	x12, v4.d[1]
	and	x11, x11, #0x2
	cmhi	v2.2d, v0.2d, v2.2d
	orr	x8, x9, x8, lsl #2
	fmov	x13, d1
	sub	x9, x11, x10
	mov	x10, v1.d[1]
	fmov	x11, d4
	ldp	q3, q4, [x0, #96]
	orr	x8, x8, x9, lsl #4
	and	x9, x12, #0x2
	mov	x12, v2.d[1]
	sub	x9, x9, x11
	fmov	x11, d2
	cmhi	v3.2d, v0.2d, v3.2d
	orr	x8, x8, x9, lsl #6
	and	x9, x10, #0x2
	sub	x9, x9, x13
	cmhi	v0.2d, v0.2d, v4.2d
	orr	x8, x8, x9, lsl #8
	and	x9, x12, #0x2
	mov	x10, v3.d[1]
	sub	x9, x9, x11
	orr	x8, x8, x9, lsl #10
	fmov	x9, d3
	mov	x12, v0.d[1]
	and	x10, x10, #0x2
	sub	x9, x10, x9
	and	x10, x12, #0x2
	orr	x8, x8, x9, lsl #12
	fmov	x9, d0
	sub	x9, x10, x9
	orr	x0, x8, x9, lsl #14
	ret
.Lfunc_end4:
	.size	series64_less, .Lfunc_end4-series64_less
	.cfi_endproc
                                        // -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
