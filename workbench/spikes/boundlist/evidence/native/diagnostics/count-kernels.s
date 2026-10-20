	.file	"codegen.cpp"
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function bl_prefix8_pair
.LCPI0_0:
	.word	1                               // 0x1
	.word	2                               // 0x2
	.word	4                               // 0x4
	.word	8                               // 0x8
	.section	.text.bl_prefix8_pair,"ax",@progbits
	.hidden	bl_prefix8_pair
	.globl	bl_prefix8_pair
	.p2align	4
	.type	bl_prefix8_pair,@function
bl_prefix8_pair:                        // @bl_prefix8_pair
	.cfi_startproc
// %bb.0:
	ldp	s0, s4, [x0]
	movi	v1.2d, #0000000000000000
	adrp	x8, .LCPI0_0
	sub	w9, w2, #1
	ushll	v4.8h, v4.8b, #0
	ushll	v0.8h, v0.8b, #0
	ushll	v2.4s, v0.4h, #0
	ext	v2.16b, v1.16b, v2.16b, #12
	uaddw	v0.4s, v2.4s, v0.4h
	ext	v2.16b, v1.16b, v0.16b, #8
	add	v5.4s, v2.4s, v0.4s
	ldr	q0, [x8, :lo12:.LCPI0_0]
	dup	v2.4s, w1
	cmhi	v3.4s, v5.4s, v2.4s
	and	v3.16b, v3.16b, v0.16b
	addv	s3, v3.4s
	fmov	w8, s3
	dup	v3.4s, w9
	cmhi	v6.4s, v5.4s, v3.4s
	and	v6.16b, v6.16b, v0.16b
	addv	s6, v6.4s
	fmov	w9, s6
	ushll	v6.4s, v4.4h, #0
	ext	v6.16b, v1.16b, v6.16b, #12
	uaddw	v4.4s, v6.4s, v4.4h
	ext	v6.16b, v1.16b, v4.16b, #8
	add	v4.4s, v6.4s, v4.4s
	dup	v6.4s, v5.s[3]
	add	v6.4s, v4.4s, v6.4s
	add	v4.4s, v4.4s, v5.4s
	cmhi	v7.4s, v6.4s, v2.4s
	cmhi	v6.4s, v6.4s, v3.4s
	and	v6.16b, v6.16b, v0.16b
	and	v7.16b, v7.16b, v0.16b
	addv	s6, v6.4s
	addv	s7, v7.4s
	fmov	w11, s6
	ldp	s5, s6, [x0, #8]
	fmov	w10, s7
	orr	w8, w8, w10, lsl #4
	orr	w9, w9, w11, lsl #4
	ushll	v5.8h, v5.8b, #0
	ushll	v7.4s, v5.4h, #0
	ext	v7.16b, v1.16b, v7.16b, #12
	uaddw	v5.4s, v7.4s, v5.4h
	ext	v7.16b, v1.16b, v5.16b, #8
	add	v5.4s, v7.4s, v5.4s
	dup	v7.4s, v4.s[3]
	add	v7.4s, v7.4s, v5.4s
	add	v4.4s, v5.4s, v4.4s
	ushll	v5.8h, v6.8b, #0
	ushll	v6.4s, v5.4h, #0
	dup	v4.4s, v4.s[3]
	cmhi	v16.4s, v7.4s, v2.4s
	cmhi	v7.4s, v7.4s, v3.4s
	ext	v6.16b, v1.16b, v6.16b, #12
	and	v7.16b, v7.16b, v0.16b
	and	v16.16b, v16.16b, v0.16b
	uaddw	v5.4s, v6.4s, v5.4h
	addv	s7, v7.4s
	addv	s16, v16.4s
	ext	v1.16b, v1.16b, v5.16b, #8
	fmov	w10, s7
	add	v1.4s, v1.4s, v5.4s
	fmov	w12, s16
	orr	w9, w9, w10, lsl #8
	orr	w8, w8, w12, lsl #8
	add	v1.4s, v1.4s, v4.4s
	cmhi	v2.4s, v1.4s, v2.4s
	cmhi	v1.4s, v1.4s, v3.4s
	and	v2.16b, v2.16b, v0.16b
	and	v0.16b, v1.16b, v0.16b
	addv	s2, v2.4s
	addv	s0, v0.4s
	fmov	w10, s2
	orr	w8, w8, w10, lsl #12
	fmov	w10, s0
	orr	w9, w9, w10, lsl #12
	orr	x0, x8, x9, lsl #32
	ret
.Lfunc_end0:
	.size	bl_prefix8_pair, .Lfunc_end0-bl_prefix8_pair
	.cfi_endproc
                                        // -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function bl_prefix16_pair
.LCPI1_0:
	.word	1                               // 0x1
	.word	2                               // 0x2
	.word	4                               // 0x4
	.word	8                               // 0x8
	.section	.text.bl_prefix16_pair,"ax",@progbits
	.hidden	bl_prefix16_pair
	.globl	bl_prefix16_pair
	.p2align	4
	.type	bl_prefix16_pair,@function
bl_prefix16_pair:                       // @bl_prefix16_pair
	.cfi_startproc
// %bb.0:
	ldp	d0, d4, [x0]
	movi	v1.2d, #0000000000000000
	adrp	x8, .LCPI1_0
	sub	w9, w2, #1
	ushll	v2.4s, v0.4h, #0
	ext	v2.16b, v1.16b, v2.16b, #12
	uaddw	v0.4s, v2.4s, v0.4h
	ext	v2.16b, v1.16b, v0.16b, #8
	add	v5.4s, v2.4s, v0.4s
	ldr	q0, [x8, :lo12:.LCPI1_0]
	dup	v2.4s, w1
	cmhi	v3.4s, v5.4s, v2.4s
	and	v3.16b, v3.16b, v0.16b
	addv	s3, v3.4s
	fmov	w8, s3
	dup	v3.4s, w9
	cmhi	v6.4s, v5.4s, v3.4s
	and	v6.16b, v6.16b, v0.16b
	addv	s6, v6.4s
	fmov	w9, s6
	ushll	v6.4s, v4.4h, #0
	ext	v6.16b, v1.16b, v6.16b, #12
	uaddw	v4.4s, v6.4s, v4.4h
	ext	v6.16b, v1.16b, v4.16b, #8
	add	v4.4s, v6.4s, v4.4s
	dup	v6.4s, v5.s[3]
	add	v6.4s, v4.4s, v6.4s
	add	v4.4s, v4.4s, v5.4s
	cmhi	v7.4s, v6.4s, v2.4s
	cmhi	v6.4s, v6.4s, v3.4s
	and	v6.16b, v6.16b, v0.16b
	and	v7.16b, v7.16b, v0.16b
	addv	s6, v6.4s
	addv	s7, v7.4s
	fmov	w11, s6
	ldp	d5, d6, [x0, #16]
	orr	w9, w9, w11, lsl #4
	fmov	w10, s7
	orr	w8, w8, w10, lsl #4
	ushll	v7.4s, v5.4h, #0
	ext	v7.16b, v1.16b, v7.16b, #12
	uaddw	v5.4s, v7.4s, v5.4h
	ext	v7.16b, v1.16b, v5.16b, #8
	add	v5.4s, v7.4s, v5.4s
	dup	v7.4s, v4.s[3]
	add	v7.4s, v7.4s, v5.4s
	add	v4.4s, v5.4s, v4.4s
	ushll	v5.4s, v6.4h, #0
	ext	v5.16b, v1.16b, v5.16b, #12
	dup	v4.4s, v4.s[3]
	cmhi	v16.4s, v7.4s, v2.4s
	cmhi	v7.4s, v7.4s, v3.4s
	uaddw	v5.4s, v5.4s, v6.4h
	and	v7.16b, v7.16b, v0.16b
	and	v16.16b, v16.16b, v0.16b
	ext	v1.16b, v1.16b, v5.16b, #8
	addv	s7, v7.4s
	addv	s16, v16.4s
	add	v1.4s, v1.4s, v5.4s
	add	v1.4s, v1.4s, v4.4s
	fmov	w10, s7
	fmov	w12, s16
	orr	w9, w9, w10, lsl #8
	orr	w8, w8, w12, lsl #8
	cmhi	v2.4s, v1.4s, v2.4s
	cmhi	v1.4s, v1.4s, v3.4s
	and	v2.16b, v2.16b, v0.16b
	and	v0.16b, v1.16b, v0.16b
	addv	s2, v2.4s
	addv	s0, v0.4s
	fmov	w10, s2
	orr	w8, w8, w10, lsl #12
	fmov	w10, s0
	orr	w9, w9, w10, lsl #12
	orr	x0, x8, x9, lsl #32
	ret
.Lfunc_end1:
	.size	bl_prefix16_pair, .Lfunc_end1-bl_prefix16_pair
	.cfi_endproc
                                        // -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          // -- Begin function bl_direct16_pair
.LCPI2_0:
	.byte	1                               // 0x1
	.byte	2                               // 0x2
	.byte	4                               // 0x4
	.byte	8                               // 0x8
	.byte	16                              // 0x10
	.byte	32                              // 0x20
	.byte	64                              // 0x40
	.byte	128                             // 0x80
	.section	.text.bl_direct16_pair,"ax",@progbits
	.hidden	bl_direct16_pair
	.globl	bl_direct16_pair
	.p2align	4
	.type	bl_direct16_pair,@function
bl_direct16_pair:                       // @bl_direct16_pair
	.cfi_startproc
// %bb.0:
	ldp	q2, q1, [x0]
	dup	v0.8h, w2
	adrp	x8, .LCPI2_0
	ldr	d4, [x8, :lo12:.LCPI2_0]
	cmhi	v3.8h, v0.8h, v1.8h
	cmhi	v0.8h, v0.8h, v2.8h
	xtn	v3.8b, v3.8h
	xtn	v0.8b, v0.8h
	bic	v3.8b, v4.8b, v3.8b
	bic	v0.8b, v4.8b, v0.8b
	addv	b3, v3.8b
	addv	b0, v0.8b
	fmov	w8, s3
	fmov	w9, s0
	dup	v0.8h, w1
	bfi	w9, w8, #8, #8
	cmhi	v1.8h, v1.8h, v0.8h
	cmhi	v0.8h, v2.8h, v0.8h
	xtn	v1.8b, v1.8h
	xtn	v0.8b, v0.8h
	and	v1.8b, v1.8b, v4.8b
	and	v0.8b, v0.8b, v4.8b
	addv	b1, v1.8b
	addv	b0, v0.8b
	fmov	w8, s1
	fmov	w10, s0
	bfi	w10, w8, #8, #8
	orr	x0, x10, x9, lsl #32
	ret
.Lfunc_end2:
	.size	bl_direct16_pair, .Lfunc_end2-bl_direct16_pair
	.cfi_endproc
                                        // -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          // -- Begin function bl_checkpoint16_find
.LCPI3_0:
	.byte	1                               // 0x1
	.byte	2                               // 0x2
	.byte	4                               // 0x4
	.byte	8                               // 0x8
	.byte	16                              // 0x10
	.byte	32                              // 0x20
	.byte	64                              // 0x40
	.byte	128                             // 0x80
	.section	.text.bl_checkpoint16_find,"ax",@progbits
	.hidden	bl_checkpoint16_find
	.globl	bl_checkpoint16_find
	.p2align	4
	.type	bl_checkpoint16_find,@function
bl_checkpoint16_find:                   // @bl_checkpoint16_find
	.cfi_startproc
// %bb.0:
	ldp	q2, q1, [x0]
	dup	v0.8h, w1
	adrp	x8, .LCPI3_0
	ldr	d3, [x8, :lo12:.LCPI3_0]
	cmhi	v1.8h, v0.8h, v1.8h
	cmhi	v0.8h, v0.8h, v2.8h
	xtn	v1.8b, v1.8h
	xtn	v0.8b, v0.8h
	bic	v1.8b, v3.8b, v1.8b
	bic	v0.8b, v3.8b, v0.8b
	addv	b1, v1.8b
	addv	b0, v0.8b
	fmov	w8, s1
	fmov	w9, s0
	bfi	w9, w8, #8, #8
	rbit	w8, w9
	cmp	w9, #0
	mov	w9, #16                         // =0x10
	clz	w8, w8
	csel	w0, w9, w8, eq
	ret
.Lfunc_end3:
	.size	bl_checkpoint16_find, .Lfunc_end3-bl_checkpoint16_find
	.cfi_endproc
                                        // -- End function
	.section	.text.bl_byte_owner16,"ax",@progbits
	.hidden	bl_byte_owner16                 // -- Begin function bl_byte_owner16
	.globl	bl_byte_owner16
	.p2align	4
	.type	bl_byte_owner16,@function
bl_byte_owner16:                        // @bl_byte_owner16
	.cfi_startproc
// %bb.0:
	ldr	q0, [x0]
	movi	v3.2d, #0000000000000000
	ushll	v1.8h, v0.8b, #0
	ushll2	v2.8h, v0.16b, #0
	ext	v1.16b, v3.16b, v1.16b, #14
	ext	v2.16b, v3.16b, v2.16b, #14
	uaddw	v1.8h, v1.8h, v0.8b
	uaddw2	v0.8h, v2.8h, v0.16b
	ext	v2.16b, v3.16b, v1.16b, #12
	add	v1.8h, v2.8h, v1.8h
	ext	v2.16b, v3.16b, v0.16b, #12
	add	v0.8h, v2.8h, v0.8h
	ext	v2.16b, v3.16b, v1.16b, #8
	add	v1.8h, v2.8h, v1.8h
	ext	v2.16b, v3.16b, v0.16b, #8
	movi	v3.8h, #1
	add	v0.8h, v2.8h, v0.8h
	dup	v2.8h, v1.h[7]
	add	v0.8h, v0.8h, v2.8h
	dup	v2.8h, w1
	cmhs	v1.8h, v2.8h, v1.8h
	cmhs	v0.8h, v2.8h, v0.8h
	and	v1.16b, v1.16b, v3.16b
	sub	v0.8h, v1.8h, v0.8h
	addv	h0, v0.8h
	fmov	w0, s0
	ret
.Lfunc_end4:
	.size	bl_byte_owner16, .Lfunc_end4-bl_byte_owner16
	.cfi_endproc
                                        // -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          // -- Begin function bl_byte_owner32
.LCPI5_0:
	.word	1                               // 0x1
	.word	2                               // 0x2
	.word	4                               // 0x4
	.word	8                               // 0x8
	.section	.text.bl_byte_owner32,"ax",@progbits
	.hidden	bl_byte_owner32
	.globl	bl_byte_owner32
	.p2align	4
	.type	bl_byte_owner32,@function
bl_byte_owner32:                        // @bl_byte_owner32
	.cfi_startproc
// %bb.0:
	ldp	s0, s3, [x0]
	adrp	x8, .LCPI5_0
	ushll	v3.8h, v3.8b, #0
	ushll	v1.8h, v0.8b, #0
	movi	v0.2d, #0000000000000000
	ushll	v2.4s, v1.4h, #0
	ext	v2.16b, v0.16b, v2.16b, #12
	uaddw	v1.4s, v2.4s, v1.4h
	ext	v2.16b, v0.16b, v1.16b, #8
	add	v4.4s, v2.4s, v1.4s
	ldr	q2, [x8, :lo12:.LCPI5_0]
	dup	v1.4s, w1
	cmhi	v5.4s, v4.4s, v1.4s
	and	v5.16b, v5.16b, v2.16b
	addv	s5, v5.4s
	fmov	w8, s5
	ushll	v5.4s, v3.4h, #0
	ext	v5.16b, v0.16b, v5.16b, #12
	uaddw	v3.4s, v5.4s, v3.4h
	ext	v5.16b, v0.16b, v3.16b, #8
	add	v3.4s, v5.4s, v3.4s
	dup	v5.4s, v4.s[3]
	add	v5.4s, v3.4s, v5.4s
	add	v3.4s, v3.4s, v4.4s
	cmhi	v5.4s, v5.4s, v1.4s
	and	v5.16b, v5.16b, v2.16b
	addv	s5, v5.4s
	fmov	w9, s5
	ldp	s4, s5, [x0, #8]
	orr	w8, w8, w9, lsl #4
	ushll	v4.8h, v4.8b, #0
	ushll	v6.4s, v4.4h, #0
	ext	v6.16b, v0.16b, v6.16b, #12
	uaddw	v4.4s, v6.4s, v4.4h
	ext	v6.16b, v0.16b, v4.16b, #8
	add	v4.4s, v6.4s, v4.4s
	dup	v6.4s, v3.s[3]
	add	v6.4s, v6.4s, v4.4s
	add	v3.4s, v4.4s, v3.4s
	ushll	v4.8h, v5.8b, #0
	ushll	v5.4s, v4.4h, #0
	dup	v3.4s, v3.s[3]
	cmhi	v6.4s, v6.4s, v1.4s
	ext	v5.16b, v0.16b, v5.16b, #12
	and	v6.16b, v6.16b, v2.16b
	uaddw	v4.4s, v5.4s, v4.4h
	addv	s6, v6.4s
	ext	v0.16b, v0.16b, v4.16b, #8
	add	v0.4s, v0.4s, v4.4s
	fmov	w10, s6
	orr	w8, w8, w10, lsl #8
	add	v0.4s, v0.4s, v3.4s
	cmhi	v0.4s, v0.4s, v1.4s
	and	v0.16b, v0.16b, v2.16b
	addv	s0, v0.4s
	fmov	w9, s0
	orr	w8, w8, w9, lsl #12
	rbit	w9, w8
	cmp	w8, #0
	mov	w8, #16                         // =0x10
	clz	w9, w9
	csel	w0, w8, w9, eq
	ret
.Lfunc_end5:
	.size	bl_byte_owner32, .Lfunc_end5-bl_byte_owner32
	.cfi_endproc
                                        // -- End function
	.section	.text.bl_checkpoint16_select,"ax",@progbits
	.hidden	bl_checkpoint16_select          // -- Begin function bl_checkpoint16_select
	.globl	bl_checkpoint16_select
	.p2align	4
	.type	bl_checkpoint16_select,@function
bl_checkpoint16_select:                 // @bl_checkpoint16_select
	.cfi_startproc
// %bb.0:
	cbz	w1, .LBB6_3
// %bb.1:
	cmp	w1, #1
	b.ne	.LBB6_4
// %bb.2:
	mov	w8, wzr
	and	w9, w2, #0xffff
	b	.LBB6_6
.LBB6_3:
	mov	w0, w1
	ret
.LBB6_4:
	and	w9, w2, #0xffff
	mov	w8, wzr
	mov	w10, w1
	.p2align	5, , 16
.LBB6_5:                                // =>This Inner Loop Header: Depth=1
	lsr	w11, w10, #1
	add	w12, w11, w8
	sub	w10, w10, w11
	add	w13, w12, #1
	cmp	w13, w1
	csinc	w13, w1, w12, hs
	sub	w13, w13, #1
	ldrh	w13, [x0, w13, uxtw #1]
	//APP
	cmp	x13, x9
	csel	w8, w12, w8, lo
	//NO_APP
	cmp	w10, #1
	b.hi	.LBB6_5
.LBB6_6:
	add	w10, w8, #1
	cmp	w10, w1
	csinc	w11, w1, w8, hs
	sub	w11, w11, #1
	ldrh	w11, [x0, w11, uxtw #1]
	//APP
	cmp	x11, x9
	csel	w8, w10, w8, lo
	//NO_APP
	mov	w0, w8
	ret
.Lfunc_end6:
	.size	bl_checkpoint16_select, .Lfunc_end6-bl_checkpoint16_select
	.cfi_endproc
                                        // -- End function
	.section	.text.bl_scan256_count16,"ax",@progbits
	.hidden	bl_scan256_count16              // -- Begin function bl_scan256_count16
	.globl	bl_scan256_count16
	.p2align	4
	.type	bl_scan256_count16,@function
bl_scan256_count16:                     // @bl_scan256_count16
	.cfi_startproc
// %bb.0:
	ldp	q3, q1, [x0, #32]
	dup	v0.8h, w1
	movi	v2.8h, #1
	cmhi	v1.8h, v0.8h, v1.8h
	ldp	q4, q5, [x0, #96]
	cmhi	v5.8h, v0.8h, v5.8h
	and	v1.16b, v1.16b, v2.16b
	cmhi	v3.8h, v0.8h, v3.8h
	cmhi	v4.8h, v0.8h, v4.8h
	sub	v1.8h, v1.8h, v5.8h
	ldr	q5, [x0, #176]
	and	v3.16b, v3.16b, v2.16b
	sub	v3.8h, v3.8h, v4.8h
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v1.8h, v1.8h, v5.8h
	ldp	q4, q5, [x0, #224]
	cmhi	v4.8h, v0.8h, v4.8h
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v1.8h, v1.8h, v5.8h
	ldr	q5, [x0, #304]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v1.8h, v1.8h, v5.8h
	ldr	q5, [x0, #368]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v1.8h, v1.8h, v5.8h
	ldr	q5, [x0, #432]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v1.8h, v1.8h, v5.8h
	ldr	q5, [x0, #496]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v1.8h, v1.8h, v5.8h
	ldr	q5, [x0, #160]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v3.8h, v3.8h, v5.8h
	ldr	q5, [x0, #288]
	sub	v3.8h, v3.8h, v4.8h
	ldr	q4, [x0, #352]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v3.8h, v3.8h, v5.8h
	ldr	q5, [x0, #416]
	cmhi	v4.8h, v0.8h, v4.8h
	sub	v3.8h, v3.8h, v4.8h
	ldr	q4, [x0, #480]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v3.8h, v3.8h, v5.8h
	ldr	q5, [x0, #16]
	cmhi	v4.8h, v0.8h, v4.8h
	sub	v3.8h, v3.8h, v4.8h
	ldr	q4, [x0, #80]
	add	v1.8h, v3.8h, v1.8h
	cmhi	v5.8h, v0.8h, v5.8h
	and	v5.16b, v5.16b, v2.16b
	cmhi	v4.8h, v0.8h, v4.8h
	sub	v4.8h, v5.8h, v4.8h
	ldr	q5, [x0, #144]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v4.8h, v4.8h, v5.8h
	ldr	q5, [x0, #208]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v4.8h, v4.8h, v5.8h
	ldr	q5, [x0, #272]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v4.8h, v4.8h, v5.8h
	ldr	q5, [x0, #336]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v4.8h, v4.8h, v5.8h
	ldr	q5, [x0, #400]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v4.8h, v4.8h, v5.8h
	ldr	q5, [x0, #464]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v4.8h, v4.8h, v5.8h
	ldr	q5, [x0]
	cmhi	v5.8h, v0.8h, v5.8h
	and	v2.16b, v5.16b, v2.16b
	ldr	q5, [x0, #64]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v2.8h, v2.8h, v5.8h
	ldr	q5, [x0, #128]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v2.8h, v2.8h, v5.8h
	ldr	q5, [x0, #192]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v2.8h, v2.8h, v5.8h
	ldr	q5, [x0, #256]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v2.8h, v2.8h, v5.8h
	ldr	q5, [x0, #320]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v2.8h, v2.8h, v5.8h
	ldr	q5, [x0, #384]
	cmhi	v5.8h, v0.8h, v5.8h
	sub	v2.8h, v2.8h, v5.8h
	ldr	q5, [x0, #448]
	cmhi	v0.8h, v0.8h, v5.8h
	sub	v0.8h, v2.8h, v0.8h
	add	v0.8h, v4.8h, v0.8h
	add	v0.8h, v0.8h, v1.8h
	addv	h0, v0.8h
	fmov	w0, s0
	ret
.Lfunc_end7:
	.size	bl_scan256_count16, .Lfunc_end7-bl_scan256_count16
	.cfi_endproc
                                        // -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221033104+2078da43e25a-1~exp1~20251221153121.57)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
