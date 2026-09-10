0000000000000000 <ikea_comp_load>:
       0:      	vmovups	(%rsi), %ymm0
       4:      	movq	(%rdi), %rax
       7:      	addq	$0x8, %rdi
       b:      	jmpq	*%rax

0000000000000000 <ikea_comp_features>:
       0:      	vbroadcasti128	(%rip), %ymm2   # ymm2 = mem[0,1,0,1]
                                                # 0x9 <ikea_comp_features+0x9>
		0000000000000005:  R_X86_64_PC32	.LCPI0_1-0x4
       9:      	vpopcntb	%ymm0, %ymm1
       f:      	vpxor	%xmm3, %xmm3, %xmm3
      13:      	movl	$0x80, %r9d
      19:      	vpshufb	%ymm1, %ymm2, %ymm2
      1e:      	vpsadbw	%ymm3, %ymm1, %ymm1
      22:      	vextracti128	$0x1, %ymm1, %xmm4
      28:      	vpaddq	%xmm4, %xmm1, %xmm1
      2c:      	vpshufd	$0xee, %xmm1, %xmm4     # xmm4 = xmm1[2,3,2,3]
      31:      	vpaddq	%xmm4, %xmm1, %xmm1
      35:      	vmovd	%xmm1, %eax
      39:      	vpsadbw	%ymm3, %ymm2, %ymm1
      3d:      	leal	-0x80(%rax), %r8d
      41:      	subl	%eax, %r9d
      44:      	movq	(%rdi), %rax
      47:      	cmovbl	%r8d, %r9d
      4b:      	addq	$0x8, %rdi
      4f:      	vextracti128	$0x1, %ymm1, %xmm2
      55:      	vpaddq	%xmm2, %xmm1, %xmm1
      59:      	vpshufd	$0xee, %xmm1, %xmm2     # xmm2 = xmm1[2,3,2,3]
      5e:      	vpaddq	%xmm2, %xmm1, %xmm1
      62:      	vmovd	%xmm1, %ecx
      66:      	shlq	$0x10, %rcx
      6a:      	orq	%r9, %rcx
      6d:      	jmpq	*%rax
      6f:      	nop

0000000000000070 <ikea_comp_load_features>:
      70:      	vmovdqu	(%rsi), %ymm0
      74:      	vbroadcasti128	(%rip), %ymm2   # ymm2 = mem[0,1,0,1]
                                                # 0x7d <ikea_comp_load_features+0xd>
		0000000000000079:  R_X86_64_PC32	.LCPI1_1-0x4
      7d:      	vpxor	%xmm3, %xmm3, %xmm3
      81:      	movl	$0x80, %r9d
      87:      	vpopcntb	%ymm0, %ymm1
      8d:      	vpshufb	%ymm1, %ymm2, %ymm2
      92:      	vpsadbw	%ymm3, %ymm1, %ymm1
      96:      	vextracti128	$0x1, %ymm1, %xmm4
      9c:      	vpaddq	%xmm4, %xmm1, %xmm1
      a0:      	vpshufd	$0xee, %xmm1, %xmm4     # xmm4 = xmm1[2,3,2,3]
      a5:      	vpaddq	%xmm4, %xmm1, %xmm1
      a9:      	vmovd	%xmm1, %eax
      ad:      	vpsadbw	%ymm3, %ymm2, %ymm1
      b1:      	leal	-0x80(%rax), %r8d
      b5:      	subl	%eax, %r9d
      b8:      	movq	(%rdi), %rax
      bb:      	cmovbl	%r8d, %r9d
      bf:      	addq	$0x8, %rdi
      c3:      	vextracti128	$0x1, %ymm1, %xmm2
      c9:      	vpaddq	%xmm2, %xmm1, %xmm1
      cd:      	vpshufd	$0xee, %xmm1, %xmm2     # xmm2 = xmm1[2,3,2,3]
      d2:      	vpaddq	%xmm2, %xmm1, %xmm1
      d6:      	vmovd	%xmm1, %ecx
      da:      	shlq	$0x10, %rcx
      de:      	orq	%r9, %rcx
      e1:      	jmpq	*%rax
      e3:      	nopw	%cs:(%rax,%rax)

