# Actual primary/first-group hit, first candidate matches; repeated operation body.
# Branch directions linearized, branch instructions preserved.
# LLVM-MCA-BEGIN granite-fuller-hot
movq	%rax, 0x18(%rsp)
movq	(%r13), %rcx
movq	0x8(%r13), %rax
movq	%rax, 0x10(%rsp)
movq	0x40(%rdi), %rsi
movq	%rcx, 0x30(%rsp)
xorq	%rcx, %rsi
bzhiq	%r11, %rsi, %rbp
movq	%rbp, %r12
shrq	$0x8, %r12
shrq	$0x10, %rbp
movq	0x48(%rdi), %rdx
xorq	%r12, %rdx
mulxq	%r14, %rcx, %rax
movq	%rsi, 0x68(%rsp)
xorl	%esi, %eax
xorl	%ecx, %eax
movq	0x50(%rdi), %rdx
xorq	%rbp, %rdx
mulxq	%r14, %rdx, %rcx
xorl	%ecx, %r12d
xorl	%edx, %r12d
movq	0x8(%rdi), %rcx
movzbl	%al, %edx
shll	$0x7, %edx
leaq	(%rcx,%rdx), %rbx
movzbl	(%rcx,%rdx), %eax
leaq	0x5(%rcx,%rdx), %rsi
movq	%rsi, 0x60(%rsp)
vpbroadcastb	%r12d, %xmm0
vpcmpeqb	0x5(%rcx,%rdx), %xmm0, %k0
kmovw	%k0, %ecx
bzhil	%eax, %ecx, %ecx
testq	%rcx, %rcx
je	.Ledge
tzcntq	%rcx, %rdx
btrq	%rdx, %rcx
leaq	(%rdx,%rdx,2), %rsi
movzwl	0x15(%rbx,%rsi), %r8d
movzbl	0x17(%rbx,%rsi), %esi
shll	$0x10, %esi
orl	%r8d, %esi
cmpl	%ebp, %esi
jne	.Ledge
jmp	.Ledge
movq	0x10(%rsp), %rax
addq	%rax, 0x30(%rbx,%rdx,8)
xorl	%ecx, %ecx
movq	0x18(%rsp), %rax
addq	%rcx, %rax
addq	$0x10, %r13
cmpq	%r15, %r13
je	.Ledge
# LLVM-MCA-END
.Ledge:
