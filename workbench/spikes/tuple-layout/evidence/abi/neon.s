	.file	"abi.cpp"
	.text
	.globl	scalar8                         // -- Begin function scalar8
	.p2align	2
	.type	scalar8,@function
scalar8:                                // @scalar8
	.cfi_startproc
// %bb.0:
	ldr	x0, [x0]
	ret
.Lfunc_end0:
	.size	scalar8, .Lfunc_end0-scalar8
	.cfi_endproc
                                        // -- End function
	.globl	aapcs_quad                      // -- Begin function aapcs_quad
	.p2align	2
	.type	aapcs_quad,@function
aapcs_quad:                             // @aapcs_quad
	.cfi_startproc
// %bb.0:
	ldp	q0, q1, [x0]
	ldp	q2, q3, [x0, #32]
	ret
.Lfunc_end1:
	.size	aapcs_quad, .Lfunc_end1-aapcs_quad
	.cfi_endproc
                                        // -- End function
	.globl	consume_aapcs                   // -- Begin function consume_aapcs
	.p2align	2
	.type	consume_aapcs,@function
consume_aapcs:                          // @consume_aapcs
	.cfi_startproc
// %bb.0:
	stp	x29, x30, [sp, #-16]!           // 16-byte Folded Spill
	.cfi_def_cfa_offset 16
	mov	x29, sp
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	mov	x8, x0
	mov	x0, x1
	blr	x8
	.cfi_def_cfa wsp, 16
	ldp	x29, x30, [sp], #16             // 16-byte Folded Reload
	.cfi_def_cfa_offset 0
	.cfi_restore w30
	.cfi_restore w29
	b	take_quad
.Lfunc_end2:
	.size	consume_aapcs, .Lfunc_end2-consume_aapcs
	.cfi_endproc
                                        // -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221033104+2078da43e25a-1~exp1~20251221153121.57)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
