.text
# Source assembly SHA256 71dff4614df099b2d372e52e9a2b0fc22ec894d57e294eca8d3f9c81f1c71e4d
# Literal successful primary iteration; no call or miss/overflow bodies.
# Branches are instructions only; LLVM-MCA does not simulate their prediction.
# LLVM-MCA-BEGIN borrow-p
	movl	(%r11,%r14,4), %r12d
	movq	%r10, %rdx
	xorq	%r12, %rdx
	mulxq	%rbx, %rax, %rdx
	movl	%eax, %ecx
	shrl	$0x17, %ecx
	movl	%eax, %edi
	andl	$0x7fffff, %edi
	imulq	$0x440, %rcx, %rsi
	vpbroadcastd	%edi, %ymm2
	vmovdqu	(%r13,%rsi), %xmm0
	vmovdqu	0x18(%r13,%rsi), %xmm1
	leaq	(%r13,%rsi), %rcx
	vinserti128	$0x1, 0x8(%r13,%rsi), %ymm0, %ymm0
	vpshufb	%ymm3, %ymm0, %ymm0
	vpcmpeqd	%ymm0, %ymm2, %k0
	vinserti128	$0x1, 0x20(%r13,%rsi), %ymm1, %ymm0
	vpshufb	%ymm3, %ymm0, %ymm0
	vpcmpeqd	%ymm0, %ymm2, %k1
	kunpckbw	%k0, %k1, %k0
	kortestw	%k0, %k0
	je .Ltarget_30a30
	kmovd	%k0, %eax
	tzcntl	%eax, %eax
	shll	$0x6, %eax
	leaq	0x40(%rcx,%rax), %rax
	vmovdqu	0x20(%rax), %ymm0
	incq	%r9
	vpaddq	(%rax), %zmm0, %zmm0
	vextracti128	$0x1, %ymm0, %xmm1
	vpaddq	%xmm1, %xmm0, %xmm0
	vpshufd	$0xee, %xmm0, %xmm1
	vpaddq	%xmm1, %xmm0, %xmm0
	vmovq	%xmm0, %rax
	addq	%rax, %rbp
	jmp .Ltarget_30973
# LLVM-MCA-END
.Ltarget_30973:
.Ltarget_30a30:
