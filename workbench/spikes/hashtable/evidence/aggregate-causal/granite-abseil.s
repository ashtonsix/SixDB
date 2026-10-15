# Actual primary/first-group hit, first candidate matches; repeated operation body.
# Branch directions linearized, branch instructions preserved.
# LLVM-MCA-BEGIN granite-abseil
movq	(%r14), %rax
movq	0x8(%r14), %rbp
movq	%rax, (%rsp)
movq	(%r13), %rdx
testb	$0x3e, %dl
je	.Ledge
movq	0x28(%r15), %rdi
movq	$-0x1, %rcx
shlxq	%rdx, %rcx, %r10
movq	%rdi, %rsi
subq	%r10, %rsi
addq	$0xf, %rsi
prefetcht2	(%rdi)
notq	%r10
andl	$0x7c0, %edx
xorq	%rax, %rdx
movabsq	$0x79d5f9e0de1e8cf5, %rcx
mulxq	%rcx, %rcx, %rdx
xorq	%rcx, %rdx
movq	%rdx, %rcx
shrq	$0x39, %rcx
vpbroadcastb	%ecx, %xmm0
movq	%rdx, %r8
xorl	%r9d, %r9d
andq	%r10, %r8
movq	%r8, %rcx
shlq	$0x4, %rcx
prefetcht0	(%rsi,%rcx)
vmovdqu	(%rdi,%r8), %xmm1
vpcmpeqb	%xmm1, %xmm0, %k0
kmovw	%k0, %ecx
testl	%ecx, %ecx
je	.Ledge
tzcntl	%ecx, %r11d
addq	%r8, %r11
andq	%r10, %r11
shlq	$0x4, %r11
cmpq	%rax, (%rsi,%r11)
je	.Ledge
addq	%r11, %rsi
addq	%rbp, 0x8(%rsi)
xorl	%eax, %eax
jmp	.Ledge
addq	%rax, %r12
addq	$0x10, %r14
cmpq	%rbx, %r14
je	.Ledge
# LLVM-MCA-END
.Ledge:
