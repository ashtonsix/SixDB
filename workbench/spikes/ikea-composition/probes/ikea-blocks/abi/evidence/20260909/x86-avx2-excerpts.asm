0000000000000060 <raw256>:
      60:      	vmovups	(%rdi), %ymm0
      64:      	retq
      65:      	nopw	%cs:(%rax,%rax)

0000000000000110 <expected512>:
     110:      	movq	%rdi, %rax
     113:      	testl	%edx, %edx
     115:      	je	0x132 <expected512+0x22>
     117:      	vmovups	(%rsi), %ymm0
     11b:      	vmovups	0x20(%rsi), %ymm1
     120:      	vmovaps	%ymm1, 0x20(%rax)
     125:      	vmovaps	%ymm0, (%rax)
     129:      	movb	$0x1, %cl
     12b:      	movb	%cl, 0x40(%rax)
     12e:      	vzeroupper
     131:      	retq
     132:      	movb	$0x3, (%rax)
     135:      	xorl	%ecx, %ecx
     137:      	movb	%cl, 0x40(%rax)
     13a:      	retq
     13b:      	nopl	(%rax,%rax)

0000000000000240 <call_expected512>:
     240:      	pushq	%rbp
     241:      	movq	%rsp, %rbp
     244:      	pushq	%rbx
     245:      	andq	$-0x40, %rsp
     249:      	subq	$0xc0, %rsp
     250:      	movq	%rdx, %rbx
     253:      	movl	%esi, %edx
     255:      	movq	%rdi, %rsi
     258:      	movq	%rsp, %rdi
     25b:      	callq	0x260 <call_expected512+0x20>
		000000000000025c:  R_X86_64_PLT32	expected512-0x4
     260:      	cmpb	$0x0, 0x40(%rsp)
     265:      	je	0x287 <call_expected512+0x47>
     267:      	vmovaps	(%rsp), %ymm0
     26c:      	vmovaps	0x20(%rsp), %ymm1
     272:      	vmovups	%ymm1, 0x20(%rbx)
     277:      	vmovups	%ymm0, (%rbx)
     27b:      	xorl	%eax, %eax
     27d:      	leaq	-0x8(%rbp), %rsp
     281:      	popq	%rbx
     282:      	popq	%rbp
     283:      	vzeroupper
     286:      	retq
     287:      	movzbl	(%rsp), %eax
     28b:      	leaq	-0x8(%rbp), %rsp
     28f:      	popq	%rbx
     290:      	popq	%rbp
     291:      	retq
     292:      	nopw	%cs:(%rax,%rax)

0000000000000140 <cps512>:
     140:      	pushq	%rbp
     141:      	movq	%rsp, %rbp
     144:      	andq	$-0x40, %rsp
     148:      	subq	$0xc0, %rsp
     14f:      	addq	$0x3, %rsi
     153:      	vmovups	(%rdi), %ymm0
     157:      	vmovups	0x20(%rdi), %ymm1
     15c:      	vmovaps	%ymm1, 0x60(%rsp)
     162:      	vmovaps	%ymm0, 0x40(%rsp)
     168:      	vmovaps	0x40(%rsp), %ymm0
     16e:      	vmovaps	0x60(%rsp), %ymm1
     174:      	vmovaps	%ymm1, 0x20(%rsp)
     17a:      	vmovaps	%ymm0, (%rsp)
     17f:      	movq	%rdx, %rdi
     182:      	vzeroupper
     185:      	callq	*%rcx
     187:      	movq	%rbp, %rsp
     18a:      	popq	%rbp
     18b:      	retq
     18c:      	nopl	(%rax)