0000000000000000 <ikea_comp_model>:
       0:      	movzbl	%cl, %eax
       3:      	cmpl	$0x80, %eax
       8:      	jne	0x15 <ikea_comp_model+0x15>
       a:      	xorl	%ecx, %ecx
       c:      	movq	(%rdi), %rax
       f:      	addq	$0x8, %rdi
      13:      	jmpq	*%rax
      15:      	movl	$0x80, %r10d
      1b:      	movl	$0x810, %r8d            # imm = 0x810
      21:      	xorl	%r9d, %r9d
      24:      	subl	%eax, %r10d
      27:      	bextrl	%r8d, %ecx, %ecx
      2c:      	cmpl	$0x9, %r10d
      30:      	setae	%r9b
      34:      	cmpl	$0x21, %r10d
      38:      	sbbq	$-0x1, %r9
      3c:      	cmpl	$0x61, %r10d
      40:      	sbbq	$-0x1, %r9
      44:      	leaq	(%r9,%r9,2), %r8
      48:      	leaq	(%rip), %r9             # 0x4f <ikea_comp_model+0x4f>
		000000000000004b:  R_X86_64_PC32	_ZN4ikea13bec_predictor12coefficientsE-0x4
      4f:      	movl	%r8d, %r8d
      52:      	imull	0x4(%r9,%r8,4), %eax
      58:      	imull	0x8(%r9,%r8,4), %ecx
      5e:      	addl	(%r9,%r8,4), %eax
      62:      	leal	0x800(%rcx,%rax), %eax
      69:      	movl	%eax, %ecx
      6b:      	sarl	$0xc, %ecx
      6e:      	sarl	$0x1f, %eax
      71:      	andnl	%ecx, %eax, %eax
      76:      	movl	$0x2f, %ecx
      7b:      	cmpl	$0x2f, %eax
      7e:      	cmovll	%eax, %ecx
      81:      	movq	(%rdi), %rax
      84:      	addq	$0x8, %rdi
      88:      	jmpq	*%rax
      8a:      	nopw	(%rax,%rax)

0000000000000090 <ikea_comp_transition_model_stage>:
      90:      	movzbl	%cl, %eax
      93:      	cmpl	$0x80, %eax
      98:      	jne	0xa5 <ikea_comp_transition_model_stage+0x15>
      9a:      	xorl	%ecx, %ecx
      9c:      	movq	(%rdi), %rax
      9f:      	addq	$0x8, %rdi
      a3:      	jmpq	*%rax
      a5:      	pushq	%rbx
      a6:      	movl	$0x810, %r8d            # imm = 0x810
      ac:      	movzbl	%ch, %ebx
      af:      	bextrl	%r8d, %ecx, %ecx
      b4:      	je	0xdc <ikea_comp_transition_model_stage+0x4c>
      b6:      	movl	$0x80, %r9d
      bc:      	xorl	%r8d, %r8d
      bf:      	subl	%eax, %r9d
      c2:      	cmpl	$0x9, %r9d
      c6:      	setae	%r8b
      ca:      	cmpl	$0x21, %r9d
      ce:      	sbbq	$-0x1, %r8
      d2:      	cmpl	$0x61, %r9d
      d6:      	sbbq	$-0x1, %r8
      da:      	jmp	0xe2 <ikea_comp_transition_model_stage+0x52>
      dc:      	movl	$0x4, %r8d
      e2:      	leaq	(%r8,%r8,4), %r8
      e6:      	leaq	(%rip), %r9             # 0xed <ikea_comp_transition_model_stage+0x5d>
		00000000000000e9:  R_X86_64_PC32	_ZN4ikea13bec_predictor23transition_coefficientsE-0x4
      ed:      	imull	0x4(%r9,%r8,4), %eax
      f3:      	imull	0x8(%r9,%r8,4), %ebx
      f9:      	imull	0xc(%r9,%r8,4), %ecx
      ff:      	addl	(%r9,%r8,4), %eax
     103:      	addl	%ebx, %eax
     105:      	leal	0x800(%rcx,%rax), %eax
     10c:      	movl	%eax, %ecx
     10e:      	sarl	$0xc, %ecx
     111:      	sarl	$0x1f, %eax
     114:      	andnl	%ecx, %eax, %eax
     119:      	movl	$0x2f, %ecx
     11e:      	cmpl	$0x2f, %eax
     121:      	cmovll	%eax, %ecx
     124:      	popq	%rbx
     125:      	movq	(%rdi), %rax
     128:      	addq	$0x8, %rdi
     12c:      	jmpq	*%rax

0000000000000000 <ikea_comp_done>:
       0:      	leaq	(%rdx,%rcx), %rax
       4:      	retq
