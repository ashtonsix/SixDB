	.intel_syntax noprefix
	.file	"x86-index-codegen.cpp"
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0                          # -- Begin function index30_public
.LCPI0_0:
	.byte	0                               # 0x0
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	35                              # 0x23
	.byte	3                               # 0x3
	.byte	4                               # 0x4
	.byte	5                               # 0x5
	.byte	39                              # 0x27
	.byte	6                               # 0x6
	.byte	7                               # 0x7
	.byte	8                               # 0x8
	.byte	43                              # 0x2b
	.byte	9                               # 0x9
	.byte	10                              # 0xa
	.byte	11                              # 0xb
	.byte	47                              # 0x2f
	.byte	12                              # 0xc
	.byte	13                              # 0xd
	.byte	14                              # 0xe
	.byte	51                              # 0x33
	.byte	15                              # 0xf
	.byte	16                              # 0x10
	.byte	17                              # 0x11
	.byte	55                              # 0x37
	.byte	18                              # 0x12
	.byte	19                              # 0x13
	.byte	20                              # 0x14
	.byte	59                              # 0x3b
	.byte	21                              # 0x15
	.byte	22                              # 0x16
	.byte	23                              # 0x17
	.byte	63                              # 0x3f
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0
.LCPI0_1:
	.byte	7                               # 0x7
	.byte	6                               # 0x6
	.byte	5                               # 0x5
	.byte	4                               # 0x4
	.byte	3                               # 0x3
	.byte	2                               # 0x2
	.byte	1                               # 0x1
	.byte	0                               # 0x0
	.byte	15                              # 0xf
	.byte	14                              # 0xe
	.byte	13                              # 0xd
	.byte	12                              # 0xc
	.byte	11                              # 0xb
	.byte	10                              # 0xa
	.byte	9                               # 0x9
	.byte	8                               # 0x8
.LCPI0_2:
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
.LCPI0_3:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.text
	.globl	index30_public
	.p2align	4
	.type	index30_public,@function
index30_public:                         # @index30_public
	.cfi_startproc
# %bb.0:
	test	dx, dx
	sete	al
	cmp	rsi, 1073741824
	setae	cl
	or	cl, al
	jne	.LBB0_1
# %bb.3:
	mov	eax, 16777215
	kmovd	k1, eax
	vmovdqu8	ymm0 {k1} {z}, ymmword ptr [rdi]
	vpxor	xmm1, xmm1, xmm1
	vmovdqa	ymm2, ymmword ptr [rip + .LCPI0_0] # ymm2 = [0,1,2,35,3,4,5,39,6,7,8,43,9,10,11,47,12,13,14,51,15,16,17,55,18,19,20,59,21,22,23,63]
	vpermt2b	ymm0, ymm2, ymm1
	vmovdqu8	ymm3 {k1} {z}, ymmword ptr [rdi + 30]
	vpermt2b	ymm3, ymm2, ymm1
	mov	eax, dword ptr [rdi + 54]
	mov	ecx, dword ptr [rdi + 56]
	shl	rcx, 16
	or	rcx, rax
	mov	eax, dword ptr [rdi + 24]
	mov	edi, dword ptr [rdi + 26]
	shl	rdi, 16
	or	rdi, rax
	vmovq	xmm1, rcx
	vmovq	xmm2, rdi
	vpunpcklqdq	xmm1, xmm2, xmm1        # xmm1 = xmm2[0],xmm1[0]
	vpshufb	xmm1, xmm1, xmmword ptr [rip + .LCPI0_1] # xmm1 = xmm1[7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8]
	vpbroadcastq	xmm2, qword ptr [rip + .LCPI0_3] # xmm2 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm1, xmm2, xmm1, 0
	vpslld	ymm0, ymm0, 6
	vpslld	ymm2, ymm3, 6
	vpmovzxbd	ymm3, xmm1              # ymm3 = xmm1[0],zero,zero,zero,xmm1[1],zero,zero,zero,xmm1[2],zero,zero,zero,xmm1[3],zero,zero,zero,xmm1[4],zero,zero,zero,xmm1[5],zero,zero,zero,xmm1[6],zero,zero,zero,xmm1[7],zero,zero,zero
	vpor	ymm0, ymm0, ymm3
	vpshufd	xmm1, xmm1, 238                 # xmm1 = xmm1[2,3,2,3]
	vpmovzxbd	ymm1, xmm1              # ymm1 = xmm1[0],zero,zero,zero,xmm1[1],zero,zero,zero,xmm1[2],zero,zero,zero,xmm1[3],zero,zero,zero,xmm1[4],zero,zero,zero,xmm1[5],zero,zero,zero,xmm1[6],zero,zero,zero,xmm1[7],zero,zero,zero
	vpor	ymm1, ymm2, ymm1
	vpbroadcastd	ymm2, esi
	vpcmpeqd	k0, ymm0, ymm2
	vpcmpeqd	k1, ymm1, ymm2
	kunpckbw	k0, k1, k0
	kmovw	eax, k0
	and	edx, eax
	jmp	.LBB0_2
