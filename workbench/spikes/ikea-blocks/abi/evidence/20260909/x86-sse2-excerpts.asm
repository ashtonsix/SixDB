0000000000000000 <raw128>:
       0:      	movups	(%rdi), %xmm0
       3:      	retq
       4:      	nopw	%cs:(%rax,%rax)

0000000000000060 <raw256>:
      60:      	movups	(%rdi), %xmm0
      63:      	movups	0x10(%rdi), %xmm1
      67:      	retq
      68:      	nopl	(%rax,%rax)

00000000000000c0 <cps256>:
      c0:      	pushq	%rbp
      c1:      	movq	%rsp, %rbp
      c4:      	andq	$-0x20, %rsp
      c8:      	subq	$0x60, %rsp
      cc:      	addq	$0x3, %rsi
      d0:      	movups	(%rdi), %xmm0
      d3:      	movups	0x10(%rdi), %xmm1
      d7:      	movaps	%xmm1, 0x30(%rsp)
      dc:      	movaps	%xmm0, 0x20(%rsp)
      e1:      	movaps	0x20(%rsp), %xmm0
      e6:      	movaps	0x30(%rsp), %xmm1
      eb:      	movaps	%xmm1, 0x10(%rsp)
      f0:      	movaps	%xmm0, (%rsp)
      f4:      	movq	%rdx, %rdi
      f7:      	callq	*%rcx
      f9:      	movq	%rbp, %rsp
      fc:      	popq	%rbp
      fd:      	retq
      fe:      	nop
