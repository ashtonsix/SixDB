	.file	"contiguous_codegen.cpp"
	.text
	.globl	contiguous_selected_l1          // -- Begin function contiguous_selected_l1
	.p2align	4
	.type	contiguous_selected_l1,@function
contiguous_selected_l1:                 // @contiguous_selected_l1
	.cfi_startproc
// %bb.0:
	ldrb	w9, [x0, #10]
	ldrh	w8, [x0, #8]
	adrp	x10, .LJTI0_0
	add	x10, x10, :lo12:.LJTI0_0
.Ltmp0:
	adr	x11, .Ltmp0
	ldrsw	x12, [x10, x9, lsl #2]
	add	x11, x11, x12
	br	x11
.LBB0_1:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_2:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, #640]
	prfm	pldl1keep, [x1, #768]
	prfm	pldl1keep, [x1, #896]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
.LBB0_3:
	ret
.LBB0_4:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, #640]
	prfm	pldl1keep, [x1, #768]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_5:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_6:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_7:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_8:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_9:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_10:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_11:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #320]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #448]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_12:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_13:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #320]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_14:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, #640]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_15:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #320]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_16:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB0_17:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.Lfunc_end0:
	.size	contiguous_selected_l1, .Lfunc_end0-contiguous_selected_l1
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI0_0:
	.word	.LBB0_1-.Ltmp0
	.word	.LBB0_12-.Ltmp0
	.word	.LBB0_9-.Ltmp0
	.word	.LBB0_10-.Ltmp0
	.word	.LBB0_6-.Ltmp0
	.word	.LBB0_13-.Ltmp0
	.word	.LBB0_15-.Ltmp0
	.word	.LBB0_11-.Ltmp0
	.word	.LBB0_17-.Ltmp0
	.word	.LBB0_8-.Ltmp0
	.word	.LBB0_16-.Ltmp0
	.word	.LBB0_5-.Ltmp0
	.word	.LBB0_7-.Ltmp0
	.word	.LBB0_14-.Ltmp0
	.word	.LBB0_4-.Ltmp0
	.word	.LBB0_2-.Ltmp0
	.word	.LBB0_3-.Ltmp0
                                        // -- End function
	.text
	.globl	contiguous_selected_l2          // -- Begin function contiguous_selected_l2
	.p2align	4
	.type	contiguous_selected_l2,@function
contiguous_selected_l2:                 // @contiguous_selected_l2
	.cfi_startproc
// %bb.0:
	ldrb	w9, [x0, #10]
	ldrh	w8, [x0, #8]
	adrp	x10, .LJTI1_0
	add	x10, x10, :lo12:.LJTI1_0
.Ltmp1:
	adr	x11, .Ltmp1
	ldrsw	x12, [x10, x9, lsl #2]
	add	x11, x11, x12
	br	x11
.LBB1_1:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_2:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, #640]
	prfm	pldl2keep, [x1, #768]
	prfm	pldl2keep, [x1, #896]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
.LBB1_3:
	ret
.LBB1_4:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, #640]
	prfm	pldl2keep, [x1, #768]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_5:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_6:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_7:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_8:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_9:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_10:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_11:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #320]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #448]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_12:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_13:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #320]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_14:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, #640]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_15:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #320]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_16:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB1_17:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.Lfunc_end1:
	.size	contiguous_selected_l2, .Lfunc_end1-contiguous_selected_l2
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI1_0:
	.word	.LBB1_1-.Ltmp1
	.word	.LBB1_12-.Ltmp1
	.word	.LBB1_9-.Ltmp1
	.word	.LBB1_10-.Ltmp1
	.word	.LBB1_6-.Ltmp1
	.word	.LBB1_13-.Ltmp1
	.word	.LBB1_15-.Ltmp1
	.word	.LBB1_11-.Ltmp1
	.word	.LBB1_17-.Ltmp1
	.word	.LBB1_8-.Ltmp1
	.word	.LBB1_16-.Ltmp1
	.word	.LBB1_5-.Ltmp1
	.word	.LBB1_7-.Ltmp1
	.word	.LBB1_14-.Ltmp1
	.word	.LBB1_4-.Ltmp1
	.word	.LBB1_2-.Ltmp1
	.word	.LBB1_3-.Ltmp1
                                        // -- End function
	.text
	.globl	contiguous_unique_selected_l1   // -- Begin function contiguous_unique_selected_l1
	.p2align	4
	.type	contiguous_unique_selected_l1,@function
contiguous_unique_selected_l1:          // @contiguous_unique_selected_l1
	.cfi_startproc
// %bb.0:
	ldrb	w8, [x0, #10]
	mov	w11, #6                         // =0x6
	bfxil	x11, x8, #3, #1
	and	w9, w8, #0xff
	ubfx	x10, x8, #3, #1
	mov	w8, #1                          // =0x1
	lsl	w10, w10, #3
	cmp	w9, #16
	mov	w9, #16                         // =0x10
	lsl	x12, x8, x11
	sub	x8, x12, #1
	and	x13, x8, x1
	ldrh	w8, [x0, #8]
	add	x14, x13, x8
	lsr	x11, x14, x11
	add	x10, x11, x10
	sub	x10, x10, x11, lsr #3
	adrp	x11, .LJTI2_0
	add	x11, x11, :lo12:.LJTI2_0
	csel	x10, x9, x10, eq
	sub	x9, x12, x13
.Ltmp2:
	adr	x12, .Ltmp2
	ldrsw	x13, [x11, x10, lsl #2]
	add	x12, x12, x13
	br	x12
.LBB2_1:
	prfm	pldl1keep, [x1]
	//APP
	//NO_APP
	ret
.LBB2_2:
	add	x9, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x9]
	prfm	pldl1keep, [x9, #128]
	prfm	pldl1keep, [x9, #256]
	prfm	pldl1keep, [x9, #384]
	prfm	pldl1keep, [x9, #512]
	prfm	pldl1keep, [x9, #640]
	prfm	pldl1keep, [x9, #768]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
.LBB2_3:
	ret
.LBB2_4:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	prfm	pldl1keep, [x8, #512]
	prfm	pldl1keep, [x8, #640]
	//APP
	//NO_APP
	ret
.LBB2_5:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	//APP
	//NO_APP
	ret
.LBB2_6:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	//APP
	//NO_APP
	ret
.LBB2_7:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	//APP
	//NO_APP
	ret
.LBB2_8:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, x9]
	//APP
	//NO_APP
	ret
.LBB2_9:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	//APP
	//NO_APP
	ret
.LBB2_10:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	//APP
	//NO_APP
	ret
.LBB2_11:
	add	x9, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x9]
	prfm	pldl1keep, [x9, #64]
	prfm	pldl1keep, [x9, #128]
	prfm	pldl1keep, [x9, #192]
	prfm	pldl1keep, [x9, #256]
	prfm	pldl1keep, [x9, #320]
	prfm	pldl1keep, [x9, #384]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB2_12:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, x9]
	//APP
	//NO_APP
	ret
.LBB2_13:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	prfm	pldl1keep, [x8, #256]
	//APP
	//NO_APP
	ret
.LBB2_14:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	prfm	pldl1keep, [x8, #512]
	//APP
	//NO_APP
	ret
.LBB2_15:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #320]
	//APP
	//NO_APP
	ret
.LBB2_16:
	add	x8, x1, x9
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	//APP
	//NO_APP
	ret
.LBB2_17:
	prfm	pldl1keep, [x1]
	//APP
	//NO_APP
	ret
.Lfunc_end2:
	.size	contiguous_unique_selected_l1, .Lfunc_end2-contiguous_unique_selected_l1
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI2_0:
	.word	.LBB2_1-.Ltmp2
	.word	.LBB2_12-.Ltmp2
	.word	.LBB2_9-.Ltmp2
	.word	.LBB2_10-.Ltmp2
	.word	.LBB2_6-.Ltmp2
	.word	.LBB2_13-.Ltmp2
	.word	.LBB2_15-.Ltmp2
	.word	.LBB2_11-.Ltmp2
	.word	.LBB2_17-.Ltmp2
	.word	.LBB2_8-.Ltmp2
	.word	.LBB2_16-.Ltmp2
	.word	.LBB2_5-.Ltmp2
	.word	.LBB2_7-.Ltmp2
	.word	.LBB2_14-.Ltmp2
	.word	.LBB2_4-.Ltmp2
	.word	.LBB2_2-.Ltmp2
	.word	.LBB2_3-.Ltmp2
                                        // -- End function
	.text
	.globl	contiguous_unique_selected_l2   // -- Begin function contiguous_unique_selected_l2
	.p2align	4
	.type	contiguous_unique_selected_l2,@function
contiguous_unique_selected_l2:          // @contiguous_unique_selected_l2
	.cfi_startproc
// %bb.0:
	ldrb	w8, [x0, #10]
	mov	w11, #6                         // =0x6
	bfxil	x11, x8, #3, #1
	and	w9, w8, #0xff
	ubfx	x10, x8, #3, #1
	mov	w8, #1                          // =0x1
	lsl	w10, w10, #3
	cmp	w9, #16
	mov	w9, #16                         // =0x10
	lsl	x12, x8, x11
	sub	x8, x12, #1
	and	x13, x8, x1
	ldrh	w8, [x0, #8]
	add	x14, x13, x8
	lsr	x11, x14, x11
	add	x10, x11, x10
	sub	x10, x10, x11, lsr #3
	adrp	x11, .LJTI3_0
	add	x11, x11, :lo12:.LJTI3_0
	csel	x10, x9, x10, eq
	sub	x9, x12, x13
.Ltmp3:
	adr	x12, .Ltmp3
	ldrsw	x13, [x11, x10, lsl #2]
	add	x12, x12, x13
	br	x12
.LBB3_1:
	prfm	pldl2keep, [x1]
	//APP
	//NO_APP
	ret
.LBB3_2:
	add	x9, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x9]
	prfm	pldl2keep, [x9, #128]
	prfm	pldl2keep, [x9, #256]
	prfm	pldl2keep, [x9, #384]
	prfm	pldl2keep, [x9, #512]
	prfm	pldl2keep, [x9, #640]
	prfm	pldl2keep, [x9, #768]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
.LBB3_3:
	ret
.LBB3_4:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #256]
	prfm	pldl2keep, [x8, #384]
	prfm	pldl2keep, [x8, #512]
	prfm	pldl2keep, [x8, #640]
	//APP
	//NO_APP
	ret
.LBB3_5:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #256]
	//APP
	//NO_APP
	ret
.LBB3_6:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #64]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #192]
	//APP
	//NO_APP
	ret
.LBB3_7:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #256]
	prfm	pldl2keep, [x8, #384]
	//APP
	//NO_APP
	ret
.LBB3_8:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, x9]
	//APP
	//NO_APP
	ret
.LBB3_9:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #64]
	//APP
	//NO_APP
	ret
.LBB3_10:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #64]
	prfm	pldl2keep, [x8, #128]
	//APP
	//NO_APP
	ret
.LBB3_11:
	add	x9, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x9]
	prfm	pldl2keep, [x9, #64]
	prfm	pldl2keep, [x9, #128]
	prfm	pldl2keep, [x9, #192]
	prfm	pldl2keep, [x9, #256]
	prfm	pldl2keep, [x9, #320]
	prfm	pldl2keep, [x9, #384]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	ret
.LBB3_12:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, x9]
	//APP
	//NO_APP
	ret
.LBB3_13:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #64]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #192]
	prfm	pldl2keep, [x8, #256]
	//APP
	//NO_APP
	ret
.LBB3_14:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #256]
	prfm	pldl2keep, [x8, #384]
	prfm	pldl2keep, [x8, #512]
	//APP
	//NO_APP
	ret
.LBB3_15:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #64]
	prfm	pldl2keep, [x8, #128]
	prfm	pldl2keep, [x8, #192]
	prfm	pldl2keep, [x8, #256]
	prfm	pldl2keep, [x8, #320]
	//APP
	//NO_APP
	ret
.LBB3_16:
	add	x8, x1, x9
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x8]
	prfm	pldl2keep, [x8, #128]
	//APP
	//NO_APP
	ret
.LBB3_17:
	prfm	pldl2keep, [x1]
	//APP
	//NO_APP
	ret
.Lfunc_end3:
	.size	contiguous_unique_selected_l2, .Lfunc_end3-contiguous_unique_selected_l2
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI3_0:
	.word	.LBB3_1-.Ltmp3
	.word	.LBB3_12-.Ltmp3
	.word	.LBB3_9-.Ltmp3
	.word	.LBB3_10-.Ltmp3
	.word	.LBB3_6-.Ltmp3
	.word	.LBB3_13-.Ltmp3
	.word	.LBB3_15-.Ltmp3
	.word	.LBB3_11-.Ltmp3
	.word	.LBB3_17-.Ltmp3
	.word	.LBB3_8-.Ltmp3
	.word	.LBB3_16-.Ltmp3
	.word	.LBB3_5-.Ltmp3
	.word	.LBB3_7-.Ltmp3
	.word	.LBB3_14-.Ltmp3
	.word	.LBB3_4-.Ltmp3
	.word	.LBB3_2-.Ltmp3
	.word	.LBB3_3-.Ltmp3
                                        // -- End function
	.text
	.globl	contiguous_fixed_64_1           // -- Begin function contiguous_fixed_64_1
	.p2align	4
	.type	contiguous_fixed_64_1,@function
contiguous_fixed_64_1:                  // @contiguous_fixed_64_1
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end4:
	.size	contiguous_fixed_64_1, .Lfunc_end4-contiguous_fixed_64_1
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_2           // -- Begin function contiguous_fixed_64_2
	.p2align	4
	.type	contiguous_fixed_64_2,@function
contiguous_fixed_64_2:                  // @contiguous_fixed_64_2
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end5:
	.size	contiguous_fixed_64_2, .Lfunc_end5-contiguous_fixed_64_2
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_3           // -- Begin function contiguous_fixed_64_3
	.p2align	4
	.type	contiguous_fixed_64_3,@function
contiguous_fixed_64_3:                  // @contiguous_fixed_64_3
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end6:
	.size	contiguous_fixed_64_3, .Lfunc_end6-contiguous_fixed_64_3
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_4           // -- Begin function contiguous_fixed_64_4
	.p2align	4
	.type	contiguous_fixed_64_4,@function
contiguous_fixed_64_4:                  // @contiguous_fixed_64_4
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #192]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end7:
	.size	contiguous_fixed_64_4, .Lfunc_end7-contiguous_fixed_64_4
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_5           // -- Begin function contiguous_fixed_64_5
	.p2align	4
	.type	contiguous_fixed_64_5,@function
contiguous_fixed_64_5:                  // @contiguous_fixed_64_5
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #192]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end8:
	.size	contiguous_fixed_64_5, .Lfunc_end8-contiguous_fixed_64_5
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_6           // -- Begin function contiguous_fixed_64_6
	.p2align	4
	.type	contiguous_fixed_64_6,@function
contiguous_fixed_64_6:                  // @contiguous_fixed_64_6
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #192]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #320]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end9:
	.size	contiguous_fixed_64_6, .Lfunc_end9-contiguous_fixed_64_6
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_7           // -- Begin function contiguous_fixed_64_7
	.p2align	4
	.type	contiguous_fixed_64_7,@function
contiguous_fixed_64_7:                  // @contiguous_fixed_64_7
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #192]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #320]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end10:
	.size	contiguous_fixed_64_7, .Lfunc_end10-contiguous_fixed_64_7
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_64_8           // -- Begin function contiguous_fixed_64_8
	.p2align	4
	.type	contiguous_fixed_64_8,@function
contiguous_fixed_64_8:                  // @contiguous_fixed_64_8
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #64]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #192]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #320]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, #448]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end11:
	.size	contiguous_fixed_64_8, .Lfunc_end11-contiguous_fixed_64_8
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_1          // -- Begin function contiguous_fixed_128_1
	.p2align	4
	.type	contiguous_fixed_128_1,@function
contiguous_fixed_128_1:                 // @contiguous_fixed_128_1
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end12:
	.size	contiguous_fixed_128_1, .Lfunc_end12-contiguous_fixed_128_1
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_2          // -- Begin function contiguous_fixed_128_2
	.p2align	4
	.type	contiguous_fixed_128_2,@function
contiguous_fixed_128_2:                 // @contiguous_fixed_128_2
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end13:
	.size	contiguous_fixed_128_2, .Lfunc_end13-contiguous_fixed_128_2
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_3          // -- Begin function contiguous_fixed_128_3
	.p2align	4
	.type	contiguous_fixed_128_3,@function
contiguous_fixed_128_3:                 // @contiguous_fixed_128_3
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end14:
	.size	contiguous_fixed_128_3, .Lfunc_end14-contiguous_fixed_128_3
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_4          // -- Begin function contiguous_fixed_128_4
	.p2align	4
	.type	contiguous_fixed_128_4,@function
contiguous_fixed_128_4:                 // @contiguous_fixed_128_4
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end15:
	.size	contiguous_fixed_128_4, .Lfunc_end15-contiguous_fixed_128_4
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_5          // -- Begin function contiguous_fixed_128_5
	.p2align	4
	.type	contiguous_fixed_128_5,@function
contiguous_fixed_128_5:                 // @contiguous_fixed_128_5
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, #512]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end16:
	.size	contiguous_fixed_128_5, .Lfunc_end16-contiguous_fixed_128_5
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_6          // -- Begin function contiguous_fixed_128_6
	.p2align	4
	.type	contiguous_fixed_128_6,@function
contiguous_fixed_128_6:                 // @contiguous_fixed_128_6
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, #512]
	prfm	pldl1keep, [x0, #640]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end17:
	.size	contiguous_fixed_128_6, .Lfunc_end17-contiguous_fixed_128_6
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_7          // -- Begin function contiguous_fixed_128_7
	.p2align	4
	.type	contiguous_fixed_128_7,@function
contiguous_fixed_128_7:                 // @contiguous_fixed_128_7
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, #512]
	prfm	pldl1keep, [x0, #640]
	prfm	pldl1keep, [x0, #768]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end18:
	.size	contiguous_fixed_128_7, .Lfunc_end18-contiguous_fixed_128_7
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_fixed_128_8          // -- Begin function contiguous_fixed_128_8
	.p2align	4
	.type	contiguous_fixed_128_8,@function
contiguous_fixed_128_8:                 // @contiguous_fixed_128_8
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, #128]
	prfm	pldl1keep, [x0, #256]
	prfm	pldl1keep, [x0, #384]
	prfm	pldl1keep, [x0, #512]
	prfm	pldl1keep, [x0, #640]
	prfm	pldl1keep, [x0, #768]
	prfm	pldl1keep, [x0, #896]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end19:
	.size	contiguous_fixed_128_8, .Lfunc_end19-contiguous_fixed_128_8
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_1    // -- Begin function contiguous_unique_fixed_64_1
	.p2align	4
	.type	contiguous_unique_fixed_64_1,@function
contiguous_unique_fixed_64_1:           // @contiguous_unique_fixed_64_1
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	//APP
	//NO_APP
	ret
.Lfunc_end20:
	.size	contiguous_unique_fixed_64_1, .Lfunc_end20-contiguous_unique_fixed_64_1
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_2    // -- Begin function contiguous_unique_fixed_64_2
	.p2align	4
	.type	contiguous_unique_fixed_64_2,@function
contiguous_unique_fixed_64_2:           // @contiguous_unique_fixed_64_2
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end21:
	.size	contiguous_unique_fixed_64_2, .Lfunc_end21-contiguous_unique_fixed_64_2
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_3    // -- Begin function contiguous_unique_fixed_64_3
	.p2align	4
	.type	contiguous_unique_fixed_64_3,@function
contiguous_unique_fixed_64_3:           // @contiguous_unique_fixed_64_3
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	//APP
	//NO_APP
	ret
.Lfunc_end22:
	.size	contiguous_unique_fixed_64_3, .Lfunc_end22-contiguous_unique_fixed_64_3
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_4    // -- Begin function contiguous_unique_fixed_64_4
	.p2align	4
	.type	contiguous_unique_fixed_64_4,@function
contiguous_unique_fixed_64_4:           // @contiguous_unique_fixed_64_4
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	//APP
	//NO_APP
	ret
.Lfunc_end23:
	.size	contiguous_unique_fixed_64_4, .Lfunc_end23-contiguous_unique_fixed_64_4
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_5    // -- Begin function contiguous_unique_fixed_64_5
	.p2align	4
	.type	contiguous_unique_fixed_64_5,@function
contiguous_unique_fixed_64_5:           // @contiguous_unique_fixed_64_5
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	//APP
	//NO_APP
	ret
.Lfunc_end24:
	.size	contiguous_unique_fixed_64_5, .Lfunc_end24-contiguous_unique_fixed_64_5
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_6    // -- Begin function contiguous_unique_fixed_64_6
	.p2align	4
	.type	contiguous_unique_fixed_64_6,@function
contiguous_unique_fixed_64_6:           // @contiguous_unique_fixed_64_6
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	prfm	pldl1keep, [x8, #256]
	//APP
	//NO_APP
	ret
.Lfunc_end25:
	.size	contiguous_unique_fixed_64_6, .Lfunc_end25-contiguous_unique_fixed_64_6
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_7    // -- Begin function contiguous_unique_fixed_64_7
	.p2align	4
	.type	contiguous_unique_fixed_64_7,@function
contiguous_unique_fixed_64_7:           // @contiguous_unique_fixed_64_7
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #320]
	//APP
	//NO_APP
	ret
.Lfunc_end26:
	.size	contiguous_unique_fixed_64_7, .Lfunc_end26-contiguous_unique_fixed_64_7
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_64_8    // -- Begin function contiguous_unique_fixed_64_8
	.p2align	4
	.type	contiguous_unique_fixed_64_8,@function
contiguous_unique_fixed_64_8:           // @contiguous_unique_fixed_64_8
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #64]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #192]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #320]
	prfm	pldl1keep, [x8, #384]
	prfm	pldl1keep, [x0, x2]
	//APP
	//NO_APP
	ret
.Lfunc_end27:
	.size	contiguous_unique_fixed_64_8, .Lfunc_end27-contiguous_unique_fixed_64_8
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_1   // -- Begin function contiguous_unique_fixed_128_1
	.p2align	4
	.type	contiguous_unique_fixed_128_1,@function
contiguous_unique_fixed_128_1:          // @contiguous_unique_fixed_128_1
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	//APP
	//NO_APP
	ret
.Lfunc_end28:
	.size	contiguous_unique_fixed_128_1, .Lfunc_end28-contiguous_unique_fixed_128_1
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_2   // -- Begin function contiguous_unique_fixed_128_2
	.p2align	4
	.type	contiguous_unique_fixed_128_2,@function
contiguous_unique_fixed_128_2:          // @contiguous_unique_fixed_128_2
	.cfi_startproc
// %bb.0:
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x0, x1]
	//APP
	//NO_APP
	ret
.Lfunc_end29:
	.size	contiguous_unique_fixed_128_2, .Lfunc_end29-contiguous_unique_fixed_128_2
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_3   // -- Begin function contiguous_unique_fixed_128_3
	.p2align	4
	.type	contiguous_unique_fixed_128_3,@function
contiguous_unique_fixed_128_3:          // @contiguous_unique_fixed_128_3
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	//APP
	//NO_APP
	ret
.Lfunc_end30:
	.size	contiguous_unique_fixed_128_3, .Lfunc_end30-contiguous_unique_fixed_128_3
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_4   // -- Begin function contiguous_unique_fixed_128_4
	.p2align	4
	.type	contiguous_unique_fixed_128_4,@function
contiguous_unique_fixed_128_4:          // @contiguous_unique_fixed_128_4
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	//APP
	//NO_APP
	ret
.Lfunc_end31:
	.size	contiguous_unique_fixed_128_4, .Lfunc_end31-contiguous_unique_fixed_128_4
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_5   // -- Begin function contiguous_unique_fixed_128_5
	.p2align	4
	.type	contiguous_unique_fixed_128_5,@function
contiguous_unique_fixed_128_5:          // @contiguous_unique_fixed_128_5
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	//APP
	//NO_APP
	ret
.Lfunc_end32:
	.size	contiguous_unique_fixed_128_5, .Lfunc_end32-contiguous_unique_fixed_128_5
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_6   // -- Begin function contiguous_unique_fixed_128_6
	.p2align	4
	.type	contiguous_unique_fixed_128_6,@function
contiguous_unique_fixed_128_6:          // @contiguous_unique_fixed_128_6
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	prfm	pldl1keep, [x8, #512]
	//APP
	//NO_APP
	ret
.Lfunc_end33:
	.size	contiguous_unique_fixed_128_6, .Lfunc_end33-contiguous_unique_fixed_128_6
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_7   // -- Begin function contiguous_unique_fixed_128_7
	.p2align	4
	.type	contiguous_unique_fixed_128_7,@function
contiguous_unique_fixed_128_7:          // @contiguous_unique_fixed_128_7
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	prfm	pldl1keep, [x8, #512]
	prfm	pldl1keep, [x8, #640]
	//APP
	//NO_APP
	ret
.Lfunc_end34:
	.size	contiguous_unique_fixed_128_7, .Lfunc_end34-contiguous_unique_fixed_128_7
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_unique_fixed_128_8   // -- Begin function contiguous_unique_fixed_128_8
	.p2align	4
	.type	contiguous_unique_fixed_128_8,@function
contiguous_unique_fixed_128_8:          // @contiguous_unique_fixed_128_8
	.cfi_startproc
// %bb.0:
	add	x8, x0, x1
	prfm	pldl1keep, [x0]
	prfm	pldl1keep, [x8]
	prfm	pldl1keep, [x8, #128]
	prfm	pldl1keep, [x8, #256]
	prfm	pldl1keep, [x8, #384]
	prfm	pldl1keep, [x8, #512]
	prfm	pldl1keep, [x8, #640]
	prfm	pldl1keep, [x8, #768]
	prfm	pldl1keep, [x0, x2]
	//APP
	//NO_APP
	ret
.Lfunc_end35:
	.size	contiguous_unique_fixed_128_8, .Lfunc_end35-contiguous_unique_fixed_128_8
	.cfi_endproc
                                        // -- End function
	.globl	contiguous_identity_selected_l1 // -- Begin function contiguous_identity_selected_l1
	.p2align	4
	.type	contiguous_identity_selected_l1,@function
contiguous_identity_selected_l1:        // @contiguous_identity_selected_l1
	.cfi_startproc
// %bb.0:
	ldrb	w9, [x0, #10]
	ldrh	w8, [x0, #8]
	adrp	x10, .LJTI36_0
	add	x10, x10, :lo12:.LJTI36_0
.Ltmp4:
	adr	x11, .Ltmp4
	ldrsw	x12, [x10, x9, lsl #2]
	add	x11, x11, x12
	br	x11
.LBB36_1:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_2:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, #640]
	prfm	pldl1keep, [x1, #768]
	prfm	pldl1keep, [x1, #896]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
.LBB36_3:
	ret
.LBB36_4:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, #640]
	prfm	pldl1keep, [x1, #768]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_5:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_6:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_7:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_8:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_9:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_10:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_11:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #320]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #448]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_12:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_13:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #320]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_14:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, #512]
	prfm	pldl1keep, [x1, #640]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_15:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #64]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #192]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, #320]
	prfm	pldl1keep, [x1, #384]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_16:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, #128]
	prfm	pldl1keep, [x1, #256]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB36_17:
	prfm	pldl1keep, [x1]
	prfm	pldl1keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.Lfunc_end36:
	.size	contiguous_identity_selected_l1, .Lfunc_end36-contiguous_identity_selected_l1
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI36_0:
	.word	.LBB36_1-.Ltmp4
	.word	.LBB36_12-.Ltmp4
	.word	.LBB36_9-.Ltmp4
	.word	.LBB36_10-.Ltmp4
	.word	.LBB36_6-.Ltmp4
	.word	.LBB36_13-.Ltmp4
	.word	.LBB36_15-.Ltmp4
	.word	.LBB36_11-.Ltmp4
	.word	.LBB36_17-.Ltmp4
	.word	.LBB36_8-.Ltmp4
	.word	.LBB36_16-.Ltmp4
	.word	.LBB36_5-.Ltmp4
	.word	.LBB36_7-.Ltmp4
	.word	.LBB36_14-.Ltmp4
	.word	.LBB36_4-.Ltmp4
	.word	.LBB36_2-.Ltmp4
	.word	.LBB36_3-.Ltmp4
                                        // -- End function
	.text
	.globl	contiguous_identity_selected_l2 // -- Begin function contiguous_identity_selected_l2
	.p2align	4
	.type	contiguous_identity_selected_l2,@function
contiguous_identity_selected_l2:        // @contiguous_identity_selected_l2
	.cfi_startproc
// %bb.0:
	ldrb	w9, [x0, #10]
	ldrh	w8, [x0, #8]
	adrp	x10, .LJTI37_0
	add	x10, x10, :lo12:.LJTI37_0
.Ltmp5:
	adr	x11, .Ltmp5
	ldrsw	x12, [x10, x9, lsl #2]
	add	x11, x11, x12
	br	x11
.LBB37_1:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_2:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, #640]
	prfm	pldl2keep, [x1, #768]
	prfm	pldl2keep, [x1, #896]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
.LBB37_3:
	ret
.LBB37_4:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, #640]
	prfm	pldl2keep, [x1, #768]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_5:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_6:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_7:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_8:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_9:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_10:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_11:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #320]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #448]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_12:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_13:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #320]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_14:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, #512]
	prfm	pldl2keep, [x1, #640]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_15:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #64]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #192]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, #320]
	prfm	pldl2keep, [x1, #384]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_16:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, #128]
	prfm	pldl2keep, [x1, #256]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.LBB37_17:
	prfm	pldl2keep, [x1]
	prfm	pldl2keep, [x1, x8]
	//APP
	//NO_APP
	//APP
	//NO_APP
	ret
.Lfunc_end37:
	.size	contiguous_identity_selected_l2, .Lfunc_end37-contiguous_identity_selected_l2
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI37_0:
	.word	.LBB37_1-.Ltmp5
	.word	.LBB37_12-.Ltmp5
	.word	.LBB37_9-.Ltmp5
	.word	.LBB37_10-.Ltmp5
	.word	.LBB37_6-.Ltmp5
	.word	.LBB37_13-.Ltmp5
	.word	.LBB37_15-.Ltmp5
	.word	.LBB37_11-.Ltmp5
	.word	.LBB37_17-.Ltmp5
	.word	.LBB37_8-.Ltmp5
	.word	.LBB37_16-.Ltmp5
	.word	.LBB37_5-.Ltmp5
	.word	.LBB37_7-.Ltmp5
	.word	.LBB37_14-.Ltmp5
	.word	.LBB37_4-.Ltmp5
	.word	.LBB37_2-.Ltmp5
	.word	.LBB37_3-.Ltmp5
                                        // -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221033104+2078da43e25a-1~exp1~20251221153121.57)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