.LBB0_1:
	xor	edx, edx
.LBB0_2:
	tzcnt	rcx, rdx
	cmp	ecx, 16
	mov	eax, 16
	cmovb	eax, ecx
	vzeroupper
	ret
.Lfunc_end0:
	.size	index30_public, .Lfunc_end0-index30_public
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          # -- Begin function index30_fused_first
.LCPI1_0:
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
.LCPI1_2:
	.zero	16,4
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0
.LCPI1_1:
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
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0
.LCPI1_3:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.text
	.globl	index30_fused_first
	.p2align	4
	.type	index30_fused_first,@function
index30_fused_first:                    # @index30_fused_first
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	setae	cl
	test	rdx, rdx
	sete	r8b
	mov	eax, 16
	or	r8b, cl
	jne	.LBB1_6
# %bb.1:
	vpbroadcastb	xmm0, esi
	vgf2p8affineqb	xmm0, xmm0, qword ptr [rip + .LCPI1_3]{1to2}, 0
	shr	rsi, 6
	vmovdqu	xmm1, xmmword ptr [rdi]
	vinserti128	ymm2, ymm1, xmmword ptr [rdi + 8], 1
	vpbroadcastd	ymm1, esi
	vpshufb	ymm2, ymm2, ymmword ptr [rip + .LCPI1_1] # ymm2 = ymm2[0,1,2],zero,ymm2[3,4,5],zero,ymm2[6,7,8],zero,ymm2[9,10,11],zero,ymm2[20,21,22],zero,ymm2[23,24,25],zero,ymm2[26,27,28],zero,ymm2[29,30,31],zero
	vpcmpeqd	k0, ymm1, ymm2
	kmovb	ecx, k0
	mov	esi, edx
	and	esi, ecx
	je	.LBB1_3
# %bb.2:
	mov	ecx, dword ptr [rdi + 24]
	mov	r8d, dword ptr [rdi + 26]
	shl	r8, 16
	or	r8, rcx
	vpbroadcastq	xmm2, r8
	vpbroadcastq	xmm3, qword ptr [rip + .LCPI1_3] # xmm3 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm2, xmm3, xmm2, 0
	vpxor	xmm2, xmm2, xmm0
	vpcmpltub	k0, xmm2, xmmword ptr [rip + .LCPI1_2]
	kmovd	ecx, k0
	and	ecx, esi
	xor	r8d, r8d
	and	ecx, esi
	jne	.LBB1_5
.LBB1_3:
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	vpshufb	ymm2, ymm2, ymmword ptr [rip + .LCPI1_1] # ymm2 = ymm2[0,1,2],zero,ymm2[3,4,5],zero,ymm2[6,7,8],zero,ymm2[9,10,11],zero,ymm2[20,21,22],zero,ymm2[23,24,25],zero,ymm2[26,27,28],zero,ymm2[29,30,31],zero
	vpcmpeqd	k0, ymm1, ymm2
	kmovb	ecx, k0
	shr	rdx, 8
	and	edx, ecx
	je	.LBB1_6
# %bb.4:
	mov	ecx, dword ptr [rdi + 54]
	mov	esi, dword ptr [rdi + 56]
	shl	rsi, 16
	or	rsi, rcx
	vpbroadcastq	xmm1, rsi
	vpbroadcastq	xmm2, qword ptr [rip + .LCPI1_3] # xmm2 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm1, xmm2, xmm1, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmmword ptr [rip + .LCPI1_2]
	kmovd	ecx, k0
	and	ecx, edx
	mov	r8d, 8
	and	ecx, edx
	je	.LBB1_6
.LBB1_5:
	tzcnt	eax, ecx
	add	eax, r8d
.LBB1_6:
	vzeroupper
	ret
.Lfunc_end1:
	.size	index30_fused_first, .Lfunc_end1-index30_fused_first
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          # -- Begin function index30_fused_full
.LCPI2_0:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0
.LCPI2_1:
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
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0
.LCPI2_2:
	.zero	16,4
	.section	.rodata.cst4,"aM",@progbits,4
	.p2align	2, 0x0
.LCPI2_3:
	.zero	4,4
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
	vpbroadcastb	xmm0, esi
	vpbroadcastq	xmm1, qword ptr [rip + .LCPI2_0] # xmm1 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm0, xmm0, xmm1, 0
	shr	rsi, 6
	vmovdqu	xmm2, xmmword ptr [rdi]
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 8], 1
	vpbroadcastd	ymm3, esi
	vmovdqa	ymm4, ymmword ptr [rip + .LCPI2_1] # ymm4 = [0,1,2,128,3,4,5,128,6,7,8,128,9,10,11,128,4,5,6,128,7,8,9,128,10,11,12,128,13,14,15,128]
	vpshufb	ymm2, ymm2, ymm4
	vpcmpeqd	k0, ymm3, ymm2
	kmovb	ecx, k0
	mov	eax, dword ptr [rdi + 24]
	mov	esi, dword ptr [rdi + 26]
	shl	rsi, 16
	or	rsi, rax
	vpbroadcastq	xmm2, rsi
	vgf2p8affineqb	xmm2, xmm1, xmm2, 0
	vpxor	xmm2, xmm2, xmm0
	vpbroadcastd	xmm5, dword ptr [rip + .LCPI2_3] # xmm5 = [4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4]
	vpcmpltub	k0, xmm2, xmm5
	kmovd	eax, k0
	and	eax, edx
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	and	eax, ecx
	vpshufb	ymm2, ymm2, ymm4
	vpcmpeqd	k0, ymm3, ymm2
	kmovb	ecx, k0
	shr	edx, 8
	mov	esi, dword ptr [rdi + 54]
	mov	edi, dword ptr [rdi + 56]
	shl	rdi, 16
	or	rdi, rsi
	vpbroadcastq	xmm2, rdi
	vgf2p8affineqb	xmm1, xmm1, xmm2, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmm5
	kmovd	esi, k0
	and	esi, edx
	and	esi, ecx
	shl	esi, 8
	lea	eax, [rsi + rax + 65536]
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
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0                          # -- Begin function index30_prepared_first
.LCPI3_0:
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
.LCPI3_2:
	.zero	16,4
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0
.LCPI3_1:
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
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0
.LCPI3_3:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.text
	.globl	index30_prepared_first
	.p2align	4
	.type	index30_prepared_first,@function
index30_prepared_first:                 # @index30_prepared_first
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	setae	cl
	test	rdx, rdx
	sete	r8b
	mov	eax, 16
	or	r8b, cl
	jne	.LBB3_6
# %bb.1:
	mov	ecx, esi
	and	cl, 63
	vpbroadcastb	xmm0, ecx
	vgf2p8affineqb	xmm0, xmm0, qword ptr [rip + .LCPI3_3]{1to2}, 0
	shr	rsi, 6
	vmovdqu	xmm1, xmmword ptr [rdi]
	vinserti128	ymm2, ymm1, xmmword ptr [rdi + 8], 1
	vpbroadcastd	ymm1, esi
	vpshufb	ymm2, ymm2, ymmword ptr [rip + .LCPI3_1] # ymm2 = ymm2[0,1,2],zero,ymm2[3,4,5],zero,ymm2[6,7,8],zero,ymm2[9,10,11],zero,ymm2[20,21,22],zero,ymm2[23,24,25],zero,ymm2[26,27,28],zero,ymm2[29,30,31],zero
	vpcmpeqd	k0, ymm1, ymm2
	kmovb	ecx, k0
	and	ecx, edx
	je	.LBB3_3
# %bb.2:
	mov	esi, dword ptr [rdi + 24]
	mov	r8d, dword ptr [rdi + 26]
	shl	r8, 16
	or	r8, rsi
	vpbroadcastq	xmm2, r8
	vpbroadcastq	xmm3, qword ptr [rip + .LCPI3_3] # xmm3 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm2, xmm3, xmm2, 0
	vpxor	xmm2, xmm2, xmm0
	vpcmpltub	k0, xmm2, xmmword ptr [rip + .LCPI3_2]
	kmovd	esi, k0
	xor	r8d, r8d
	and	esi, ecx
	jne	.LBB3_5
.LBB3_3:
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	vpshufb	ymm2, ymm2, ymmword ptr [rip + .LCPI3_1] # ymm2 = ymm2[0,1,2],zero,ymm2[3,4,5],zero,ymm2[6,7,8],zero,ymm2[9,10,11],zero,ymm2[20,21,22],zero,ymm2[23,24,25],zero,ymm2[26,27,28],zero,ymm2[29,30,31],zero
	vpcmpeqd	k0, ymm1, ymm2
	shr	edx, 8
	kmovb	ecx, k0
	and	ecx, edx
	je	.LBB3_6
# %bb.4:
	mov	edx, dword ptr [rdi + 54]
	mov	esi, dword ptr [rdi + 56]
	shl	rsi, 16
	or	rsi, rdx
	vpbroadcastq	xmm1, rsi
	vpbroadcastq	xmm2, qword ptr [rip + .LCPI3_3] # xmm2 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm1, xmm2, xmm1, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmmword ptr [rip + .LCPI3_2]
	kmovd	esi, k0
	mov	r8d, 8
	and	esi, ecx
	je	.LBB3_6
.LBB3_5:
	tzcnt	eax, esi
	add	eax, r8d
.LBB3_6:
	vzeroupper
	ret
.Lfunc_end3:
	.size	index30_prepared_first, .Lfunc_end3-index30_prepared_first
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          # -- Begin function index30_prepared_full
.LCPI4_0:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.section	.rodata.cst32,"aM",@progbits,32
	.p2align	5, 0x0
.LCPI4_1:
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
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0
.LCPI4_2:
	.zero	16,4
	.section	.rodata.cst4,"aM",@progbits,4
	.p2align	2, 0x0
.LCPI4_3:
	.zero	4,4
	.text
	.globl	index30_prepared_full
	.p2align	4
	.type	index30_prepared_full,@function
index30_prepared_full:                  # @index30_prepared_full
	.cfi_startproc
# %bb.0:
	cmp	rsi, 1073741824
	setae	cl
	test	rdx, rdx
	sete	r8b
	mov	eax, 16
	or	r8b, cl
	jne	.LBB4_2
# %bb.1:
	mov	eax, esi
	and	al, 63
	vpbroadcastb	xmm0, eax
	vpbroadcastq	xmm1, qword ptr [rip + .LCPI4_0] # xmm1 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm0, xmm0, xmm1, 0
	shr	rsi, 6
	vmovdqu	xmm2, xmmword ptr [rdi]
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 8], 1
	vpbroadcastd	ymm3, esi
	vmovdqa	ymm4, ymmword ptr [rip + .LCPI4_1] # ymm4 = [0,1,2,128,3,4,5,128,6,7,8,128,9,10,11,128,4,5,6,128,7,8,9,128,10,11,12,128,13,14,15,128]
	vpshufb	ymm2, ymm2, ymm4
	mov	eax, dword ptr [rdi + 24]
	mov	ecx, dword ptr [rdi + 26]
	shl	rcx, 16
	or	rcx, rax
	vpbroadcastq	xmm5, rcx
	vgf2p8affineqb	xmm5, xmm1, xmm5, 0
	vpxor	xmm5, xmm5, xmm0
	vpbroadcastd	xmm6, dword ptr [rip + .LCPI4_3] # xmm6 = [4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4]
	vpcmpltub	k1, xmm5, xmm6
	vpcmpeqd	k0 {k1}, ymm3, ymm2
	kmovb	eax, k0
	and	eax, edx
	vmovdqu	xmm2, xmmword ptr [rdi + 30]
	vinserti128	ymm2, ymm2, xmmword ptr [rdi + 38], 1
	vpshufb	ymm2, ymm2, ymm4
	vpcmpeqd	k0, ymm3, ymm2
	shr	edx, 8
	kmovd	ecx, k0
	and	ecx, edx
	mov	edx, dword ptr [rdi + 54]
	mov	esi, dword ptr [rdi + 56]
	shl	rsi, 16
	or	rsi, rdx
	vpbroadcastq	xmm2, rsi
	vgf2p8affineqb	xmm1, xmm1, xmm2, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmm6
	kmovb	edx, k0
	and	edx, ecx
	shl	edx, 8
	or	edx, eax
	or	rdx, 65536
	tzcnt	rax, rdx
.LBB4_2:
                                        # kill: def $eax killed $eax killed $rax
	vzeroupper
	ret
.Lfunc_end4:
	.size	index30_prepared_full, .Lfunc_end4-index30_prepared_full
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          # -- Begin function equal6_bounded
.LCPI5_0:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0
.LCPI5_1:
	.zero	16,4
	.text
	.globl	equal6_bounded
	.p2align	4
	.type	equal6_bounded,@function
equal6_bounded:                         # @equal6_bounded
	.cfi_startproc
# %bb.0:
	vpbroadcastb	xmm0, esi
	vpbroadcastq	xmm1, qword ptr [rip + .LCPI5_0] # xmm1 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm0, xmm0, xmm1, 0
	mov	eax, dword ptr [rdi]
	mov	ecx, dword ptr [rdi + 2]
	shl	rcx, 16
	or	rcx, rax
	vpbroadcastq	xmm2, rcx
	vgf2p8affineqb	xmm1, xmm1, xmm2, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmmword ptr [rip + .LCPI5_1]
	kmovb	eax, k0
	ret
.Lfunc_end5:
	.size	equal6_bounded, .Lfunc_end5-equal6_bounded
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst8,"aM",@progbits,8
	.p2align	3, 0x0                          # -- Begin function equal6_owner
.LCPI6_0:
	.byte	1                               # 0x1
	.byte	2                               # 0x2
	.byte	4                               # 0x4
	.byte	8                               # 0x8
	.byte	16                              # 0x10
	.byte	32                              # 0x20
	.byte	64                              # 0x40
	.byte	128                             # 0x80
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4, 0x0
.LCPI6_1:
	.zero	16,4
	.text
	.globl	equal6_owner
	.p2align	4
	.type	equal6_owner,@function
equal6_owner:                           # @equal6_owner
	.cfi_startproc
# %bb.0:
	vpbroadcastb	xmm0, esi
	vpbroadcastq	xmm1, qword ptr [rip + .LCPI6_0] # xmm1 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vgf2p8affineqb	xmm0, xmm0, xmm1, 0
	vgf2p8affineqb	xmm1, xmm1, qword ptr [rdi]{1to2}, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmmword ptr [rip + .LCPI6_1]
	kmovb	eax, k0
	ret
.Lfunc_end6:
	.size	equal6_owner, .Lfunc_end6-equal6_owner
	.cfi_endproc
                                        # -- End function
	.ident	"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
