	.file	"contiguous_codegen.cpp"
	.text
	.globl	contiguous_selected_l1          # -- Begin function contiguous_selected_l1
	.p2align	4
	.type	contiguous_selected_l1,@function
contiguous_selected_l1:                 # @contiguous_selected_l1
	.cfi_startproc
# %bb.0:
	movzbl	10(%rdi), %ecx
	leaq	.LJTI0_0(%rip), %rdx
	movzwl	8(%rdi), %eax
	movslq	(%rdx,%rcx,4), %rcx
	addq	%rdx, %rcx
	jmpq	*%rcx
.LBB0_1:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_16:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	640(%rsi)
	prefetcht0	768(%rsi)
	prefetcht0	896(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
.LBB0_17:
	retq
.LBB0_15:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	640(%rsi)
	prefetcht0	768(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_12:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_5:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_13:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_10:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_3:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_4:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_8:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	320(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	448(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_2:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_6:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	320(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_14:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	640(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_7:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	320(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_11:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB0_9:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.Lfunc_end0:
	.size	contiguous_selected_l1, .Lfunc_end0-contiguous_selected_l1
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI0_0:
	.long	.LBB0_1-.LJTI0_0
	.long	.LBB0_2-.LJTI0_0
	.long	.LBB0_3-.LJTI0_0
	.long	.LBB0_4-.LJTI0_0
	.long	.LBB0_5-.LJTI0_0
	.long	.LBB0_6-.LJTI0_0
	.long	.LBB0_7-.LJTI0_0
	.long	.LBB0_8-.LJTI0_0
	.long	.LBB0_9-.LJTI0_0
	.long	.LBB0_10-.LJTI0_0
	.long	.LBB0_11-.LJTI0_0
	.long	.LBB0_12-.LJTI0_0
	.long	.LBB0_13-.LJTI0_0
	.long	.LBB0_14-.LJTI0_0
	.long	.LBB0_15-.LJTI0_0
	.long	.LBB0_16-.LJTI0_0
	.long	.LBB0_17-.LJTI0_0
                                        # -- End function
	.text
	.globl	contiguous_selected_l2          # -- Begin function contiguous_selected_l2
	.p2align	4
	.type	contiguous_selected_l2,@function
contiguous_selected_l2:                 # @contiguous_selected_l2
	.cfi_startproc
# %bb.0:
	movzbl	10(%rdi), %ecx
	leaq	.LJTI1_0(%rip), %rdx
	movzwl	8(%rdi), %eax
	movslq	(%rdx,%rcx,4), %rcx
	addq	%rdx, %rcx
	jmpq	*%rcx
.LBB1_1:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_16:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	640(%rsi)
	prefetcht1	768(%rsi)
	prefetcht1	896(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
.LBB1_17:
	retq
.LBB1_15:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	640(%rsi)
	prefetcht1	768(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_12:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_5:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_13:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_10:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_3:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_4:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_8:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	320(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	448(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_2:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_6:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	320(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_14:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	640(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_7:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	320(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_11:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB1_9:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.Lfunc_end1:
	.size	contiguous_selected_l2, .Lfunc_end1-contiguous_selected_l2
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI1_0:
	.long	.LBB1_1-.LJTI1_0
	.long	.LBB1_2-.LJTI1_0
	.long	.LBB1_3-.LJTI1_0
	.long	.LBB1_4-.LJTI1_0
	.long	.LBB1_5-.LJTI1_0
	.long	.LBB1_6-.LJTI1_0
	.long	.LBB1_7-.LJTI1_0
	.long	.LBB1_8-.LJTI1_0
	.long	.LBB1_9-.LJTI1_0
	.long	.LBB1_10-.LJTI1_0
	.long	.LBB1_11-.LJTI1_0
	.long	.LBB1_12-.LJTI1_0
	.long	.LBB1_13-.LJTI1_0
	.long	.LBB1_14-.LJTI1_0
	.long	.LBB1_15-.LJTI1_0
	.long	.LBB1_16-.LJTI1_0
	.long	.LBB1_17-.LJTI1_0
                                        # -- End function
	.text
	.globl	contiguous_unique_selected_l1   # -- Begin function contiguous_unique_selected_l1
	.p2align	4
	.type	contiguous_unique_selected_l1,@function
contiguous_unique_selected_l1:          # @contiguous_unique_selected_l1
	.cfi_startproc
# %bb.0:
	movzbl	10(%rdi), %eax
	movl	$259, %ecx                      # imm = 0x103
	bextrl	%ecx, %eax, %edx
	movzwl	8(%rdi), %ecx
	leal	6(%rdx), %r8d
	bzhiq	%r8, %rsi, %r9
	leaq	(%r9,%rcx), %rdi
	shrxq	%r8, %rdi, %rdi
	leaq	(%rdi,%rdx,8), %rdx
	shrq	$3, %rdi
	subq	%rdi, %rdx
	cmpl	$16, %eax
	movl	$1, %eax
	movl	$16, %edi
	shlxq	%r8, %rax, %rax
	cmovneq	%rdx, %rdi
	leaq	.LJTI2_0(%rip), %r8
	movslq	(%r8,%rdi,4), %rdi
	subq	%r9, %rax
	addq	%r8, %rdi
	jmpq	*%rdi
.LBB2_1:
	prefetcht0	(%rsi)
	#APP
	#NO_APP
	retq
.LBB2_16:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	prefetcht0	384(%rsi,%rax)
	prefetcht0	512(%rsi,%rax)
	prefetcht0	640(%rsi,%rax)
	prefetcht0	768(%rsi,%rax)
	prefetcht0	(%rsi,%rcx)
	#APP
	#NO_APP
.LBB2_17:
	retq
.LBB2_15:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	prefetcht0	384(%rsi,%rax)
	prefetcht0	512(%rsi,%rax)
	prefetcht0	640(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_12:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_5:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	64(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	192(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_13:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	prefetcht0	384(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_10:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_3:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	64(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_4:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	64(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_8:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	64(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	192(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	prefetcht0	320(%rsi,%rax)
	prefetcht0	384(%rsi,%rax)
	prefetcht0	(%rsi,%rcx)
	#APP
	#NO_APP
	retq
.LBB2_2:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_6:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	64(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	192(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_14:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	prefetcht0	384(%rsi,%rax)
	prefetcht0	512(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_7:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	64(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	prefetcht0	192(%rsi,%rax)
	prefetcht0	256(%rsi,%rax)
	prefetcht0	320(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_11:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	prefetcht0	128(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB2_9:
	prefetcht0	(%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end2:
	.size	contiguous_unique_selected_l1, .Lfunc_end2-contiguous_unique_selected_l1
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI2_0:
	.long	.LBB2_1-.LJTI2_0
	.long	.LBB2_2-.LJTI2_0
	.long	.LBB2_3-.LJTI2_0
	.long	.LBB2_4-.LJTI2_0
	.long	.LBB2_5-.LJTI2_0
	.long	.LBB2_6-.LJTI2_0
	.long	.LBB2_7-.LJTI2_0
	.long	.LBB2_8-.LJTI2_0
	.long	.LBB2_9-.LJTI2_0
	.long	.LBB2_10-.LJTI2_0
	.long	.LBB2_11-.LJTI2_0
	.long	.LBB2_12-.LJTI2_0
	.long	.LBB2_13-.LJTI2_0
	.long	.LBB2_14-.LJTI2_0
	.long	.LBB2_15-.LJTI2_0
	.long	.LBB2_16-.LJTI2_0
	.long	.LBB2_17-.LJTI2_0
                                        # -- End function
	.text
	.globl	contiguous_unique_selected_l2   # -- Begin function contiguous_unique_selected_l2
	.p2align	4
	.type	contiguous_unique_selected_l2,@function
contiguous_unique_selected_l2:          # @contiguous_unique_selected_l2
	.cfi_startproc
# %bb.0:
	movzbl	10(%rdi), %eax
	movl	$259, %ecx                      # imm = 0x103
	bextrl	%ecx, %eax, %edx
	movzwl	8(%rdi), %ecx
	leal	6(%rdx), %r8d
	bzhiq	%r8, %rsi, %r9
	leaq	(%r9,%rcx), %rdi
	shrxq	%r8, %rdi, %rdi
	leaq	(%rdi,%rdx,8), %rdx
	shrq	$3, %rdi
	subq	%rdi, %rdx
	cmpl	$16, %eax
	movl	$1, %eax
	movl	$16, %edi
	shlxq	%r8, %rax, %rax
	cmovneq	%rdx, %rdi
	leaq	.LJTI3_0(%rip), %r8
	movslq	(%r8,%rdi,4), %rdi
	subq	%r9, %rax
	addq	%r8, %rdi
	jmpq	*%rdi
.LBB3_1:
	prefetcht1	(%rsi)
	#APP
	#NO_APP
	retq
.LBB3_16:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	prefetcht1	384(%rsi,%rax)
	prefetcht1	512(%rsi,%rax)
	prefetcht1	640(%rsi,%rax)
	prefetcht1	768(%rsi,%rax)
	prefetcht1	(%rsi,%rcx)
	#APP
	#NO_APP
.LBB3_17:
	retq
.LBB3_15:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	prefetcht1	384(%rsi,%rax)
	prefetcht1	512(%rsi,%rax)
	prefetcht1	640(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_12:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_5:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	64(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	192(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_13:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	prefetcht1	384(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_10:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_3:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	64(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_4:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	64(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_8:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	64(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	192(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	prefetcht1	320(%rsi,%rax)
	prefetcht1	384(%rsi,%rax)
	prefetcht1	(%rsi,%rcx)
	#APP
	#NO_APP
	retq
.LBB3_2:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_6:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	64(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	192(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_14:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	prefetcht1	384(%rsi,%rax)
	prefetcht1	512(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_7:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	64(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	prefetcht1	192(%rsi,%rax)
	prefetcht1	256(%rsi,%rax)
	prefetcht1	320(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_11:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	prefetcht1	128(%rsi,%rax)
	#APP
	#NO_APP
	retq
.LBB3_9:
	prefetcht1	(%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end3:
	.size	contiguous_unique_selected_l2, .Lfunc_end3-contiguous_unique_selected_l2
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI3_0:
	.long	.LBB3_1-.LJTI3_0
	.long	.LBB3_2-.LJTI3_0
	.long	.LBB3_3-.LJTI3_0
	.long	.LBB3_4-.LJTI3_0
	.long	.LBB3_5-.LJTI3_0
	.long	.LBB3_6-.LJTI3_0
	.long	.LBB3_7-.LJTI3_0
	.long	.LBB3_8-.LJTI3_0
	.long	.LBB3_9-.LJTI3_0
	.long	.LBB3_10-.LJTI3_0
	.long	.LBB3_11-.LJTI3_0
	.long	.LBB3_12-.LJTI3_0
	.long	.LBB3_13-.LJTI3_0
	.long	.LBB3_14-.LJTI3_0
	.long	.LBB3_15-.LJTI3_0
	.long	.LBB3_16-.LJTI3_0
	.long	.LBB3_17-.LJTI3_0
                                        # -- End function
	.text
	.globl	contiguous_fixed_64_1           # -- Begin function contiguous_fixed_64_1
	.p2align	4
	.type	contiguous_fixed_64_1,@function
contiguous_fixed_64_1:                  # @contiguous_fixed_64_1
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end4:
	.size	contiguous_fixed_64_1, .Lfunc_end4-contiguous_fixed_64_1
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_2           # -- Begin function contiguous_fixed_64_2
	.p2align	4
	.type	contiguous_fixed_64_2,@function
contiguous_fixed_64_2:                  # @contiguous_fixed_64_2
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end5:
	.size	contiguous_fixed_64_2, .Lfunc_end5-contiguous_fixed_64_2
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_3           # -- Begin function contiguous_fixed_64_3
	.p2align	4
	.type	contiguous_fixed_64_3,@function
contiguous_fixed_64_3:                  # @contiguous_fixed_64_3
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end6:
	.size	contiguous_fixed_64_3, .Lfunc_end6-contiguous_fixed_64_3
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_4           # -- Begin function contiguous_fixed_64_4
	.p2align	4
	.type	contiguous_fixed_64_4,@function
contiguous_fixed_64_4:                  # @contiguous_fixed_64_4
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	192(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end7:
	.size	contiguous_fixed_64_4, .Lfunc_end7-contiguous_fixed_64_4
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_5           # -- Begin function contiguous_fixed_64_5
	.p2align	4
	.type	contiguous_fixed_64_5,@function
contiguous_fixed_64_5:                  # @contiguous_fixed_64_5
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	192(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end8:
	.size	contiguous_fixed_64_5, .Lfunc_end8-contiguous_fixed_64_5
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_6           # -- Begin function contiguous_fixed_64_6
	.p2align	4
	.type	contiguous_fixed_64_6,@function
contiguous_fixed_64_6:                  # @contiguous_fixed_64_6
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	192(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	320(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end9:
	.size	contiguous_fixed_64_6, .Lfunc_end9-contiguous_fixed_64_6
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_7           # -- Begin function contiguous_fixed_64_7
	.p2align	4
	.type	contiguous_fixed_64_7,@function
contiguous_fixed_64_7:                  # @contiguous_fixed_64_7
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	192(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	320(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end10:
	.size	contiguous_fixed_64_7, .Lfunc_end10-contiguous_fixed_64_7
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_64_8           # -- Begin function contiguous_fixed_64_8
	.p2align	4
	.type	contiguous_fixed_64_8,@function
contiguous_fixed_64_8:                  # @contiguous_fixed_64_8
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	64(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	192(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	320(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	448(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end11:
	.size	contiguous_fixed_64_8, .Lfunc_end11-contiguous_fixed_64_8
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_1          # -- Begin function contiguous_fixed_128_1
	.p2align	4
	.type	contiguous_fixed_128_1,@function
contiguous_fixed_128_1:                 # @contiguous_fixed_128_1
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end12:
	.size	contiguous_fixed_128_1, .Lfunc_end12-contiguous_fixed_128_1
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_2          # -- Begin function contiguous_fixed_128_2
	.p2align	4
	.type	contiguous_fixed_128_2,@function
contiguous_fixed_128_2:                 # @contiguous_fixed_128_2
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end13:
	.size	contiguous_fixed_128_2, .Lfunc_end13-contiguous_fixed_128_2
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_3          # -- Begin function contiguous_fixed_128_3
	.p2align	4
	.type	contiguous_fixed_128_3,@function
contiguous_fixed_128_3:                 # @contiguous_fixed_128_3
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end14:
	.size	contiguous_fixed_128_3, .Lfunc_end14-contiguous_fixed_128_3
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_4          # -- Begin function contiguous_fixed_128_4
	.p2align	4
	.type	contiguous_fixed_128_4,@function
contiguous_fixed_128_4:                 # @contiguous_fixed_128_4
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end15:
	.size	contiguous_fixed_128_4, .Lfunc_end15-contiguous_fixed_128_4
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_5          # -- Begin function contiguous_fixed_128_5
	.p2align	4
	.type	contiguous_fixed_128_5,@function
contiguous_fixed_128_5:                 # @contiguous_fixed_128_5
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	512(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end16:
	.size	contiguous_fixed_128_5, .Lfunc_end16-contiguous_fixed_128_5
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_6          # -- Begin function contiguous_fixed_128_6
	.p2align	4
	.type	contiguous_fixed_128_6,@function
contiguous_fixed_128_6:                 # @contiguous_fixed_128_6
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	512(%rdi)
	prefetcht0	640(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end17:
	.size	contiguous_fixed_128_6, .Lfunc_end17-contiguous_fixed_128_6
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_7          # -- Begin function contiguous_fixed_128_7
	.p2align	4
	.type	contiguous_fixed_128_7,@function
contiguous_fixed_128_7:                 # @contiguous_fixed_128_7
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	512(%rdi)
	prefetcht0	640(%rdi)
	prefetcht0	768(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end18:
	.size	contiguous_fixed_128_7, .Lfunc_end18-contiguous_fixed_128_7
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_fixed_128_8          # -- Begin function contiguous_fixed_128_8
	.p2align	4
	.type	contiguous_fixed_128_8,@function
contiguous_fixed_128_8:                 # @contiguous_fixed_128_8
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	128(%rdi)
	prefetcht0	256(%rdi)
	prefetcht0	384(%rdi)
	prefetcht0	512(%rdi)
	prefetcht0	640(%rdi)
	prefetcht0	768(%rdi)
	prefetcht0	896(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end19:
	.size	contiguous_fixed_128_8, .Lfunc_end19-contiguous_fixed_128_8
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_1    # -- Begin function contiguous_unique_fixed_64_1
	.p2align	4
	.type	contiguous_unique_fixed_64_1,@function
contiguous_unique_fixed_64_1:           # @contiguous_unique_fixed_64_1
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	#APP
	#NO_APP
	retq
.Lfunc_end20:
	.size	contiguous_unique_fixed_64_1, .Lfunc_end20-contiguous_unique_fixed_64_1
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_2    # -- Begin function contiguous_unique_fixed_64_2
	.p2align	4
	.type	contiguous_unique_fixed_64_2,@function
contiguous_unique_fixed_64_2:           # @contiguous_unique_fixed_64_2
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end21:
	.size	contiguous_unique_fixed_64_2, .Lfunc_end21-contiguous_unique_fixed_64_2
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_3    # -- Begin function contiguous_unique_fixed_64_3
	.p2align	4
	.type	contiguous_unique_fixed_64_3,@function
contiguous_unique_fixed_64_3:           # @contiguous_unique_fixed_64_3
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	64(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end22:
	.size	contiguous_unique_fixed_64_3, .Lfunc_end22-contiguous_unique_fixed_64_3
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_4    # -- Begin function contiguous_unique_fixed_64_4
	.p2align	4
	.type	contiguous_unique_fixed_64_4,@function
contiguous_unique_fixed_64_4:           # @contiguous_unique_fixed_64_4
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	64(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end23:
	.size	contiguous_unique_fixed_64_4, .Lfunc_end23-contiguous_unique_fixed_64_4
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_5    # -- Begin function contiguous_unique_fixed_64_5
	.p2align	4
	.type	contiguous_unique_fixed_64_5,@function
contiguous_unique_fixed_64_5:           # @contiguous_unique_fixed_64_5
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	64(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	192(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end24:
	.size	contiguous_unique_fixed_64_5, .Lfunc_end24-contiguous_unique_fixed_64_5
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_6    # -- Begin function contiguous_unique_fixed_64_6
	.p2align	4
	.type	contiguous_unique_fixed_64_6,@function
contiguous_unique_fixed_64_6:           # @contiguous_unique_fixed_64_6
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	64(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	192(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end25:
	.size	contiguous_unique_fixed_64_6, .Lfunc_end25-contiguous_unique_fixed_64_6
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_7    # -- Begin function contiguous_unique_fixed_64_7
	.p2align	4
	.type	contiguous_unique_fixed_64_7,@function
contiguous_unique_fixed_64_7:           # @contiguous_unique_fixed_64_7
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	64(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	192(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	prefetcht0	320(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end26:
	.size	contiguous_unique_fixed_64_7, .Lfunc_end26-contiguous_unique_fixed_64_7
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_64_8    # -- Begin function contiguous_unique_fixed_64_8
	.p2align	4
	.type	contiguous_unique_fixed_64_8,@function
contiguous_unique_fixed_64_8:           # @contiguous_unique_fixed_64_8
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	64(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	192(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	prefetcht0	320(%rdi,%rsi)
	prefetcht0	384(%rdi,%rsi)
	prefetcht0	(%rdi,%rdx)
	#APP
	#NO_APP
	retq
.Lfunc_end27:
	.size	contiguous_unique_fixed_64_8, .Lfunc_end27-contiguous_unique_fixed_64_8
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_1   # -- Begin function contiguous_unique_fixed_128_1
	.p2align	4
	.type	contiguous_unique_fixed_128_1,@function
contiguous_unique_fixed_128_1:          # @contiguous_unique_fixed_128_1
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	#APP
	#NO_APP
	retq
.Lfunc_end28:
	.size	contiguous_unique_fixed_128_1, .Lfunc_end28-contiguous_unique_fixed_128_1
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_2   # -- Begin function contiguous_unique_fixed_128_2
	.p2align	4
	.type	contiguous_unique_fixed_128_2,@function
contiguous_unique_fixed_128_2:          # @contiguous_unique_fixed_128_2
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end29:
	.size	contiguous_unique_fixed_128_2, .Lfunc_end29-contiguous_unique_fixed_128_2
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_3   # -- Begin function contiguous_unique_fixed_128_3
	.p2align	4
	.type	contiguous_unique_fixed_128_3,@function
contiguous_unique_fixed_128_3:          # @contiguous_unique_fixed_128_3
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end30:
	.size	contiguous_unique_fixed_128_3, .Lfunc_end30-contiguous_unique_fixed_128_3
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_4   # -- Begin function contiguous_unique_fixed_128_4
	.p2align	4
	.type	contiguous_unique_fixed_128_4,@function
contiguous_unique_fixed_128_4:          # @contiguous_unique_fixed_128_4
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end31:
	.size	contiguous_unique_fixed_128_4, .Lfunc_end31-contiguous_unique_fixed_128_4
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_5   # -- Begin function contiguous_unique_fixed_128_5
	.p2align	4
	.type	contiguous_unique_fixed_128_5,@function
contiguous_unique_fixed_128_5:          # @contiguous_unique_fixed_128_5
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	prefetcht0	384(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end32:
	.size	contiguous_unique_fixed_128_5, .Lfunc_end32-contiguous_unique_fixed_128_5
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_6   # -- Begin function contiguous_unique_fixed_128_6
	.p2align	4
	.type	contiguous_unique_fixed_128_6,@function
contiguous_unique_fixed_128_6:          # @contiguous_unique_fixed_128_6
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	prefetcht0	384(%rdi,%rsi)
	prefetcht0	512(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end33:
	.size	contiguous_unique_fixed_128_6, .Lfunc_end33-contiguous_unique_fixed_128_6
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_7   # -- Begin function contiguous_unique_fixed_128_7
	.p2align	4
	.type	contiguous_unique_fixed_128_7,@function
contiguous_unique_fixed_128_7:          # @contiguous_unique_fixed_128_7
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	prefetcht0	384(%rdi,%rsi)
	prefetcht0	512(%rdi,%rsi)
	prefetcht0	640(%rdi,%rsi)
	#APP
	#NO_APP
	retq
.Lfunc_end34:
	.size	contiguous_unique_fixed_128_7, .Lfunc_end34-contiguous_unique_fixed_128_7
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_unique_fixed_128_8   # -- Begin function contiguous_unique_fixed_128_8
	.p2align	4
	.type	contiguous_unique_fixed_128_8,@function
contiguous_unique_fixed_128_8:          # @contiguous_unique_fixed_128_8
	.cfi_startproc
# %bb.0:
	prefetcht0	(%rdi)
	prefetcht0	(%rdi,%rsi)
	prefetcht0	128(%rdi,%rsi)
	prefetcht0	256(%rdi,%rsi)
	prefetcht0	384(%rdi,%rsi)
	prefetcht0	512(%rdi,%rsi)
	prefetcht0	640(%rdi,%rsi)
	prefetcht0	768(%rdi,%rsi)
	prefetcht0	(%rdi,%rdx)
	#APP
	#NO_APP
	retq
.Lfunc_end35:
	.size	contiguous_unique_fixed_128_8, .Lfunc_end35-contiguous_unique_fixed_128_8
	.cfi_endproc
                                        # -- End function
	.globl	contiguous_identity_selected_l1 # -- Begin function contiguous_identity_selected_l1
	.p2align	4
	.type	contiguous_identity_selected_l1,@function
contiguous_identity_selected_l1:        # @contiguous_identity_selected_l1
	.cfi_startproc
# %bb.0:
	movzbl	10(%rdi), %ecx
	leaq	.LJTI36_0(%rip), %rdx
	movzwl	8(%rdi), %eax
	movslq	(%rdx,%rcx,4), %rcx
	addq	%rdx, %rcx
	jmpq	*%rcx
.LBB36_1:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_16:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	640(%rsi)
	prefetcht0	768(%rsi)
	prefetcht0	896(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
.LBB36_17:
	retq
.LBB36_15:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	640(%rsi)
	prefetcht0	768(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_12:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_5:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_13:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_10:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_3:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_4:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_8:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	320(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	448(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_2:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_6:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	320(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_14:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	512(%rsi)
	prefetcht0	640(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_7:
	prefetcht0	(%rsi)
	prefetcht0	64(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	192(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	320(%rsi)
	prefetcht0	384(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_11:
	prefetcht0	(%rsi)
	prefetcht0	128(%rsi)
	prefetcht0	256(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB36_9:
	prefetcht0	(%rsi)
	prefetcht0	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.Lfunc_end36:
	.size	contiguous_identity_selected_l1, .Lfunc_end36-contiguous_identity_selected_l1
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI36_0:
	.long	.LBB36_1-.LJTI36_0
	.long	.LBB36_2-.LJTI36_0
	.long	.LBB36_3-.LJTI36_0
	.long	.LBB36_4-.LJTI36_0
	.long	.LBB36_5-.LJTI36_0
	.long	.LBB36_6-.LJTI36_0
	.long	.LBB36_7-.LJTI36_0
	.long	.LBB36_8-.LJTI36_0
	.long	.LBB36_9-.LJTI36_0
	.long	.LBB36_10-.LJTI36_0
	.long	.LBB36_11-.LJTI36_0
	.long	.LBB36_12-.LJTI36_0
	.long	.LBB36_13-.LJTI36_0
	.long	.LBB36_14-.LJTI36_0
	.long	.LBB36_15-.LJTI36_0
	.long	.LBB36_16-.LJTI36_0
	.long	.LBB36_17-.LJTI36_0
                                        # -- End function
	.text
	.globl	contiguous_identity_selected_l2 # -- Begin function contiguous_identity_selected_l2
	.p2align	4
	.type	contiguous_identity_selected_l2,@function
contiguous_identity_selected_l2:        # @contiguous_identity_selected_l2
	.cfi_startproc
# %bb.0:
	movzbl	10(%rdi), %ecx
	leaq	.LJTI37_0(%rip), %rdx
	movzwl	8(%rdi), %eax
	movslq	(%rdx,%rcx,4), %rcx
	addq	%rdx, %rcx
	jmpq	*%rcx
.LBB37_1:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_16:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	640(%rsi)
	prefetcht1	768(%rsi)
	prefetcht1	896(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
.LBB37_17:
	retq
.LBB37_15:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	640(%rsi)
	prefetcht1	768(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_12:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_5:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_13:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_10:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_3:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_4:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_8:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	320(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	448(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_2:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_6:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	320(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_14:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	512(%rsi)
	prefetcht1	640(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_7:
	prefetcht1	(%rsi)
	prefetcht1	64(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	192(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	320(%rsi)
	prefetcht1	384(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_11:
	prefetcht1	(%rsi)
	prefetcht1	128(%rsi)
	prefetcht1	256(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.LBB37_9:
	prefetcht1	(%rsi)
	prefetcht1	(%rsi,%rax)
	#APP
	#NO_APP
	#APP
	#NO_APP
	retq
.Lfunc_end37:
	.size	contiguous_identity_selected_l2, .Lfunc_end37-contiguous_identity_selected_l2
	.cfi_endproc
	.section	.rodata,"a",@progbits
	.p2align	2, 0x0
.LJTI37_0:
	.long	.LBB37_1-.LJTI37_0
	.long	.LBB37_2-.LJTI37_0
	.long	.LBB37_3-.LJTI37_0
	.long	.LBB37_4-.LJTI37_0
	.long	.LBB37_5-.LJTI37_0
	.long	.LBB37_6-.LJTI37_0
	.long	.LBB37_7-.LJTI37_0
	.long	.LBB37_8-.LJTI37_0
	.long	.LBB37_9-.LJTI37_0
	.long	.LBB37_10-.LJTI37_0
	.long	.LBB37_11-.LJTI37_0
	.long	.LBB37_12-.LJTI37_0
	.long	.LBB37_13-.LJTI37_0
	.long	.LBB37_14-.LJTI37_0
	.long	.LBB37_15-.LJTI37_0
	.long	.LBB37_16-.LJTI37_0
	.long	.LBB37_17-.LJTI37_0
                                        # -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221033104+2078da43e25a-1~exp1~20251221153121.57)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
