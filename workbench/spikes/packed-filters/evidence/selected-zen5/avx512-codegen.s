	.file	"codegen.cpp"
	.text
	.globl	series8_equal                   # -- Begin function series8_equal
	.p2align	4
	.type	series8_equal,@function
series8_equal:                          # @series8_equal
	.cfi_startproc
# %bb.0:
	cmpq	$255, %rsi
	jbe	.LBB0_2
# %bb.1:
	xorl	%eax, %eax
	retq
.LBB0_2:
	vpbroadcastb	%esi, %xmm0
	vpcmpeqb	(%rdi), %xmm0, %k0
	kmovw	%k0, %eax
	retq
.Lfunc_end0:
	.size	series8_equal, .Lfunc_end0-series8_equal
	.cfi_endproc
                                        # -- End function
	.globl	fused8_equal                    # -- Begin function fused8_equal
	.p2align	4
	.type	fused8_equal,@function
fused8_equal:                           # @fused8_equal
	.cfi_startproc
# %bb.0:
	cmpq	$255, %rsi
	jbe	.LBB1_2
# %bb.1:
	xorl	%eax, %eax
	retq
.LBB1_2:
	vpbroadcastb	%esi, %xmm0
	vpcmpeqb	(%rdi), %xmm0, %k0
	kmovw	%k0, %eax
	retq
.Lfunc_end1:
	.size	fused8_equal, .Lfunc_end1-fused8_equal
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          # -- Begin function series3_equal
.LCPI2_0:
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	2                               # 0x2
	.byte	1                               # 0x1
	.byte	0                               # 0x0
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	128                             # 0x80
	.byte	5                               # 0x5
	.byte	4                               # 0x4
	.byte	3                               # 0x3
.LCPI2_1:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0
.LCPI2_2:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.text
	.globl	series3_equal
	.p2align	4
	.type	series3_equal,@function
series3_equal:                          # @series3_equal
	.cfi_startproc
# %bb.0:
	cmpq	$7, %rsi
	jbe	.LBB2_3
# %bb.1:
	xorl	%eax, %eax
	retq
.LBB2_3:
	movl	2(%rdi), %ecx
	movl	(%rdi), %eax
	vpbroadcastq	.LCPI2_2(%rip), %xmm2   # xmm2 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vpbroadcastb	%esi, %xmm0
	shlq	$16, %rcx
	orq	%rcx, %rax
	vmovq	%rax, %xmm1
	vpshufb	.LCPI2_0(%rip), %xmm1, %xmm1    # xmm1 = zero,zero,zero,zero,zero,xmm1[2,1,0],zero,zero,zero,zero,zero,xmm1[5,4,3]
	vgf2p8affineqb	$0, %xmm1, %xmm2, %xmm1
	vpcmpeqb	%xmm0, %xmm1, %k0
	kmovw	%k0, %eax
	retq
.Lfunc_end2:
	.size	series3_equal, .Lfunc_end2-series3_equal
	.cfi_endproc
                                        # -- End function
	.globl	packed3_equal                   # -- Begin function packed3_equal
	.p2align	4
	.type	packed3_equal,@function
packed3_equal:                          # @packed3_equal
	.cfi_startproc
# %bb.0:
	xorl	%eax, %eax
	cmpq	$7, %rsi
	ja	.LBB3_2
# %bb.1:
	movzbl	(%rdi), %edx
	movl	%esi, %ecx
	movzbl	1(%rdi), %r8d
	movzbl	2(%rdi), %r9d
	andb	$1, %cl
	decb	%cl
	xorb	%cl, %dl
	btl	$1, %esi
	adcl	$255, %eax
	shrq	$2, %rsi
	xorb	3(%rdi), %cl
	movzbl	%dl, %edx
	decl	%esi
	xorl	%eax, %r8d
	xorl	%esi, %r9d
	andl	%r8d, %r9d
	movzbl	4(%rdi), %r8d
	andl	%edx, %r9d
	movzbl	%cl, %ecx
	xorl	%eax, %r8d
	movzbl	5(%rdi), %eax
	xorl	%esi, %eax
	andl	%r8d, %eax
	andl	%ecx, %eax
	shll	$8, %eax
	orl	%r9d, %eax
.LBB3_2:
	retq
.Lfunc_end3:
	.size	packed3_equal, .Lfunc_end3-packed3_equal
	.cfi_endproc
                                        # -- End function
	.globl	series64_less                   # -- Begin function series64_less
	.p2align	4
	.type	series64_less,@function
series64_less:                          # @series64_less
	.cfi_startproc
# %bb.0:
	vpbroadcastq	%rsi, %ymm0
	vpcmpnleuq	96(%rdi), %ymm0, %k0
	kmovb	%k0, %ecx
	vpcmpnleuq	64(%rdi), %ymm0, %k0
	shll	$12, %ecx
	kmovb	%k0, %edx
	vpcmpnleuq	32(%rdi), %ymm0, %k0
	shll	$8, %edx
	kmovd	%k0, %eax
	vpcmpnleuq	(%rdi), %ymm0, %k0
	shlb	$4, %al
	kmovd	%k0, %esi
	orb	%al, %sil
	movzbl	%sil, %eax
	orq	%rdx, %rax
	orq	%rcx, %rax
	vzeroupper
	retq
.Lfunc_end4:
	.size	series64_less, .Lfunc_end4-series64_less
	.cfi_endproc
                                        # -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
