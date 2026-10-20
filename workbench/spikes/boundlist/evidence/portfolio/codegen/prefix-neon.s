	.file	"prefix.cpp"
	.text
	.globl	boundlist_owner_u16_narrow      // -- Begin function boundlist_owner_u16_narrow
	.p2align	4
	.type	boundlist_owner_u16_narrow,@function
boundlist_owner_u16_narrow:             // @boundlist_owner_u16_narrow
// %bb.0:
	ldp	q0, q1, [x0]
	movi	v2.2d, #0000000000000000
	ext	v3.16b, v2.16b, v0.16b, #14
	add	v0.8h, v3.8h, v0.8h
	ext	v3.16b, v2.16b, v1.16b, #14
	add	v1.8h, v3.8h, v1.8h
	ext	v3.16b, v2.16b, v0.16b, #12
	add	v0.8h, v3.8h, v0.8h
	ext	v3.16b, v2.16b, v1.16b, #12
	add	v1.8h, v3.8h, v1.8h
	ext	v3.16b, v2.16b, v0.16b, #8
	add	v0.8h, v3.8h, v0.8h
	ext	v2.16b, v2.16b, v1.16b, #8
	movi	v3.8h, #1
	add	v1.8h, v2.8h, v1.8h
	dup	v2.8h, v0.h[7]
	add	v1.8h, v1.8h, v2.8h
	dup	v2.8h, w1
	cmhs	v0.8h, v2.8h, v0.8h
	cmhs	v1.8h, v2.8h, v1.8h
	and	v0.16b, v0.16b, v3.16b
	sub	v0.8h, v0.8h, v1.8h
	addv	h0, v0.8h
	fmov	w0, s0
	ret
.Lfunc_end0:
	.size	boundlist_owner_u16_narrow, .Lfunc_end0-boundlist_owner_u16_narrow
                                        // -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221033104+2078da43e25a-1~exp1~20251221153121.57)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
