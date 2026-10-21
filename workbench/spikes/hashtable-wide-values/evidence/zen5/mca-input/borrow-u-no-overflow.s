.text
# Source assembly SHA256 71dff4614df099b2d372e52e9a2b0fc22ec894d57e294eca8d3f9c81f1c71e4d
# Literal successful primary iteration; no call or miss/overflow bodies.
# Branches are instructions only; LLVM-MCA does not simulate their prediction.
# LLVM-MCA-BEGIN borrow-u-no-overflow
	movq	0x20(%rsp), %rax
	movq	%r11, %rdx
	movl	(%rax,%rbp,4), %r12d
	movabsq	$-0x61c8864e7a143579, %rax
	xorq	%r12, %rdx
	mulxq	%rax, %rcx, %rdx
	movl	%ecx, %eax
	shrl	$0x17, %eax
	movl	%ecx, %r9d
	andl	$0x7fffff, %r9d
	imulq	$0x440, %rax, %rdi
	vpbroadcastd	%r9d, %ymm2
	vmovdqu	(%r13,%rdi), %xmm0
	vmovdqu	0x18(%r13,%rdi), %xmm1
	movzbl	0x3f(%r13,%rdi), %esi
	leaq	(%r13,%rdi), %rax
	vinserti128	$0x1, 0x8(%r13,%rdi), %ymm0, %ymm0
	movzbl	%sil, %r10d
	vpshufb	%ymm3, %ymm0, %ymm0
	vpcmpeqd	%ymm0, %ymm2, %k0
	vinserti128	$0x1, 0x20(%r13,%rdi), %ymm1, %ymm0
	vpshufb	%ymm3, %ymm0, %ymm0
	vpcmpeqd	%ymm0, %ymm2, %k1
	kunpckbw	%k0, %k1, %k0
	kmovw	%k0, %edi
	testl	%r10d, %r10d
	je .Ltarget_30b20
	tzcntq	%rdi, %rcx
	shll	$0x6, %ecx
	leaq	0x40(%rax,%rcx), %rax
	leaq	0x14e4c(%rip), %rcx
	testq	%rdi, %rdi
	cmoveq	%rcx, %rax
	testq	%rdi, %rdi
	setne	%cl
	vmovdqu	0x20(%rax), %ymm0
	vpaddq	(%rax), %zmm0, %zmm0
	vextracti128	$0x1, %ymm0, %xmm1
	vpaddq	%xmm1, %xmm0, %xmm0
	vpshufd	$0xee, %xmm0, %xmm1
	vpaddq	%xmm1, %xmm0, %xmm0
	vmovq	%xmm0, %rax
	addq	%rax, %r14
	movzbl	%cl, %eax
	addq	%rax, %r15
	testb	%al, %al
	movl	$0x0, %eax
	cmoveq	%rax, %r12
	incq	%rbp
	addq	%r12, %rbx
	cmpq	%rbp, %r8
	je .Ltarget_30c83
# LLVM-MCA-END
.Ltarget_30b20:
.Ltarget_30c83:
