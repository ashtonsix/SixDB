0x9ec830 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const>:
  9ec830:      	pushq	%rbp
  9ec831:      	pushq	%r15
  9ec833:      	pushq	%r14
  9ec835:      	pushq	%r13
  9ec837:      	pushq	%r12
  9ec839:      	pushq	%rbx
  9ec83a:      	movq	(%rdi), %rax
  9ec83d:      	testl	%esi, %esi
  9ec83f:      	movl	$0x1c00, %ecx           # imm = 0x1C00
  9ec844:      	cmovel	%esi, %ecx
  9ec847:      	movl	%ecx, -0x14(%rsp)
  9ec84b:      	movq	(%rax), %r13
  9ec84e:      	movq	0x8(%rax), %rax
  9ec852:      	movq	%rax, -0x10(%rsp)
  9ec857:      	cmpq	%rax, %r13
  9ec85a:      	je	0x9ec99d <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x16d>
  9ec860:      	movq	0x8(%rdi), %rax
  9ec864:      	movq	(%rax), %rcx
  9ec867:      	testb	$0x3e, %cl
  9ec86a:      	je	0x9ec994 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x164>
  9ec870:      	movq	0x8(%rax), %r11
  9ec874:      	movq	$-0x1, %rdx
  9ec87b:      	movl	%ecx, %esi
  9ec87d:      	andl	$0x7c0, %esi            # imm = 0x7C0
  9ec883:      	movabsq	$0xb86659fd93, %r10     # imm = 0xB86659FD93
  9ec88d:      	xorl	%r14d, %r14d
  9ec890:      	movb	$0x28, %r15b
  9ec893:      	movabsq	$0x79d5f9e0de1e8cf5, %r12 # imm = 0x79D5F9E0DE1E8CF5
  9ec89d:      	xorl	%eax, %eax
  9ec89f:      	movq	%r13, -0x8(%rsp)
  9ec8a4:      	shlxq	%rcx, %rdx, %rdi
  9ec8a9:      	movq	%r11, %rbx
  9ec8ac:      	subq	%rdi, %rbx
  9ec8af:      	notq	%rdi
  9ec8b2:      	addq	$0xf, %rbx
  9ec8b6:      	jmp	0x9ec940 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x110>
  9ec8bb:      	nopl	(%rax,%rax)
  9ec8c0:      	andq	%rdi, %rdx
  9ec8c3:      	movq	%rdx, %r8
  9ec8c6:      	shlq	$0x4, %r8
  9ec8ca:      	prefetcht0	(%rbx,%r8)
  9ec8cf:      	vmovdqu	(%r11,%rdx), %xmm1
  9ec8d5:      	vpcmpeqb	%xmm1, %xmm0, %xmm2
  9ec8d9:      	vpmovmskb	%xmm2, %r8d
  9ec8dd:      	testl	%r8d, %r8d
  9ec8e0:      	je	0x9ec900 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0xd0>
  9ec8e2:      	tzcntl	%r8d, %r9d
  9ec8e7:      	addq	%rdx, %r9
  9ec8ea:      	andq	%rdi, %r9
  9ec8ed:      	shlq	$0x4, %r9
  9ec8f1:      	cmpq	%rbp, (%rbx,%r9)
  9ec8f5:      	je	0x9ec920 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0xf0>
  9ec8f7:      	blsrl	%r8d, %r8d
  9ec8fc:      	jne	0x9ec8e2 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0xb2>
  9ec8fe:      	nop
  9ec900:      	vpsignb	%xmm1, %xmm1, %xmm1
  9ec905:      	vpmovmskb	%xmm1, %r8d
  9ec909:      	testl	%r8d, %r8d
  9ec90c:      	jne	0x9ec980 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x150>
  9ec90e:      	leaq	0x10(%rcx,%rdx), %rdx
  9ec913:      	addq	$0x10, %rcx
  9ec917:      	jmp	0x9ec8c0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x90>
  9ec919:      	nopl	(%rax)
  9ec920:      	addq	%rbx, %r9
  9ec923:      	movl	$0x1, %ecx
  9ec928:      	addq	0x8(%r9), %rax
  9ec92c:      	movq	%rax, %rdx
  9ec92f:      	movq	%rcx, %rax
  9ec932:      	addq	%rdx, %rax
  9ec935:      	addq	$0x4, %r13
  9ec939:      	cmpq	-0x10(%rsp), %r13
  9ec93e:      	je	0x9ec984 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x154>
  9ec940:      	movl	(%r13), %ecx
  9ec944:      	prefetcht2	(%r11)
  9ec948:      	addl	-0x14(%rsp), %ecx
  9ec94c:      	imulq	%r10, %rcx
  9ec950:      	bzhiq	%r15, %rcx, %rbp
  9ec955:      	movq	%rbp, %rdx
  9ec958:      	xorq	%rsi, %rdx
  9ec95b:      	mulxq	%r12, %rcx, %rdx
  9ec960:      	xorq	%rcx, %rdx
  9ec963:      	shrq	$0x39, %rcx
  9ec967:      	vmovd	%ecx, %xmm0
  9ec96b:      	xorl	%ecx, %ecx
  9ec96d:      	vpbroadcastb	%xmm0, %xmm0
  9ec972:      	jmp	0x9ec8c0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x90>
  9ec977:      	nopw	(%rax,%rax)
  9ec980:      	xorl	%ecx, %ecx
  9ec982:      	jmp	0x9ec92c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0xfc>
  9ec984:      	movq	-0x8(%rsp), %r13
  9ec989:      	incl	%r14d
  9ec98c:      	cmpl	$0x10, %r14d
  9ec990:      	jne	0x9ec940 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x110>
  9ec992:      	jmp	0x9ec99f <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x16f>
  9ec994:      	cmpq	$0x8000, %rcx           # imm = 0x8000
  9ec99b:      	jae	0x9ec9aa <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x17a>
  9ec99d:      	xorl	%eax, %eax
  9ec99f:      	popq	%rbx
  9ec9a0:      	popq	%r12
  9ec9a2:      	popq	%r13
  9ec9a4:      	popq	%r14
  9ec9a6:      	popq	%r15
  9ec9a8:      	popq	%rbp
  9ec9a9:      	retq
  9ec9aa:      	movq	0x8(%rax), %rsi
  9ec9ae:      	movabsq	$0xb86659fd93, %rdx     # imm = 0xB86659FD93
  9ec9b8:      	movb	$0x28, %cl
  9ec9ba:      	xorl	%eax, %eax
  9ec9bc:      	movq	%r13, %r10
  9ec9bf:      	movq	(%rsi), %rdi
  9ec9c2:      	jmp	0x9ec9e9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1b9>
  9ec9c4:      	nopw	%cs:(%rax,%rax)
  9ec9d0:      	movq	%rax, %r9
  9ec9d3:      	xorl	%eax, %eax
  9ec9d5:      	cmpq	%r8, %rdi
  9ec9d8:      	sete	%al
  9ec9db:      	addq	$0x4, %r10
  9ec9df:      	addq	%r9, %rax
  9ec9e2:      	cmpq	-0x10(%rsp), %r10
  9ec9e7:      	je	0x9eca05 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1d5>
  9ec9e9:      	movl	(%r10), %r8d
  9ec9ec:      	addl	-0x14(%rsp), %r8d
  9ec9f1:      	imulq	%rdx, %r8
  9ec9f5:      	bzhiq	%rcx, %r8, %r8
  9ec9fa:      	cmpq	%r8, %rdi
  9ec9fd:      	jne	0x9ec9d0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1a0>
  9ec9ff:      	addq	0x8(%rsi), %rax
  9eca03:      	jmp	0x9ec9d0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1a0>
  9eca05:      	movb	$0x28, %cl
  9eca07:      	movq	%r13, %r10
  9eca0a:      	jmp	0x9eca29 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1f9>
  9eca0c:      	nopl	(%rax)
  9eca10:      	movq	%rax, %r9
  9eca13:      	xorl	%eax, %eax
  9eca15:      	cmpq	%r8, %rdi
  9eca18:      	sete	%al
  9eca1b:      	addq	$0x4, %r10
  9eca1f:      	addq	%r9, %rax
  9eca22:      	cmpq	-0x10(%rsp), %r10
  9eca27:      	je	0x9eca45 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x215>
  9eca29:      	movl	(%r10), %r8d
  9eca2c:      	addl	-0x14(%rsp), %r8d
  9eca31:      	imulq	%rdx, %r8
  9eca35:      	bzhiq	%rcx, %r8, %r8
  9eca3a:      	cmpq	%r8, %rdi
  9eca3d:      	jne	0x9eca10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1e0>
  9eca3f:      	addq	0x8(%rsi), %rax
  9eca43:      	jmp	0x9eca10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x1e0>
  9eca45:      	movb	$0x28, %cl
  9eca47:      	movq	%r13, %r10
  9eca4a:      	jmp	0x9eca69 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x239>
  9eca4c:      	nopl	(%rax)
  9eca50:      	movq	%rax, %r9
  9eca53:      	xorl	%eax, %eax
  9eca55:      	cmpq	%r8, %rdi
  9eca58:      	sete	%al
  9eca5b:      	addq	$0x4, %r10
  9eca5f:      	addq	%r9, %rax
  9eca62:      	cmpq	-0x10(%rsp), %r10
  9eca67:      	je	0x9eca85 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x255>
  9eca69:      	movl	(%r10), %r8d
  9eca6c:      	addl	-0x14(%rsp), %r8d
  9eca71:      	imulq	%rdx, %r8
  9eca75:      	bzhiq	%rcx, %r8, %r8
  9eca7a:      	cmpq	%r8, %rdi
  9eca7d:      	jne	0x9eca50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x220>
  9eca7f:      	addq	0x8(%rsi), %rax
  9eca83:      	jmp	0x9eca50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x220>
  9eca85:      	movb	$0x28, %cl
  9eca87:      	movq	%r13, %r10
  9eca8a:      	jmp	0x9ecaa9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x279>
  9eca8c:      	nopl	(%rax)
  9eca90:      	movq	%rax, %r9
  9eca93:      	xorl	%eax, %eax
  9eca95:      	cmpq	%r8, %rdi
  9eca98:      	sete	%al
  9eca9b:      	addq	$0x4, %r10
  9eca9f:      	addq	%r9, %rax
  9ecaa2:      	cmpq	-0x10(%rsp), %r10
  9ecaa7:      	je	0x9ecac5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x295>
  9ecaa9:      	movl	(%r10), %r8d
  9ecaac:      	addl	-0x14(%rsp), %r8d
  9ecab1:      	imulq	%rdx, %r8
  9ecab5:      	bzhiq	%rcx, %r8, %r8
  9ecaba:      	cmpq	%r8, %rdi
  9ecabd:      	jne	0x9eca90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x260>
  9ecabf:      	addq	0x8(%rsi), %rax
  9ecac3:      	jmp	0x9eca90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x260>
  9ecac5:      	movb	$0x28, %cl
  9ecac7:      	movq	%r13, %r10
  9ecaca:      	jmp	0x9ecae9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2b9>
  9ecacc:      	nopl	(%rax)
  9ecad0:      	movq	%rax, %r9
  9ecad3:      	xorl	%eax, %eax
  9ecad5:      	cmpq	%r8, %rdi
  9ecad8:      	sete	%al
  9ecadb:      	addq	$0x4, %r10
  9ecadf:      	addq	%r9, %rax
  9ecae2:      	cmpq	-0x10(%rsp), %r10
  9ecae7:      	je	0x9ecb05 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2d5>
  9ecae9:      	movl	(%r10), %r8d
  9ecaec:      	addl	-0x14(%rsp), %r8d
  9ecaf1:      	imulq	%rdx, %r8
  9ecaf5:      	bzhiq	%rcx, %r8, %r8
  9ecafa:      	cmpq	%r8, %rdi
  9ecafd:      	jne	0x9ecad0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2a0>
  9ecaff:      	addq	0x8(%rsi), %rax
  9ecb03:      	jmp	0x9ecad0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2a0>
  9ecb05:      	movb	$0x28, %cl
  9ecb07:      	movq	%r13, %r10
  9ecb0a:      	jmp	0x9ecb29 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2f9>
  9ecb0c:      	nopl	(%rax)
  9ecb10:      	movq	%rax, %r9
  9ecb13:      	xorl	%eax, %eax
  9ecb15:      	cmpq	%r8, %rdi
  9ecb18:      	sete	%al
  9ecb1b:      	addq	$0x4, %r10
  9ecb1f:      	addq	%r9, %rax
  9ecb22:      	cmpq	-0x10(%rsp), %r10
  9ecb27:      	je	0x9ecb45 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x315>
  9ecb29:      	movl	(%r10), %r8d
  9ecb2c:      	addl	-0x14(%rsp), %r8d
  9ecb31:      	imulq	%rdx, %r8
  9ecb35:      	bzhiq	%rcx, %r8, %r8
  9ecb3a:      	cmpq	%r8, %rdi
  9ecb3d:      	jne	0x9ecb10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2e0>
  9ecb3f:      	addq	0x8(%rsi), %rax
  9ecb43:      	jmp	0x9ecb10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x2e0>
  9ecb45:      	movb	$0x28, %cl
  9ecb47:      	movq	%r13, %r10
  9ecb4a:      	jmp	0x9ecb69 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x339>
  9ecb4c:      	nopl	(%rax)
  9ecb50:      	movq	%rax, %r9
  9ecb53:      	xorl	%eax, %eax
  9ecb55:      	cmpq	%r8, %rdi
  9ecb58:      	sete	%al
  9ecb5b:      	addq	$0x4, %r10
  9ecb5f:      	addq	%r9, %rax
  9ecb62:      	cmpq	-0x10(%rsp), %r10
  9ecb67:      	je	0x9ecb85 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x355>
  9ecb69:      	movl	(%r10), %r8d
  9ecb6c:      	addl	-0x14(%rsp), %r8d
  9ecb71:      	imulq	%rdx, %r8
  9ecb75:      	bzhiq	%rcx, %r8, %r8
  9ecb7a:      	cmpq	%r8, %rdi
  9ecb7d:      	jne	0x9ecb50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x320>
  9ecb7f:      	addq	0x8(%rsi), %rax
  9ecb83:      	jmp	0x9ecb50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x320>
  9ecb85:      	movb	$0x28, %cl
  9ecb87:      	movq	%r13, %r10
  9ecb8a:      	jmp	0x9ecba9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x379>
  9ecb8c:      	nopl	(%rax)
  9ecb90:      	movq	%rax, %r9
  9ecb93:      	xorl	%eax, %eax
  9ecb95:      	cmpq	%r8, %rdi
  9ecb98:      	sete	%al
  9ecb9b:      	addq	$0x4, %r10
  9ecb9f:      	addq	%r9, %rax
  9ecba2:      	cmpq	-0x10(%rsp), %r10
  9ecba7:      	je	0x9ecbc5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x395>
  9ecba9:      	movl	(%r10), %r8d
  9ecbac:      	addl	-0x14(%rsp), %r8d
  9ecbb1:      	imulq	%rdx, %r8
  9ecbb5:      	bzhiq	%rcx, %r8, %r8
  9ecbba:      	cmpq	%r8, %rdi
  9ecbbd:      	jne	0x9ecb90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x360>
  9ecbbf:      	addq	0x8(%rsi), %rax
  9ecbc3:      	jmp	0x9ecb90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x360>
  9ecbc5:      	movb	$0x28, %cl
  9ecbc7:      	movq	%r13, %r10
  9ecbca:      	jmp	0x9ecbe9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3b9>
  9ecbcc:      	nopl	(%rax)
  9ecbd0:      	movq	%rax, %r9
  9ecbd3:      	xorl	%eax, %eax
  9ecbd5:      	cmpq	%r8, %rdi
  9ecbd8:      	sete	%al
  9ecbdb:      	addq	$0x4, %r10
  9ecbdf:      	addq	%r9, %rax
  9ecbe2:      	cmpq	-0x10(%rsp), %r10
  9ecbe7:      	je	0x9ecc05 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3d5>
  9ecbe9:      	movl	(%r10), %r8d
  9ecbec:      	addl	-0x14(%rsp), %r8d
  9ecbf1:      	imulq	%rdx, %r8
  9ecbf5:      	bzhiq	%rcx, %r8, %r8
  9ecbfa:      	cmpq	%r8, %rdi
  9ecbfd:      	jne	0x9ecbd0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3a0>
  9ecbff:      	addq	0x8(%rsi), %rax
  9ecc03:      	jmp	0x9ecbd0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3a0>
  9ecc05:      	movb	$0x28, %cl
  9ecc07:      	movq	%r13, %r10
  9ecc0a:      	jmp	0x9ecc29 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3f9>
  9ecc0c:      	nopl	(%rax)
  9ecc10:      	movq	%rax, %r9
  9ecc13:      	xorl	%eax, %eax
  9ecc15:      	cmpq	%r8, %rdi
  9ecc18:      	sete	%al
  9ecc1b:      	addq	$0x4, %r10
  9ecc1f:      	addq	%r9, %rax
  9ecc22:      	cmpq	-0x10(%rsp), %r10
  9ecc27:      	je	0x9ecc45 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x415>
  9ecc29:      	movl	(%r10), %r8d
  9ecc2c:      	addl	-0x14(%rsp), %r8d
  9ecc31:      	imulq	%rdx, %r8
  9ecc35:      	bzhiq	%rcx, %r8, %r8
  9ecc3a:      	cmpq	%r8, %rdi
  9ecc3d:      	jne	0x9ecc10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3e0>
  9ecc3f:      	addq	0x8(%rsi), %rax
  9ecc43:      	jmp	0x9ecc10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x3e0>
  9ecc45:      	movb	$0x28, %cl
  9ecc47:      	movq	%r13, %r10
  9ecc4a:      	jmp	0x9ecc69 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x439>
  9ecc4c:      	nopl	(%rax)
  9ecc50:      	movq	%rax, %r9
  9ecc53:      	xorl	%eax, %eax
  9ecc55:      	cmpq	%r8, %rdi
  9ecc58:      	sete	%al
  9ecc5b:      	addq	$0x4, %r10
  9ecc5f:      	addq	%r9, %rax
  9ecc62:      	cmpq	-0x10(%rsp), %r10
  9ecc67:      	je	0x9ecc85 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x455>
  9ecc69:      	movl	(%r10), %r8d
  9ecc6c:      	addl	-0x14(%rsp), %r8d
  9ecc71:      	imulq	%rdx, %r8
  9ecc75:      	bzhiq	%rcx, %r8, %r8
  9ecc7a:      	cmpq	%r8, %rdi
  9ecc7d:      	jne	0x9ecc50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x420>
  9ecc7f:      	addq	0x8(%rsi), %rax
  9ecc83:      	jmp	0x9ecc50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x420>
  9ecc85:      	movb	$0x28, %cl
  9ecc87:      	movq	%r13, %r10
  9ecc8a:      	jmp	0x9ecca9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x479>
  9ecc8c:      	nopl	(%rax)
  9ecc90:      	movq	%rax, %r9
  9ecc93:      	xorl	%eax, %eax
  9ecc95:      	cmpq	%r8, %rdi
  9ecc98:      	sete	%al
  9ecc9b:      	addq	$0x4, %r10
  9ecc9f:      	addq	%r9, %rax
  9ecca2:      	cmpq	-0x10(%rsp), %r10
  9ecca7:      	je	0x9eccc5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x495>
  9ecca9:      	movl	(%r10), %r8d
  9eccac:      	addl	-0x14(%rsp), %r8d
  9eccb1:      	imulq	%rdx, %r8
  9eccb5:      	bzhiq	%rcx, %r8, %r8
  9eccba:      	cmpq	%r8, %rdi
  9eccbd:      	jne	0x9ecc90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x460>
  9eccbf:      	addq	0x8(%rsi), %rax
  9eccc3:      	jmp	0x9ecc90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x460>
  9eccc5:      	movb	$0x28, %cl
  9eccc7:      	movq	%r13, %r10
  9eccca:      	jmp	0x9ecce9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4b9>
  9ecccc:      	nopl	(%rax)
  9eccd0:      	movq	%rax, %r9
  9eccd3:      	xorl	%eax, %eax
  9eccd5:      	cmpq	%r8, %rdi
  9eccd8:      	sete	%al
  9eccdb:      	addq	$0x4, %r10
  9eccdf:      	addq	%r9, %rax
  9ecce2:      	cmpq	-0x10(%rsp), %r10
  9ecce7:      	je	0x9ecd05 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4d5>
  9ecce9:      	movl	(%r10), %r8d
  9eccec:      	addl	-0x14(%rsp), %r8d
  9eccf1:      	imulq	%rdx, %r8
  9eccf5:      	bzhiq	%rcx, %r8, %r8
  9eccfa:      	cmpq	%r8, %rdi
  9eccfd:      	jne	0x9eccd0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4a0>
  9eccff:      	addq	0x8(%rsi), %rax
  9ecd03:      	jmp	0x9eccd0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4a0>
  9ecd05:      	movb	$0x28, %cl
  9ecd07:      	movq	%r13, %r10
  9ecd0a:      	jmp	0x9ecd29 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4f9>
  9ecd0c:      	nopl	(%rax)
  9ecd10:      	movq	%rax, %r9
  9ecd13:      	xorl	%eax, %eax
  9ecd15:      	cmpq	%r8, %rdi
  9ecd18:      	sete	%al
  9ecd1b:      	addq	$0x4, %r10
  9ecd1f:      	addq	%r9, %rax
  9ecd22:      	cmpq	-0x10(%rsp), %r10
  9ecd27:      	je	0x9ecd45 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x515>
  9ecd29:      	movl	(%r10), %r8d
  9ecd2c:      	addl	-0x14(%rsp), %r8d
  9ecd31:      	imulq	%rdx, %r8
  9ecd35:      	bzhiq	%rcx, %r8, %r8
  9ecd3a:      	cmpq	%r8, %rdi
  9ecd3d:      	jne	0x9ecd10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4e0>
  9ecd3f:      	addq	0x8(%rsi), %rax
  9ecd43:      	jmp	0x9ecd10 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x4e0>
  9ecd45:      	movb	$0x28, %cl
  9ecd47:      	movq	%r13, %r10
  9ecd4a:      	jmp	0x9ecd69 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x539>
  9ecd4c:      	nopl	(%rax)
  9ecd50:      	movq	%rax, %r9
  9ecd53:      	xorl	%eax, %eax
  9ecd55:      	cmpq	%r8, %rdi
  9ecd58:      	sete	%al
  9ecd5b:      	addq	$0x4, %r10
  9ecd5f:      	addq	%r9, %rax
  9ecd62:      	cmpq	-0x10(%rsp), %r10
  9ecd67:      	je	0x9ecd85 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x555>
  9ecd69:      	movl	(%r10), %r8d
  9ecd6c:      	addl	-0x14(%rsp), %r8d
  9ecd71:      	imulq	%rdx, %r8
  9ecd75:      	bzhiq	%rcx, %r8, %r8
  9ecd7a:      	cmpq	%r8, %rdi
  9ecd7d:      	jne	0x9ecd50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x520>
  9ecd7f:      	addq	0x8(%rsi), %rax
  9ecd83:      	jmp	0x9ecd50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x520>
  9ecd85:      	movb	$0x28, %cl
  9ecd87:      	jmp	0x9ecdac <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x57c>
  9ecd89:      	nopl	(%rax)
  9ecd90:      	xorl	%r9d, %r9d
  9ecd93:      	cmpq	%r8, %rdi
  9ecd96:      	sete	%r9b
  9ecd9a:      	addq	$0x4, %r13
  9ecd9e:      	addq	%r9, %rax
  9ecda1:      	cmpq	-0x10(%rsp), %r13
  9ecda6:      	je	0x9ec99f <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x16f>
  9ecdac:      	movl	(%r13), %r8d
  9ecdb0:      	addl	-0x14(%rsp), %r8d
  9ecdb5:      	imulq	%rdx, %r8
  9ecdb9:      	bzhiq	%rcx, %r8, %r8
  9ecdbe:      	cmpq	%r8, %rdi
  9ecdc1:      	jne	0x9ecd90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x560>
  9ecdc3:      	addq	0x8(%rsi), %rax
  9ecdc7:      	jmp	0x9ecd90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)::'lambda'(bool)::operator()(bool) const+0x560>
  9ecdc9:      	nopl	(%rax)

