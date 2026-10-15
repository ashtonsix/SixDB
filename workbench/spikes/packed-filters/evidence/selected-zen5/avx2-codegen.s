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
	vmovd	%esi, %xmm0
	vpbroadcastb	%xmm0, %xmm0
	vpcmpeqb	(%rdi), %xmm0, %xmm0
	vpmovmskb	%xmm0, %eax
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
	vmovd	%esi, %xmm0
	vpbroadcastb	%xmm0, %xmm0
	vpcmpeqb	(%rdi), %xmm0, %xmm0
	vpmovmskb	%xmm0, %eax
	retq
.Lfunc_end1:
	.size	fused8_equal, .Lfunc_end1-fused8_equal
	.cfi_endproc
                                        # -- End function
	.globl	series3_equal                   # -- Begin function series3_equal
	.p2align	4
	.type	series3_equal,@function
series3_equal:                          # @series3_equal
	.cfi_startproc
# %bb.0:
	xorl	%eax, %eax
	cmpq	$7, %rsi
	ja	.LBB2_2
# %bb.1:
	movq	%rsi, %rcx
	shrq	$2, %rcx
	decl	%ecx
	btl	$1, %esi
	adcl	$-1, %eax
	andb	$1, %sil
	decb	%sil
	movzbl	4(%rdi), %edx
	xorl	%eax, %edx
	movzbl	3(%rdi), %r8d
	xorb	%sil, %r8b
	movzbl	%r8b, %r8d
	movzbl	5(%rdi), %r9d
	xorl	%ecx, %r9d
	andl	%edx, %r9d
	andl	%r8d, %r9d
	shll	$8, %r9d
	movzbl	1(%rdi), %edx
	xorl	%eax, %edx
	xorb	(%rdi), %sil
	movzbl	%sil, %esi
	movzbl	2(%rdi), %eax
	xorl	%ecx, %eax
	andl	%edx, %eax
	andl	%esi, %eax
	orl	%r9d, %eax
.LBB2_2:
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
	movl	%esi, %ecx
	andb	$1, %cl
	decb	%cl
	movzbl	(%rdi), %edx
	xorb	%cl, %dl
	btl	$1, %esi
	adcl	$255, %eax
	movzbl	1(%rdi), %r8d
	xorl	%eax, %r8d
	movzbl	%dl, %edx
	movzbl	2(%rdi), %r9d
	shrq	$2, %rsi
	decl	%esi
	xorl	%esi, %r9d
	andl	%r8d, %r9d
	andl	%edx, %r9d
	xorb	3(%rdi), %cl
	movzbl	4(%rdi), %edx
	xorl	%eax, %edx
	movzbl	%cl, %ecx
	movzbl	5(%rdi), %eax
	xorl	%esi, %eax
	andl	%edx, %eax
	andl	%ecx, %eax
	shll	$8, %eax
	orl	%r9d, %eax
.LBB3_2:
	retq
.Lfunc_end3:
	.size	packed3_equal, .Lfunc_end3-packed3_equal
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          # -- Begin function series64_less
.LCPI4_0:
	.quad	-9223372036854775808            # 0x8000000000000000
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0
.LCPI4_1:
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.short	255                             # 0xff
	.text
	.globl	series64_less
	.p2align	4
	.type	series64_less,@function
series64_less:                          # @series64_less
	.cfi_startproc
# %bb.0:
	movabsq	$-9223372036854775808, %rax     # imm = 0x8000000000000000
	xorq	%rsi, %rax
	vmovq	%rax, %xmm0
	vpbroadcastq	%xmm0, %ymm0
	vpbroadcastq	.LCPI4_0(%rip), %ymm1   # ymm1 = [9223372036854775808,9223372036854775808,9223372036854775808,9223372036854775808]
	vpxor	96(%rdi), %ymm1, %ymm2
	vpcmpgtq	%ymm2, %ymm0, %ymm2
	vmovmskpd	%ymm2, %ecx
	shll	$12, %ecx
	vpxor	64(%rdi), %ymm1, %ymm2
	vpcmpgtq	%ymm2, %ymm0, %ymm2
	vmovmskpd	%ymm2, %edx
	vpxor	32(%rdi), %ymm1, %ymm2
	shll	$8, %edx
	vpcmpgtq	%ymm2, %ymm0, %ymm2
	vextracti128	$1, %ymm2, %xmm3
	vpackssdw	%xmm3, %xmm2, %xmm2
	vpxor	(%rdi), %ymm1, %ymm1
	vpcmpgtq	%ymm1, %ymm0, %ymm0
	vextracti128	$1, %ymm0, %xmm1
	vpackssdw	%xmm1, %xmm0, %xmm0
	vpxor	%xmm1, %xmm1, %xmm1
	vpblendw	$170, %xmm1, %xmm2, %xmm2       # xmm2 = xmm2[0],xmm1[1],xmm2[2],xmm1[3],xmm2[4],xmm1[5],xmm2[6],xmm1[7]
	vpblendw	$170, %xmm1, %xmm0, %xmm0       # xmm0 = xmm0[0],xmm1[1],xmm0[2],xmm1[3],xmm0[4],xmm1[5],xmm0[6],xmm1[7]
	vpackusdw	%xmm2, %xmm0, %xmm0
	vpand	.LCPI4_1(%rip), %xmm0, %xmm0
	vpackuswb	%xmm0, %xmm0, %xmm0
	vpmovmskb	%xmm0, %eax
	movzbl	%al, %eax
	orl	%edx, %eax
	orl	%ecx, %eax
	vzeroupper
	retq
.Lfunc_end4:
	.size	series64_less, .Lfunc_end4-series64_less
	.cfi_endproc
                                        # -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
