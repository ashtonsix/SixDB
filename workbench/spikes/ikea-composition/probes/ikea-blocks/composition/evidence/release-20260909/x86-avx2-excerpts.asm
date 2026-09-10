0000000000000000 <ikea_comp_load>:
       0:      	vmovups	(%rsi), %ymm0
       4:      	movq	(%rdi), %rax
       7:      	addq	$0x8, %rdi
       b:      	jmpq	*%rax

0000000000000000 <ikea_comp_features>:
       0:      	vpbroadcastd	(%rip), %ymm1   # 0x9 <ikea_comp_features+0x9>
		0000000000000005:  R_X86_64_PC32	.LCPI0_3-0x4
       9:      	vpand	%ymm1, %ymm0, %ymm2
       d:      	vbroadcasti128	(%rip), %ymm3   # ymm3 = mem[0,1,0,1]
                                                # 0x16 <ikea_comp_features+0x16>
		0000000000000012:  R_X86_64_PC32	.LCPI0_4-0x4
      16:      	vpshufb	%ymm2, %ymm3, %ymm2
      1b:      	vpsrlw	$0x4, %ymm0, %ymm4
      20:      	vpand	%ymm1, %ymm4, %ymm1
      24:      	vpshufb	%ymm1, %ymm3, %ymm1
      29:      	vpaddb	%ymm2, %ymm1, %ymm1
      2d:      	vbroadcasti128	(%rip), %ymm2   # ymm2 = mem[0,1,0,1]
                                                # 0x36 <ikea_comp_features+0x36>
		0000000000000032:  R_X86_64_PC32	.LCPI0_5-0x4
      36:      	vpshufb	%ymm1, %ymm2, %ymm2
      3b:      	vpxor	%xmm3, %xmm3, %xmm3
      3f:      	vpsadbw	%ymm3, %ymm1, %ymm1
      43:      	vextracti128	$0x1, %ymm1, %xmm4
      49:      	vpaddq	%xmm4, %xmm1, %xmm1
      4d:      	vpshufd	$0xee, %xmm1, %xmm4     # xmm4 = xmm1[2,3,2,3]
      52:      	vpaddq	%xmm4, %xmm1, %xmm1
      56:      	vmovd	%xmm1, %eax
      5a:      	vpsadbw	%ymm3, %ymm2, %ymm1
      5e:      	vextracti128	$0x1, %ymm1, %xmm2
      64:      	vpaddq	%xmm2, %xmm1, %xmm1
      68:      	vpshufd	$0xee, %xmm1, %xmm2     # xmm2 = xmm1[2,3,2,3]
      6d:      	vpaddq	%xmm2, %xmm1, %xmm1
      71:      	vmovd	%xmm1, %ecx
      75:      	leal	-0x80(%rax), %r8d
      79:      	movl	$0x80, %r9d
      7f:      	subl	%eax, %r9d
      82:      	cmovbl	%r8d, %r9d
      86:      	shlq	$0x10, %rcx
      8a:      	orq	%r9, %rcx
      8d:      	movq	(%rdi), %rax
      90:      	addq	$0x8, %rdi
      94:      	jmpq	*%rax
      96:      	nopw	%cs:(%rax,%rax)

00000000000000a0 <ikea_comp_load_features>:
      a0:      	vmovdqu	(%rsi), %ymm0
      a4:      	vpbroadcastd	(%rip), %ymm1   # 0xad <ikea_comp_load_features+0xd>
		00000000000000a9:  R_X86_64_PC32	.LCPI1_3-0x4
      ad:      	vpand	%ymm1, %ymm0, %ymm2
      b1:      	vbroadcasti128	(%rip), %ymm3   # ymm3 = mem[0,1,0,1]
                                                # 0xba <ikea_comp_load_features+0x1a>
		00000000000000b6:  R_X86_64_PC32	.LCPI1_4-0x4
      ba:      	vpshufb	%ymm2, %ymm3, %ymm2
      bf:      	vpsrlw	$0x4, %ymm0, %ymm4
      c4:      	vpand	%ymm1, %ymm4, %ymm1
      c8:      	vpshufb	%ymm1, %ymm3, %ymm1
      cd:      	vpaddb	%ymm2, %ymm1, %ymm1
      d1:      	vbroadcasti128	(%rip), %ymm2   # ymm2 = mem[0,1,0,1]
                                                # 0xda <ikea_comp_load_features+0x3a>
		00000000000000d6:  R_X86_64_PC32	.LCPI1_5-0x4
      da:      	vpshufb	%ymm1, %ymm2, %ymm2
      df:      	vpxor	%xmm3, %xmm3, %xmm3
      e3:      	vpsadbw	%ymm3, %ymm1, %ymm1
      e7:      	vextracti128	$0x1, %ymm1, %xmm4
      ed:      	vpaddq	%xmm4, %xmm1, %xmm1
      f1:      	vpshufd	$0xee, %xmm1, %xmm4     # xmm4 = xmm1[2,3,2,3]
      f6:      	vpaddq	%xmm4, %xmm1, %xmm1
      fa:      	vmovd	%xmm1, %eax
      fe:      	vpsadbw	%ymm3, %ymm2, %ymm1
     102:      	vextracti128	$0x1, %ymm1, %xmm2
     108:      	vpaddq	%xmm2, %xmm1, %xmm1
     10c:      	vpshufd	$0xee, %xmm1, %xmm2     # xmm2 = xmm1[2,3,2,3]
     111:      	vpaddq	%xmm2, %xmm1, %xmm1
     115:      	vmovd	%xmm1, %ecx
     119:      	leal	-0x80(%rax), %r8d
     11d:      	movl	$0x80, %r9d
     123:      	subl	%eax, %r9d
     126:      	cmovbl	%r8d, %r9d
     12a:      	shlq	$0x10, %rcx
     12e:      	orq	%r9, %rcx
     131:      	movq	(%rdi), %rax
     134:      	addq	$0x8, %rdi
     138:      	jmpq	*%rax
     13a:      	nopw	(%rax,%rax)

0000000000000000 <ikea_comp_model>:
       0:      	movzbl	%cl, %eax
       3:      	cmpl	$0x80, %eax
       8:      	jne	0x15 <ikea_comp_model+0x15>
       a:      	xorl	%ecx, %ecx
       c:      	movq	(%rdi), %rax
       f:      	addq	$0x8, %rdi
      13:      	jmpq	*%rax
      15:      	shrl	$0x10, %ecx
      18:      	movzbl	%cl, %ecx
      1b:      	movl	$0x80, %r8d
      21:      	subl	%eax, %r8d
      24:      	xorl	%r9d, %r9d
      27:      	cmpl	$0x9, %r8d
      2b:      	setae	%r9b
      2f:      	cmpl	$0x21, %r8d
      33:      	sbbq	$-0x1, %r9
      37:      	cmpl	$0x61, %r8d
      3b:      	sbbq	$-0x1, %r9
      3f:      	leaq	(%r9,%r9,2), %r8
      43:      	movl	%r8d, %r8d
      46:      	leaq	(%rip), %r9             # 0x4d <ikea_comp_model+0x4d>
		0000000000000049:  R_X86_64_PC32	_ZN4ikea13bec_predictor12coefficientsE-0x4
      4d:      	imull	0x4(%r9,%r8,4), %eax
      53:      	imull	0x8(%r9,%r8,4), %ecx
      59:      	addl	(%r9,%r8,4), %eax
      5d:      	addl	%ecx, %eax
      5f:      	addl	$0x800, %eax            # imm = 0x800
      64:      	movl	%eax, %ecx
      66:      	sarl	$0xc, %ecx
      69:      	sarl	$0x1f, %eax
      6c:      	andnl	%ecx, %eax, %eax
      71:      	cmpl	$0x2f, %eax
      74:      	movl	$0x2f, %ecx
      79:      	cmovll	%eax, %ecx
      7c:      	movq	(%rdi), %rax
      7f:      	addq	$0x8, %rdi
      83:      	jmpq	*%rax
      85:      	nopw	%cs:(%rax,%rax)

0000000000000090 <ikea_comp_transition_model_stage>:
      90:      	movzbl	%cl, %eax
      93:      	cmpl	$0x80, %eax
      98:      	jne	0xa5 <ikea_comp_transition_model_stage+0x15>
      9a:      	xorl	%ecx, %ecx
      9c:      	movq	(%rdi), %rax
      9f:      	addq	$0x8, %rdi
      a3:      	jmpq	*%rax
      a5:      	pushq	%rbx
      a6:      	movzbl	%ch, %ebx
      a9:      	shrl	$0x10, %ecx
      ac:      	movzbl	%cl, %ecx
      af:      	testl	%ecx, %ecx
      b1:      	je	0xd9 <ikea_comp_transition_model_stage+0x49>
      b3:      	movl	$0x80, %r9d
      b9:      	subl	%eax, %r9d
      bc:      	xorl	%r8d, %r8d
      bf:      	cmpl	$0x9, %r9d
      c3:      	setae	%r8b
      c7:      	cmpl	$0x21, %r9d
      cb:      	sbbq	$-0x1, %r8
      cf:      	cmpl	$0x61, %r9d
      d3:      	sbbq	$-0x1, %r8
      d7:      	jmp	0xdf <ikea_comp_transition_model_stage+0x4f>
      d9:      	movl	$0x4, %r8d
      df:      	leaq	(%r8,%r8,4), %r8
      e3:      	leaq	(%rip), %r9             # 0xea <ikea_comp_transition_model_stage+0x5a>
		00000000000000e6:  R_X86_64_PC32	_ZN4ikea13bec_predictor23transition_coefficientsE-0x4
      ea:      	imull	0x4(%r9,%r8,4), %eax
      f0:      	imull	0x8(%r9,%r8,4), %ebx
      f6:      	imull	0xc(%r9,%r8,4), %ecx
      fc:      	addl	(%r9,%r8,4), %eax
     100:      	addl	%ebx, %eax
     102:      	addl	%ecx, %eax
     104:      	addl	$0x800, %eax            # imm = 0x800
     109:      	movl	%eax, %ecx
     10b:      	sarl	$0xc, %ecx
     10e:      	sarl	$0x1f, %eax
     111:      	andnl	%ecx, %eax, %eax
     116:      	cmpl	$0x2f, %eax
     119:      	movl	$0x2f, %ecx
     11e:      	cmovll	%eax, %ecx
     121:      	popq	%rbx
     122:      	movq	(%rdi), %rax
     125:      	addq	$0x8, %rdi
     129:      	jmpq	*%rax

0000000000000000 <ikea_comp_done>:
       0:      	leaq	(%rdx,%rcx), %rax
       4:      	retq
