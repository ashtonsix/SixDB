0x9345b0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const>:
  9345b0:      	pushq	%rbp
  9345b1:      	pushq	%r15
  9345b3:      	pushq	%r14
  9345b5:      	pushq	%r13
  9345b7:      	pushq	%r12
  9345b9:      	pushq	%rbx
  9345ba:      	subq	$0x18, %rsp
  9345be:      	movq	(%rdi), %rbx
  9345c1:      	movb	$0x28, %r10b
  9345c4:      	movabsq	$0x79d5f9e0de1e8cf5, %r11 # imm = 0x79D5F9E0DE1E8CF5
  9345ce:      	movq	%rdi, %r9
  9345d1:      	xorl	%ebp, %ebp
  9345d3:      	xorl	%r14d, %r14d
  9345d6:      	movq	%rdi, 0x8(%rsp)
  9345db:      	jmp	0x9345fb <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x4b>
  9345dd:      	nopl	(%rax)
  9345e0:      	xorl	%ecx, %ecx
  9345e2:      	xorl	%eax, %eax
  9345e4:      	movq	(%r9), %rbx
  9345e7:      	incl	%ebp
  9345e9:      	movq	%rax, %r14
  9345ec:      	addq	%rcx, (%rbx)
  9345ef:      	cmpl	$0x1c000, %ebp          # imm = 0x1C000
  9345f5:      	je	0x9349c4 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x414>
  9345fb:      	movq	0x8(%r9), %r12
  9345ff:      	movq	0x58(%r12), %rdi
  934604:      	movq	0x60(%r12), %rdx
  934609:      	movq	0x20(%r12), %r8
  93460e:      	xorq	%r14, %rdi
  934611:      	bzhiq	%r10, %rdi, %rsi
  934616:      	movq	%rsi, %rcx
  934619:      	shrq	$0xa, %rcx
  93461d:      	xorq	%rcx, %rdx
  934620:      	mulxq	%r11, %rdx, %rax
  934625:      	xorl	%edx, %edi
  934627:      	xorl	%eax, %edi
  934629:      	shll	$0x7, %edi
  93462c:      	andl	$0x1ff80, %edi          # imm = 0x1FF80
  934632:      	leaq	(%r8,%rdi), %rax
  934636:      	movzbl	(%r8,%rdi), %edi
  93463b:      	testb	%dil, %dil
  93463e:      	je	0x9346d9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x129>
  934644:      	movq	0x68(%r12), %rdx
  934649:      	shrq	$0x12, %rsi
  93464d:      	vmovq	0x5(%rax), %xmm0
  934652:      	movzbl	%dil, %r13d
  934656:      	movl	$0xa, %r15d
  93465c:      	xorq	%rsi, %rdx
  93465f:      	mulxq	%r11, %r8, %rdx
  934664:      	xorl	%r8d, %ecx
  934667:      	movzwl	0xd(%rax), %r8d
  93466c:      	xorl	%edx, %ecx
  93466e:      	cmpb	$0xa, %dil
  934672:      	cmovael	%r15d, %r13d
  934676:      	vmovd	%r8d, %xmm1
  93467b:      	vpunpcklqdq	%xmm1, %xmm0, %xmm0 # xmm0 = xmm0[0],xmm1[0]
  93467f:      	vmovd	%ecx, %xmm1
  934683:      	vpbroadcastb	%xmm1, %xmm1
  934688:      	vpcmpeqb	%xmm0, %xmm1, %xmm0
  93468c:      	vpmovmskb	%xmm0, %ecx
  934690:      	bzhil	%r13d, %ecx, %edx
  934695:      	je	0x9346d9 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x129>
  934697:      	nopw	(%rax,%rax)
  9346a0:      	tzcntq	%rdx, %rcx
  9346a5:      	leal	(%rcx,%rcx,2), %edi
  9346a8:      	leal	0x78(,%rdi,8), %edi
  9346af:      	shrl	$0x3, %edi
  9346b2:      	movzwl	(%rax,%rdi), %r8d
  9346b7:      	movzbl	0x2(%rax,%rdi), %edi
  9346bc:      	shll	$0x10, %edi
  9346bf:      	orl	%r8d, %edi
  9346c2:      	cmpl	%esi, %edi
  9346c4:      	je	0x9348eb <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x33b>
  9346ca:      	movq	$-0x2, %rdi
  9346d1:      	rolq	%cl, %rdi
  9346d4:      	andq	%rdi, %rdx
  9346d7:      	jne	0x9346a0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0xf0>
  9346d9:      	cmpl	$0x0, 0x1(%rax)
  9346dd:      	je	0x9345e0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x30>
  9346e3:      	movq	0x78(%r12), %rdi
  9346e8:      	movq	0x80(%r12), %rdx
  9346f0:      	movq	0x40(%r12), %r8
  9346f5:      	xorq	%r14, %rdi
  9346f8:      	bzhiq	%r10, %rdi, %rsi
  9346fd:      	movq	%rsi, %rcx
  934700:      	shrq	$0x6, %rcx
  934704:      	xorq	%rcx, %rdx
  934707:      	mulxq	%r11, %rdx, %rax
  93470c:      	xorl	%edx, %edi
  93470e:      	xorl	%eax, %edi
  934710:      	andl	$0x3f, %edi
  934713:      	shll	$0x7, %edi
  934716:      	leaq	(%r8,%rdi), %rax
  93471a:      	movzbl	(%r8,%rdi), %edi
  93471f:      	testb	%dil, %dil
  934722:      	je	0x9347ab <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x1fb>
  934728:      	movq	0x88(%r12), %rdx
  934730:      	shrq	$0xe, %rsi
  934734:      	vmovq	0x9(%rax), %xmm0
  934739:      	movzbl	%dil, %r13d
  93473d:      	movl	$0x9, %r15d
  934743:      	xorq	%rsi, %rdx
  934746:      	mulxq	%r11, %r8, %rdx
  93474b:      	xorl	%r8d, %ecx
  93474e:      	movzbl	0x11(%rax), %r8d
  934753:      	xorl	%edx, %ecx
  934755:      	cmpb	$0x9, %dil
  934759:      	cmovael	%r15d, %r13d
  93475d:      	vmovd	%r8d, %xmm1
  934762:      	vpunpcklqdq	%xmm1, %xmm0, %xmm0 # xmm0 = xmm0[0],xmm1[0]
  934766:      	vmovd	%ecx, %xmm1
  93476a:      	vpbroadcastb	%xmm1, %xmm1
  93476f:      	vpcmpeqb	%xmm0, %xmm1, %xmm0
  934773:      	vpmovmskb	%xmm0, %ecx
  934777:      	bzhil	%r13d, %ecx, %edx
  93477c:      	je	0x9347ab <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x1fb>
  93477e:      	nop
  934780:      	tzcntq	%rdx, %rcx
  934785:      	movl	%ecx, %edi
  934787:      	shll	$0x5, %edi
  93478a:      	addl	$0x90, %edi
  934790:      	shrl	$0x3, %edi
  934793:      	cmpl	(%rax,%rdi), %esi
  934796:      	je	0x934959 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3a9>
  93479c:      	movq	$-0x2, %rdi
  9347a3:      	rolq	%cl, %rdi
  9347a6:      	andq	%rdi, %rdx
  9347a9:      	jne	0x934780 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x1d0>
  9347ab:      	cmpl	$0x0, 0x1(%rax)
  9347af:      	je	0x9345e0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x30>
  9347b5:      	movl	0x5(%rax), %r13d
  9347b9:      	testl	%r13d, %r13d
  9347bc:      	je	0x9345e0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x30>
  9347c2:      	movq	%rbx, 0x10(%rsp)
  9347c7:      	jmp	0x9347fa <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x24a>
  9347c9:      	nopl	(%rax)
  9347d0:      	movl	0x2(%rax,%rcx), %r13d
  9347d5:      	movq	0x8(%rsp), %r9
  9347da:      	movl	$0x0, %ecx
  9347df:      	movl	$0x0, %eax
  9347e4:      	movb	$0x28, %r10b
  9347e7:      	movabsq	$0x79d5f9e0de1e8cf5, %r11 # imm = 0x79D5F9E0DE1E8CF5
  9347f1:      	testl	%r13d, %r13d
  9347f4:      	je	0x9345e4 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x34>
  9347fa:      	movq	0xa0(%r12), %r15
  934802:      	movzbl	0xa91b27(%rip), %eax    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  934809:      	testb	%al, %al
  93480b:      	je	0x93489b <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2eb>
  934811:      	movl	%r13d, %ebx
  934814:      	shrl	$0x8, %ebx
  934817:      	movq	0xa91afa(%rip), %rdx    # 0x13c6318 <ht::links<40u, false, 8u, false>::formats()::p>
  93481e:      	decl	%ebx
  934820:      	leaq	(%rbx,%rbx,4), %rax
  934824:      	movl	0x20(%r15,%rax,8), %ecx
  934829:      	movq	0x8(%r15,%rax,8), %rax
  93482e:      	imulq	$0x2c, %rcx, %rcx
  934832:      	movzwl	0x8(%rdx,%rcx), %esi
  934837:      	cmpb	$0x0, 0xa(%rdx,%rcx)
  93483c:      	movl	$0x0, %ecx
  934841:      	cmovel	%ecx, %esi
  934844:      	movzbl	%r13b, %ecx
  934848:      	imulq	%rsi, %rcx
  93484c:      	movzwl	0x6(%rax,%rcx), %edx
  934851:      	testq	%rdx, %rdx
  934854:      	je	0x9347d0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x220>
  93485a:      	leaq	(%rdx,%rdx,2), %rsi
  93485e:      	movq	$-0xd, %r15
  934865:      	leaq	(%rdx,%rsi,4), %rdx
  934869:      	leaq	0xc(%rax,%rcx), %rsi
  93486e:      	nop
  934870:      	movzbl	(%rsi), %r8d
  934874:      	movl	-0x4(%rsi), %edi
  934877:      	shlq	$0x20, %r8
  93487b:      	orq	%rdi, %r8
  93487e:      	cmpq	%r14, %r8
  934881:      	je	0x9348f6 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x346>
  934883:      	leaq	-0xd(%rdx,%r15), %rdi
  934888:      	addq	$-0xd, %r15
  93488c:      	addq	$0xd, %rsi
  934890:      	cmpq	$-0xd, %rdi
  934894:      	jne	0x934870 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2c0>
  934896:      	jmp	0x9347d0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x220>
  93489b:      	leaq	0xa91a8e(%rip), %rdi    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  9348a2:      	callq	0x7780 <__cxa_guard_acquire@plt>
  9348a7:      	testl	%eax, %eax
  9348a9:      	je	0x934811 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x261>
  9348af:      	leaq	0xa91a62(%rip), %rdi    # 0x13c6318 <ht::links<40u, false, 8u, false>::formats()::p>
  9348b6:      	leaq	0x6(%rsp), %rsi
  9348bb:      	callq	0x92fa20 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  9348c0:      	leaq	-0x9241c7(%rip), %rdi   # 0x10700 <std::vector<ikea::streampack::layout, std::allocator<ikea::streampack::layout>>::~vector()>
  9348c7:      	leaq	0xa91a4a(%rip), %rsi    # 0x13c6318 <ht::links<40u, false, 8u, false>::formats()::p>
  9348ce:      	leaq	0xa90b2b(%rip), %rdx    # 0x13c5400 <__dso_handle>
  9348d5:      	callq	0x7390 <__cxa_atexit@plt>
  9348da:      	leaq	0xa91a4f(%rip), %rdi    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  9348e1:      	callq	0x72b0 <__cxa_guard_release@plt>
  9348e6:      	jmp	0x934811 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x261>
  9348eb:      	shll	$0x6, %ecx
  9348ee:      	addl	$0x168, %ecx            # imm = 0x168
  9348f4:      	jmp	0x934962 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3b2>
  9348f6:      	movq	0xa0(%r12), %r14
  9348fe:      	movzbl	0xa91a2b(%rip), %eax    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  934905:      	testb	%al, %al
  934907:      	je	0x934978 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3c8>
  934909:      	leaq	(%rbx,%rbx,4), %rcx
  93490d:      	movq	0xa91a04(%rip), %rdx    # 0x13c6318 <ht::links<40u, false, 8u, false>::formats()::p>
  934914:      	movq	0x8(%rsp), %r9
  934919:      	movq	0x10(%rsp), %rbx
  93491e:      	movb	$0x28, %r10b
  934921:      	movabsq	$0x79d5f9e0de1e8cf5, %r11 # imm = 0x79D5F9E0DE1E8CF5
  93492b:      	movl	0x20(%r14,%rcx,8), %eax
  934930:      	imulq	$0x2c, %rax, %rax
  934934:      	movzwl	0x8(%rdx,%rax), %esi
  934939:      	cmpb	$0x0, 0xa(%rdx,%rax)
  93493e:      	movl	$0x0, %eax
  934943:      	cmoveq	%rax, %rsi
  934947:      	movzbl	%r13b, %eax
  93494b:      	imulq	%rsi, %rax
  93494f:      	addq	0x8(%r14,%rcx,8), %rax
  934954:      	subq	%r15, %rax
  934957:      	jmp	0x934968 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3b8>
  934959:      	shll	$0x6, %ecx
  93495c:      	addl	$0x1b0, %ecx            # imm = 0x1B0
  934962:      	shrl	$0x3, %ecx
  934965:      	addq	%rcx, %rax
  934968:      	movq	(%rax), %rax
  93496b:      	movl	$0x1, %ecx
  934970:      	addq	%rax, (%rbx)
  934973:      	jmp	0x9345e4 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x34>
  934978:      	leaq	0xa919b1(%rip), %rdi    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  93497f:      	callq	0x7780 <__cxa_guard_acquire@plt>
  934984:      	testl	%eax, %eax
  934986:      	je	0x934909 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x359>
  934988:      	leaq	0xa91989(%rip), %rdi    # 0x13c6318 <ht::links<40u, false, 8u, false>::formats()::p>
  93498f:      	leaq	0x7(%rsp), %rsi
  934994:      	callq	0x92fa20 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  934999:      	leaq	-0x9242a0(%rip), %rdi   # 0x10700 <std::vector<ikea::streampack::layout, std::allocator<ikea::streampack::layout>>::~vector()>
  9349a0:      	leaq	0xa91971(%rip), %rsi    # 0x13c6318 <ht::links<40u, false, 8u, false>::formats()::p>
  9349a7:      	leaq	0xa90a52(%rip), %rdx    # 0x13c5400 <__dso_handle>
  9349ae:      	callq	0x7390 <__cxa_atexit@plt>
  9349b3:      	leaq	0xa91976(%rip), %rdi    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  9349ba:      	callq	0x72b0 <__cxa_guard_release@plt>
  9349bf:      	jmp	0x934909 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x359>
  9349c4:      	addq	$0x18, %rsp
  9349c8:      	popq	%rbx
  9349c9:      	popq	%r12
  9349cb:      	popq	%r13
  9349cd:      	popq	%r14
  9349cf:      	popq	%r15
  9349d1:      	popq	%rbp
  9349d2:      	retq
  9349d3:      	jmp	0x9349d5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x425>
  9349d5:      	leaq	0xa91954(%rip), %rdi    # 0x13c6330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  9349dc:      	movq	%rax, %rbx
  9349df:      	callq	0x7290 <__cxa_guard_abort@plt>
  9349e4:      	movq	%rbx, %rdi
  9349e7:      	callq	0x7740 <_Unwind_Resume@plt>
  9349ec:      	nopl	(%rax)

