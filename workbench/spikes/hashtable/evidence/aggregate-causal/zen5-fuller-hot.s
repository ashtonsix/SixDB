# Actual primary/first-group hit, first candidate matches; repeated operation body.
# Branch directions linearized, branch instructions preserved.
# LLVM-MCA-BEGIN zen5-fuller-hot
movq	(%r13), %rcx
movq	0x40(%rdi), %rsi
movq	0x48(%rdi), %rdx
movq	%rax, 0x18(%rsp)
movq	0x8(%r13), %rax
xorq	%rcx, %rsi
movq	%rax, 0x10(%rsp)
movq	%rcx, 0x30(%rsp)
bzhiq	%r11, %rsi, %rbp
movq	%rsi, 0x68(%rsp)
movq	%rbp, %r12
shrq	$0x8, %r12
shrq	$0x10, %rbp
xorq	%r12, %rdx
mulxq	%r14, %rcx, %rax
movq	0x50(%rdi), %rdx
xorl	%esi, %ecx
movq	0x8(%rdi), %rsi
xorl	%eax, %ecx
xorq	%rbp, %rdx
movzbl	%cl, %ecx
mulxq	%r14, %rdx, %rax
shll	$0x7, %ecx
leaq	(%rsi,%rcx), %rbx
xorl	%edx, %r12d
xorl	%eax, %r12d
leaq	0x5(%rsi,%rcx), %rax
vmovd	%r12d, %xmm0
vpbroadcastb	%xmm0, %xmm0
vpcmpeqb	0x5(%rsi,%rcx), %xmm0, %xmm0
movq	%rax, 0x60(%rsp)
movzbl	(%rsi,%rcx), %eax
vpmovmskb	%xmm0, %ecx
bzhil	%eax, %ecx, %ecx
testq	%rcx, %rcx
je	.Ledge
tzcntq	%rcx, %rdx
leaq	(%rdx,%rdx,2), %rsi
btrq	%rdx, %rcx
movzwl	0x15(%rbx,%rsi), %r8d
movzbl	0x17(%rbx,%rsi), %esi
shll	$0x10, %esi
orl	%r8d, %esi
cmpl	%ebp, %esi
jne	.Ledge
jmp	.Ledge
movq	0x10(%rsp), %rax
addq	%rax, 0x30(%rbx,%rdx,8)
movq	0x18(%rsp), %rax
xorl	%ecx, %ecx
addq	%rcx, %rax
addq	$0x10, %r13
cmpq	%r15, %r13
je	.Ledge
# LLVM-MCA-END
.Ledge:
