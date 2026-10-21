.text
# Source assembly SHA256 71dff4614df099b2d372e52e9a2b0fc22ec894d57e294eca8d3f9c81f1c71e4d
# Literal successful primary iteration; no call or miss/overflow bodies.
# Branches are instructions only; LLVM-MCA does not simulate their prediction.
# LLVM-MCA-BEGIN materialized-p
	movq	(%r10), %rsi
	movl	(%r14,%rbx,4), %r13d
	movabsq	$-0x61c8864e7a143579, %rax
	movq	0x70(%rsi), %rdx
	movq	0x10(%rsi), %rdi
	xorq	%r13, %rdx
	mulxq	%rax, %rax, %rcx
	movl	%eax, %edx
	shrl	$0x17, %edx
	movl	%eax, %r9d
	andl	$0x7fffff, %r9d
	imulq	$0x440, %rdx, %r11
	vpbroadcastd	%r9d, %ymm2
	vmovdqu	(%rdi,%r11), %xmm0
	vmovdqu	0x18(%rdi,%r11), %xmm1
	leaq	(%rdi,%r11), %rdx
	vinserti128	$0x1, 0x8(%rdi,%r11), %ymm0, %ymm0
	vpshufb	%ymm3, %ymm0, %ymm0
	vpcmpeqd	%ymm0, %ymm2, %k0
	vinserti128	$0x1, 0x20(%rdi,%r11), %ymm1, %ymm0
	vpshufb	%ymm3, %ymm0, %ymm0
	vpcmpeqd	%ymm0, %ymm2, %k1
	kunpckbw	%k0, %k1, %k0
	kortestw	%k0, %k0
	je .Ltarget_100d0
	kmovd	%k0, %eax
	tzcntl	%eax, %eax
	shll	$0x6, %eax
	vmovdqu64	0x40(%rdx,%rax), %zmm0
	vextracti64x4	$0x1, %zmm0, %ymm1
	incq	%rbp
	vpaddq	%zmm1, %zmm0, %zmm0
	vextracti128	$0x1, %ymm0, %xmm1
	vpaddq	%xmm1, %xmm0, %xmm0
	vpshufd	$0xee, %xmm0, %xmm1
	vpaddq	%xmm1, %xmm0, %xmm0
	vmovq	%xmm0, %rax
	addq	%rax, %r12
	addq	%r13, %r15
	incq	%rbx
	cmpq	%rbx, %r8
	jne .Ltarget_fff5
# LLVM-MCA-END
.Ltarget_100d0:
.Ltarget_fff5:
