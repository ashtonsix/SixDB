# Actual primary/first-group hit, first candidate matches; repeated operation body.
# Branch directions linearized, branch instructions preserved.
# LLVM-MCA-BEGIN zen5-abseil
movq	(%r14), %rax
movq	0x8(%r14), %rbp
movq	%rax, (%rsp)
movq	(%r13), %rdx
testb	$0x3e, %dl
je	.Ledge
movq	$-0x1, %rcx
movq	0x28(%r15), %rdi
xorl	%r9d, %r9d
shlxq	%rdx, %rcx, %r10
andl	$0x7c0, %edx
movabsq	$0x79d5f9e0de1e8cf5, %rcx
xorq	%rax, %rdx
mulxq	%rcx, %rcx, %rdx
prefetcht2	(%rdi)
movq	%rdi, %rsi
subq	%r10, %rsi
notq	%r10
addq	$0xf, %rsi
xorq	%rcx, %rdx
movq	%rdx, %rcx
shrq	$0x39, %rcx
movq	%rdx, %r8
vmovd	%ecx, %xmm0
vpbroadcastb	%xmm0, %xmm0
andq	%r10, %r8
movq	%r8, %rcx
shlq	$0x4, %rcx
prefetcht0	(%rsi,%rcx)
vmovdqu	(%rdi,%r8), %xmm1
vpcmpeqb	%xmm1, %xmm0, %xmm2
vpmovmskb	%xmm2, %ecx
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
