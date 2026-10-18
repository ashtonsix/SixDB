	.intel_syntax noprefix
	.file	"x86-index-codegen.cpp"
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0                          # -- Begin function index30_public
.LCPI0_0:
	.byte	0                               # 0x0
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	128                             # 0x80
	.byte	3                               # 0x3
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	128                             # 0x80
	.byte	6                               # 0x6
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	128                             # 0x80
	.byte	9                               # 0x9
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	128                             # 0x80
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	6                               # 0x6
	.byte	128                             # 0x80
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	9                               # 0x9
	.byte	128                             # 0x80
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	12                              # 0xc
	.byte	128                             # 0x80
	.byte	13                              # 0xd
	.byte	14                              # 0xe
	.byte	15                              # 0xf
	.byte	128                             # 0x80
	.text
	.globl	index30_public
	.p2align	4
	.type	index30_public,@function
index30_public:                         # @index30_public
	.cfi_startproc
# %bb.0:
	and	rdx, 65535
	sete	al
	cmp	rsi, 1073741824
	setae	cl
	or	cl, al
	jne	.LBB0_2
# %bb.1:
	mov	eax, esi
	and	eax, 63
	movabs	rcx, 72340172838076673
	mov	r8d, dword ptr [rdi + 26]
	vmovdqu	xmm1, xmmword ptr [rdi]
	shr	rsi, 6
	pdep	rcx, rax, rcx
	vinserti128	ymm2, ymm1, xmmword ptr [rdi + 8], 1
	vmovdqa	ymm1, ymmword ptr [rip + .LCPI0_0] # ymm1 = [0,1,2,128,3,4,5,128,6,7,8,128,9,10,11,128,4,5,6,128,7,8,9,128,10,11,12,128,13,14,15,128]
	vmovd	xmm0, esi
	vpbroadcastd	ymm0, xmm0
	mov	rax, rcx
	shl	rax, 8
	sub	rax, rcx
	mov	ecx, dword ptr [rdi + 24]
	shl	r8, 16
	vpshufb	ymm2, ymm2, ymm1
	or	r8, rcx
	xor	r8, rax
	vpcmpeqd	ymm2, ymm0, ymm2
	not	r8
	vmovmskps	esi, ymm2
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	mov	rcx, r8
	shr	rcx, 16
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	and	ecx, r8d
	mov	r8d, ecx
	shr	r8d, 16
	and	r8d, ecx
	mov	ecx, r8d
	shr	ecx, 8
	and	ecx, r8d
	mov	r8d, dword ptr [rdi + 54]
	mov	edi, dword ptr [rdi + 56]
	and	ecx, esi
	vpshufb	ymm1, ymm2, ymm1
	shl	rdi, 16
	vpcmpeqd	ymm0, ymm0, ymm1
	or	rdi, r8
	vmovmskps	esi, ymm0
	xor	rdi, rax
	not	rdi
	mov	rax, rdi
	shr	rax, 16
	and	eax, edi
	mov	edi, eax
	shr	edi, 16
	and	edi, eax
	mov	eax, edi
	shr	eax, 8
	and	eax, edi
	and	eax, esi
	shl	eax, 8
	or	eax, ecx
	and	edx, eax
	mov	eax, 16
	tzcnt	rcx, rdx
	cmp	ecx, 16
	cmovb	eax, ecx
	vzeroupper
	ret
.LBB0_2:
	mov	eax, 16
	ret
.Lfunc_end0:
	.size	index30_public, .Lfunc_end0-index30_public
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0                          # -- Begin function index30_fused_first
.LCPI1_0:
	.byte	0                               # 0x0
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	128                             # 0x80
	.byte	3                               # 0x3
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	128                             # 0x80
	.byte	6                               # 0x6
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	128                             # 0x80
	.byte	9                               # 0x9
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	128                             # 0x80
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	6                               # 0x6
	.byte	128                             # 0x80
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	9                               # 0x9
	.byte	128                             # 0x80
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	12                              # 0xc
	.byte	128                             # 0x80
	.byte	13                              # 0xd
	.byte	14                              # 0xe
	.byte	15                              # 0xf
	.byte	128                             # 0x80
	.text
	.globl	index30_fused_first
	.p2align	4
	.type	index30_fused_first,@function
index30_fused_first:                    # @index30_fused_first
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	mov	eax, 16
	setae	cl
	test	rdx, rdx
	sete	r8b
	or	r8b, cl
	jne	.LBB1_6
# %bb.1:
	vmovdqu	xmm1, xmmword ptr [rdi]
	movabs	rcx, 72340172838076673
	vinserti128	ymm1, ymm1, xmmword ptr [rdi + 8], 1
	pdep	r8, rsi, rcx
	shr	rsi, 6
	vpshufb	ymm1, ymm1, ymmword ptr [rip + .LCPI1_0] # ymm1 = ymm1[0,1,2],zero,ymm1[3,4,5],zero,ymm1[6,7,8],zero,ymm1[9,10,11],zero,ymm1[20,21,22],zero,ymm1[23,24,25],zero,ymm1[26,27,28],zero,ymm1[29,30,31],zero
	vmovd	xmm0, esi
	mov	esi, edx
	vpbroadcastd	ymm0, xmm0
	mov	rcx, r8
	shl	rcx, 8
	sub	rcx, r8
	vpcmpeqd	ymm1, ymm0, ymm1
	vmovmskps	r8d, ymm1
	and	esi, r8d
	je	.LBB1_3
# %bb.2:
	mov	r9d, dword ptr [rdi + 26]
	mov	r8d, dword ptr [rdi + 24]
	shl	r9, 16
	or	r9, r8
	xor	r9, rcx
	not	r9
	mov	r8, r9
	shr	r8, 16
	and	r8d, r9d
	mov	r9d, r8d
	shr	r9d, 16
	and	r9d, r8d
	mov	r8d, r9d
	shr	r8d, 8
	and	esi, r8d
	xor	r8d, r8d
	and	esi, r9d
	jne	.LBB1_5
.LBB1_3:
	vmovdqu	xmm1, xmmword ptr [rdi + 30]
	shr	rdx, 8
	vinserti128	ymm1, ymm1, xmmword ptr [rdi + 38], 1
	vpshufb	ymm1, ymm1, ymmword ptr [rip + .LCPI1_0] # ymm1 = ymm1[0,1,2],zero,ymm1[3,4,5],zero,ymm1[6,7,8],zero,ymm1[9,10,11],zero,ymm1[20,21,22],zero,ymm1[23,24,25],zero,ymm1[26,27,28],zero,ymm1[29,30,31],zero
	vpcmpeqd	ymm0, ymm0, ymm1
	vmovmskps	esi, ymm0
	and	edx, esi
	je	.LBB1_6
# %bb.4:
	mov	esi, dword ptr [rdi + 54]
	mov	edi, dword ptr [rdi + 56]
	mov	r8d, 8
	shl	rdi, 16
	or	rdi, rsi
	xor	rcx, rdi
	not	rcx
	mov	rsi, rcx
	shr	rsi, 16
	and	esi, ecx
	mov	ecx, esi
	shr	ecx, 16
	and	ecx, esi
	mov	esi, ecx
	shr	esi, 8
	and	edx, esi
	and	edx, ecx
	mov	esi, edx
	je	.LBB1_6
.LBB1_5:
	tzcnt	eax, esi
	add	eax, r8d
.LBB1_6:
	vzeroupper
	ret
.Lfunc_end1:
	.size	index30_fused_first, .Lfunc_end1-index30_fused_first
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0                          # -- Begin function index30_fused_full
.LCPI2_0:
	.byte	0                               # 0x0
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	128                             # 0x80
	.byte	3                               # 0x3
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	128                             # 0x80
	.byte	6                               # 0x6
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	128                             # 0x80
	.byte	9                               # 0x9
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	128                             # 0x80
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	6                               # 0x6
	.byte	128                             # 0x80
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	9                               # 0x9
	.byte	128                             # 0x80
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	12                              # 0xc
	.byte	128                             # 0x80
	.byte	13                              # 0xd
	.byte	14                              # 0xe
	.byte	15                              # 0xf
	.byte	128                             # 0x80
	.text
	.globl	index30_fused_full
	.p2align	4
	.type	index30_fused_full,@function
index30_fused_full:                     # @index30_fused_full
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	setae	al
	test	rdx, rdx
	sete	cl
	or	cl, al
	jne	.LBB2_2
# %bb.1:
	movabs	rax, 72340172838076673
	mov	r8d, dword ptr [rdi + 26]
	vmovdqu	xmm1, xmmword ptr [rdi]
	pdep	rcx, rsi, rax
	vinserti128	ymm2, ymm1, xmmword ptr [rdi + 8], 1
	vmovdqa	ymm1, ymmword ptr [rip + .LCPI2_0] # ymm1 = [0,1,2,128,3,4,5,128,6,7,8,128,9,10,11,128,4,5,6,128,7,8,9,128,10,11,12,128,13,14,15,128]
	shr	rsi, 6
	vmovd	xmm0, esi
	vpbroadcastd	ymm0, xmm0
	mov	rax, rcx
	shl	rax, 8
	sub	rax, rcx
	mov	ecx, dword ptr [rdi + 24]
	shl	r8, 16
	vpshufb	ymm2, ymm2, ymm1
	or	r8, rcx
	xor	r8, rax
	vpcmpeqd	ymm2, ymm0, ymm2
	not	r8
	vmovmskps	esi, ymm2
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	mov	rcx, r8
	shr	rcx, 16
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	and	ecx, r8d
	mov	r8d, ecx
	shr	r8d, 16
	and	r8d, ecx
	mov	ecx, r8d
	shr	ecx, 8
	and	ecx, edx
	and	ecx, r8d
	mov	r8d, dword ptr [rdi + 54]
	mov	edi, dword ptr [rdi + 56]
	and	ecx, esi
	vpshufb	ymm1, ymm2, ymm1
	vpcmpeqd	ymm0, ymm0, ymm1
	shl	rdi, 16
	vmovmskps	esi, ymm0
	or	rdi, r8
	xor	rdi, rax
	not	rdi
	mov	rax, rdi
	shr	rax, 16
	and	eax, edi
	mov	edi, eax
	shr	edi, 16
	and	edi, eax
	and	edx, edi
	shr	edx, 8
	and	edx, edi
	and	edx, esi
	shl	edx, 8
	lea	eax, [rdx + rcx + 65536]
	tzcnt	eax, eax
	vzeroupper
	ret
.LBB2_2:
	mov	eax, 16
	ret
.Lfunc_end2:
	.size	index30_fused_full, .Lfunc_end2-index30_fused_full
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0                          # -- Begin function index30_prepared_first
.LCPI3_0:
	.byte	0                               # 0x0
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	128                             # 0x80
	.byte	3                               # 0x3
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	128                             # 0x80
	.byte	6                               # 0x6
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	128                             # 0x80
	.byte	9                               # 0x9
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	128                             # 0x80
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	6                               # 0x6
	.byte	128                             # 0x80
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	9                               # 0x9
	.byte	128                             # 0x80
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	12                              # 0xc
	.byte	128                             # 0x80
	.byte	13                              # 0xd
	.byte	14                              # 0xe
	.byte	15                              # 0xf
	.byte	128                             # 0x80
	.text
	.globl	index30_prepared_first
	.p2align	4
	.type	index30_prepared_first,@function
index30_prepared_first:                 # @index30_prepared_first
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	mov	eax, 16
	setae	cl
	test	rdx, rdx
	sete	r8b
	or	r8b, cl
	jne	.LBB3_6
# %bb.1:
	vmovdqu	xmm1, xmmword ptr [rdi]
	mov	ecx, esi
	shr	rsi, 6
	and	ecx, 63
	movabs	r8, 72340172838076673
	vinserti128	ymm1, ymm1, xmmword ptr [rdi + 8], 1
	pdep	r8, rcx, r8
	vmovd	xmm0, esi
	vpshufb	ymm1, ymm1, ymmword ptr [rip + .LCPI3_0] # ymm1 = ymm1[0,1,2],zero,ymm1[3,4,5],zero,ymm1[6,7,8],zero,ymm1[9,10,11],zero,ymm1[20,21,22],zero,ymm1[23,24,25],zero,ymm1[26,27,28],zero,ymm1[29,30,31],zero
	vpbroadcastd	ymm0, xmm0
	mov	rcx, r8
	shl	rcx, 8
	sub	rcx, r8
	vpcmpeqd	ymm1, ymm0, ymm1
	vmovmskps	esi, ymm1
	and	esi, edx
	je	.LBB3_3
# %bb.2:
	mov	r9d, dword ptr [rdi + 26]
	mov	r8d, dword ptr [rdi + 24]
	shl	r9, 16
	or	r9, r8
	xor	r9, rcx
	not	r9
	mov	r8, r9
	shr	r8, 16
	and	r8d, r9d
	mov	r9d, r8d
	shr	r9d, 16
	and	r9d, r8d
	mov	r8d, r9d
	shr	r8d, 8
	and	r8d, esi
	xor	esi, esi
	and	r8d, r9d
	jne	.LBB3_5
.LBB3_3:
	vmovdqu	xmm1, xmmword ptr [rdi + 30]
	shr	edx, 8
	vinserti128	ymm1, ymm1, xmmword ptr [rdi + 38], 1
	vpshufb	ymm1, ymm1, ymmword ptr [rip + .LCPI3_0] # ymm1 = ymm1[0,1,2],zero,ymm1[3,4,5],zero,ymm1[6,7,8],zero,ymm1[9,10,11],zero,ymm1[20,21,22],zero,ymm1[23,24,25],zero,ymm1[26,27,28],zero,ymm1[29,30,31],zero
	vpcmpeqd	ymm0, ymm0, ymm1
	vmovmskps	esi, ymm0
	and	edx, esi
	je	.LBB3_6
# %bb.4:
	mov	esi, dword ptr [rdi + 54]
	mov	edi, dword ptr [rdi + 56]
	shl	rdi, 16
	or	rdi, rsi
	xor	rcx, rdi
	not	rcx
	mov	rsi, rcx
	shr	rsi, 16
	and	esi, ecx
	mov	ecx, esi
	shr	ecx, 16
	and	ecx, esi
	mov	esi, 8
	mov	r8d, ecx
	shr	r8d, 8
	and	r8d, edx
	and	r8d, ecx
	je	.LBB3_6
.LBB3_5:
	tzcnt	eax, r8d
	add	eax, esi
.LBB3_6:
	vzeroupper
	ret
.Lfunc_end3:
	.size	index30_prepared_first, .Lfunc_end3-index30_prepared_first
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0                          # -- Begin function index30_prepared_full
.LCPI4_0:
	.byte	0                               # 0x0
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	128                             # 0x80
	.byte	3                               # 0x3
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	128                             # 0x80
	.byte	6                               # 0x6
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	128                             # 0x80
	.byte	9                               # 0x9
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	128                             # 0x80
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	6                               # 0x6
	.byte	128                             # 0x80
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	9                               # 0x9
	.byte	128                             # 0x80
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	12                              # 0xc
	.byte	128                             # 0x80
	.byte	13                              # 0xd
	.byte	14                              # 0xe
	.byte	15                              # 0xf
	.byte	128                             # 0x80
	.text
	.globl	index30_prepared_full
	.p2align	4
	.type	index30_prepared_full,@function
index30_prepared_full:                  # @index30_prepared_full
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	setae	al
	test	rdx, rdx
	sete	cl
	or	cl, al
	jne	.LBB4_2
# %bb.1:
	mov	eax, esi
	and	eax, 63
	movabs	rcx, 72340172838076673
	mov	r8d, dword ptr [rdi + 26]
	vmovdqu	xmm1, xmmword ptr [rdi]
	shr	rsi, 6
	pdep	rcx, rax, rcx
	vinserti128	ymm2, ymm1, xmmword ptr [rdi + 8], 1
	vmovdqa	ymm1, ymmword ptr [rip + .LCPI4_0] # ymm1 = [0,1,2,128,3,4,5,128,6,7,8,128,9,10,11,128,4,5,6,128,7,8,9,128,10,11,12,128,13,14,15,128]
	vmovd	xmm0, esi
	vpbroadcastd	ymm0, xmm0
	mov	rax, rcx
	shl	rax, 8
	sub	rax, rcx
	mov	ecx, dword ptr [rdi + 24]
	shl	r8, 16
	vpshufb	ymm2, ymm2, ymm1
	or	r8, rcx
	xor	r8, rax
	vpcmpeqd	ymm2, ymm0, ymm2
	not	r8
	vmovmskps	esi, ymm2
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	mov	rcx, r8
	shr	rcx, 16
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	and	ecx, r8d
	mov	r8d, ecx
	shr	r8d, 16
	and	r8d, ecx
	mov	ecx, r8d
	shr	ecx, 8
	and	ecx, edx
	and	ecx, r8d
	mov	r8d, dword ptr [rdi + 54]
	mov	edi, dword ptr [rdi + 56]
	and	ecx, esi
	vpshufb	ymm1, ymm2, ymm1
	vpcmpeqd	ymm0, ymm0, ymm1
	shl	rdi, 16
	vmovmskps	esi, ymm0
	or	rdi, r8
	xor	rdi, rax
	not	rdi
	mov	rax, rdi
	shr	rax, 16
	and	eax, edi
	mov	edi, eax
	shr	edi, 16
	and	edi, eax
	and	edx, edi
	shr	edx, 8
	and	edx, edi
	and	edx, esi
	shl	edx, 8
	lea	eax, [rdx + rcx + 65536]
	tzcnt	eax, eax
	vzeroupper
	ret
.LBB4_2:
	mov	eax, 16
	ret
.Lfunc_end4:
	.size	index30_prepared_full, .Lfunc_end4-index30_prepared_full
	.cfi_endproc
                                        # -- End function
	.globl	equal6_bounded                  # -- Begin function equal6_bounded
	.p2align	4
	.type	equal6_bounded,@function
equal6_bounded:                         # @equal6_bounded
	.cfi_startproc
# %bb.0:
	movabs	rax, 72340172838076673
	mov	edx, dword ptr [rdi + 2]
	pdep	rax, rsi, rax
	mov	rcx, rax
	shl	rcx, 8
	sub	rcx, rax
	mov	eax, dword ptr [rdi]
	shl	rdx, 16
	or	rdx, rax
	xor	rdx, rcx
	not	rdx
	mov	rax, rdx
	shr	rax, 16
	and	eax, edx
	mov	ecx, eax
	shr	ecx, 16
	and	ecx, eax
	mov	eax, ecx
	shr	eax, 8
	and	eax, ecx
	ret
.Lfunc_end5:
	.size	equal6_bounded, .Lfunc_end5-equal6_bounded
	.cfi_endproc
                                        # -- End function
	.globl	equal6_owner                    # -- Begin function equal6_owner
	.p2align	4
	.type	equal6_owner,@function
equal6_owner:                           # @equal6_owner
	.cfi_startproc
# %bb.0:
	movabs	rax, 72340172838076673
	pdep	rax, rsi, rax
	mov	rcx, rax
	shl	rcx, 8
	sub	rcx, rax
	xor	rcx, qword ptr [rdi]
	not	rcx
	mov	rax, rcx
	shr	rax, 16
	and	eax, ecx
	mov	ecx, eax
	shr	ecx, 16
	and	ecx, eax
	mov	eax, ecx
	shr	eax, 8
	and	eax, ecx
	ret
.Lfunc_end6:
	.size	equal6_owner, .Lfunc_end6-equal6_owner
	.cfi_endproc
                                        # -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
