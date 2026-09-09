00000000000000d0 <raw512>:
      d0:      	vmovups	(%rdi), %zmm0
      d6:      	retq
      d7:      	nopw	(%rax,%rax)

0000000000000100 <expected512>:
     100:      	movq	%rdi, %rax
     103:      	testl	%edx, %edx
     105:      	je	0x11c <expected512+0x1c>
     107:      	vmovups	(%rsi), %zmm0
     10d:      	vmovaps	%zmm0, (%rax)
     113:      	movb	$0x1, %cl
     115:      	movb	%cl, 0x40(%rax)
     118:      	vzeroupper
     11b:      	retq
     11c:      	movb	$0x3, (%rax)
     11f:      	xorl	%ecx, %ecx
     121:      	movb	%cl, 0x40(%rax)
     124:      	retq
     125:      	nopw	%cs:(%rax,%rax)

0000000000000230 <call_expected512>:
     230:      	pushq	%rbp
     231:      	movq	%rsp, %rbp
     234:      	pushq	%rbx
     235:      	andq	$-0x40, %rsp
     239:      	subq	$0xc0, %rsp
     240:      	movq	%rdx, %rbx
     243:      	movl	%esi, %edx
     245:      	movq	%rdi, %rsi
     248:      	movq	%rsp, %rdi
     24b:      	callq	0x250 <call_expected512+0x20>
		000000000000024c:  R_X86_64_PLT32	expected512-0x4
     250:      	cmpb	$0x0, 0x40(%rsp)
     255:      	je	0x270 <call_expected512+0x40>
     257:      	vmovaps	(%rsp), %zmm0
     25e:      	vmovups	%zmm0, (%rbx)
     264:      	xorl	%eax, %eax
     266:      	leaq	-0x8(%rbp), %rsp
     26a:      	popq	%rbx
     26b:      	popq	%rbp
     26c:      	vzeroupper
     26f:      	retq
     270:      	movzbl	(%rsp), %eax
     274:      	leaq	-0x8(%rbp), %rsp
     278:      	popq	%rbx
     279:      	popq	%rbp
     27a:      	retq
     27b:      	nopl	(%rax,%rax)

00000000000001a0 <__regcall3__reg_tagged128>:
     1a0:      	addq	$0x3, %rcx
     1a4:      	vmovups	(%rax), %xmm0
     1a8:      	movq	%rcx, %rax
     1ab:      	retq
     1ac:      	nopl	(%rax)

00000000000001b0 <__regcall3__reg_expected128>:
     1b0:      	testl	%edx, %edx
     1b2:      	je	0x1c2 <__regcall3__reg_expected128+0x12>
     1b4:      	vmovups	(%rcx), %xmm0
     1b8:      	movb	$0x1, %cl
     1ba:      	movb	%cl, 0x10(%rax)
     1bd:      	vmovups	%xmm0, (%rax)
     1c1:      	retq
     1c2:      	vmovaps	(%rip), %xmm0           # 0x1ca <__regcall3__reg_expected128+0x1a>
		00000000000001c6:  R_X86_64_PC32	.LCPI16_0-0x4
     1ca:      	xorl	%ecx, %ecx
     1cc:      	movb	%cl, 0x10(%rax)
     1cf:      	vmovups	%xmm0, (%rax)
     1d3:      	retq
     1d4:      	nopw	%cs:(%rax,%rax)

0000000000000350 <call_reg_expected128>:
     350:      	pushq	%rbx
     351:      	subq	$0x20, %rsp
     355:      	movq	%rdx, %rbx
     358:      	movl	%esi, %edx
     35a:      	movq	%rdi, %rcx
     35d:      	movq	%rsp, %rax
     360:      	callq	*(%rip)                 # 0x366 <call_reg_expected128+0x16>
		0000000000000362:  R_X86_64_GOTPCRELX	__regcall3__reg_expected128-0x4
     366:      	vmovdqu	(%rsp), %xmm0
     36b:      	testb	$0x1, 0x10(%rsp)
     370:      	je	0x37e <call_reg_expected128+0x2e>
     372:      	vmovdqu	%xmm0, (%rbx)
     376:      	xorl	%eax, %eax
     378:      	addq	$0x20, %rsp
     37c:      	popq	%rbx
     37d:      	retq
     37e:      	vpextrb	$0x0, %xmm0, %eax
     384:      	addq	$0x20, %rsp
     388:      	popq	%rbx
     389:      	retq
     38a:      	nopw	(%rax,%rax)

0000000000000250 <__regcall3__reg_union128>:
     250:      	testl	%ecx, %ecx
     252:      	je	0x25b <__regcall3__reg_union128+0xb>
     254:      	vmovups	(%rax), %xmm0
     258:      	movb	$0x1, %al
     25a:      	retq
     25b:      	vmovaps	(%rip), %xmm0           # 0x263 <__regcall3__reg_union128+0x13>
		000000000000025f:  R_X86_64_PC32	.LCPI21_0-0x4
     263:      	xorl	%eax, %eax
     265:      	retq

0000000000000290 <inline_raw512>:
     290:      	vmovups	(%rdi), %zmm0
     296:      	vmovups	%zmm0, (%rsi)
     29c:      	vzeroupper
     29f:      	retq

00000000000002a0 <inline_expected_success512>:
     2a0:      	vmovups	(%rdi), %zmm0
     2a6:      	vmovups	%zmm0, (%rsi)
     2ac:      	vzeroupper
     2af:      	retq

00000000000002b0 <inline_expected_dynamic512>:
     2b0:      	movl	$0x3, %eax
     2b5:      	testl	%esi, %esi
     2b7:      	je	0x2c7 <inline_expected_dynamic512+0x17>
     2b9:      	vmovups	(%rdi), %zmm0
     2bf:      	vmovups	%zmm0, (%rdx)
     2c5:      	xorl	%eax, %eax
     2c7:      	vzeroupper
     2ca:      	retq
     2cb:      	nopl	(%rax,%rax)
