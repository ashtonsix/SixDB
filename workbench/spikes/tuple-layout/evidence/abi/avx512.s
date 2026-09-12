	.file	"abi.cpp"
	.text
	.globl	scalar8                         # -- Begin function scalar8
	.p2align	4
	.type	scalar8,@function
scalar8:                                # @scalar8
	.cfi_startproc
# %bb.0:
	movq	(%rdi), %rax
	retq
.Lfunc_end0:
	.size	scalar8, .Lfunc_end0-scalar8
	.cfi_endproc
                                        # -- End function
	.globl	sysv_pair                       # -- Begin function sysv_pair
	.p2align	4
	.type	sysv_pair,@function
sysv_pair:                              # @sysv_pair
	.cfi_startproc
# %bb.0:
	movq	%rdi, %rax
	vmovups	(%rsi), %zmm0
	vmovups	%zmm0, (%rdi)
	vzeroupper
	retq
.Lfunc_end1:
	.size	sysv_pair, .Lfunc_end1-sysv_pair
	.cfi_endproc
                                        # -- End function
	.globl	sysv_single32                   # -- Begin function sysv_single32
	.p2align	4
	.type	sysv_single32,@function
sysv_single32:                          # @sysv_single32
	.cfi_startproc
# %bb.0:
	vmovups	(%rdi), %ymm0
	retq
.Lfunc_end2:
	.size	sysv_single32, .Lfunc_end2-sysv_single32
	.cfi_endproc
                                        # -- End function
	.globl	__regcall3__regcall_pair        # -- Begin function __regcall3__regcall_pair
	.p2align	4
	.type	__regcall3__regcall_pair,@function
__regcall3__regcall_pair:               # @__regcall3__regcall_pair
	.cfi_startproc
# %bb.0:
	vmovups	(%rax), %ymm0
	vmovups	32(%rax), %ymm1
	retq
.Lfunc_end3:
	.size	__regcall3__regcall_pair, .Lfunc_end3-__regcall3__regcall_pair
	.cfi_endproc
                                        # -- End function
	.globl	flattened_handoff               # -- Begin function flattened_handoff
	.p2align	4
	.type	flattened_handoff,@function
flattened_handoff:                      # @flattened_handoff
	.cfi_startproc
# %bb.0:
	vmovups	(%rdi), %ymm0
	vmovups	32(%rdi), %ymm1
	jmp	take_pair@PLT                   # TAILCALL
.Lfunc_end4:
	.size	flattened_handoff, .Lfunc_end4-flattened_handoff
	.cfi_endproc
                                        # -- End function
	.globl	consume_sysv                    # -- Begin function consume_sysv
	.p2align	4
	.type	consume_sysv,@function
consume_sysv:                           # @consume_sysv
	.cfi_startproc
# %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	andq	$-32, %rsp
	subq	$96, %rsp
	movq	%rdi, %rax
	movq	%rsp, %rdi
	callq	*%rax
	vmovaps	(%rsp), %ymm0
	vmovaps	32(%rsp), %ymm1
	callq	take_pair@PLT
	movq	%rbp, %rsp
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	vzeroupper
	retq
.Lfunc_end5:
	.size	consume_sysv, .Lfunc_end5-consume_sysv
	.cfi_endproc
                                        # -- End function
	.globl	consume_regcall                 # -- Begin function consume_regcall
	.p2align	4
	.type	consume_regcall,@function
consume_regcall:                        # @consume_regcall
	.cfi_startproc
# %bb.0:
	pushq	%rax
	.cfi_def_cfa_offset 16
	movq	%rsi, %rax
	callq	*%rdi
	popq	%rax
	.cfi_def_cfa_offset 8
	jmp	take_pair@PLT                   # TAILCALL
.Lfunc_end6:
	.size	consume_regcall, .Lfunc_end6-consume_regcall
	.cfi_endproc
                                        # -- End function
	.globl	sysv_single64                   # -- Begin function sysv_single64
	.p2align	4
	.type	sysv_single64,@function
sysv_single64:                          # @sysv_single64
	.cfi_startproc
# %bb.0:
	vmovups	(%rdi), %zmm0
	retq
.Lfunc_end7:
	.size	sysv_single64, .Lfunc_end7-sysv_single64
	.cfi_endproc
                                        # -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221033104+2078da43e25a-1~exp1~20251221153121.57)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
