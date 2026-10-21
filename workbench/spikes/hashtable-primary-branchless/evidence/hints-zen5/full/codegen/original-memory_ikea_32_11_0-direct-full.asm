000000000000d4c0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    d4c0:      	pushq	%rbp
    d4c1:      	pushq	%r15
    d4c3:      	pushq	%r14
    d4c5:      	pushq	%r13
    d4c7:      	pushq	%r12
    d4c9:      	pushq	%rbx
    d4ca:      	subq	$0x28, %rsp
    d4ce:      	vpxor	%xmm0, %xmm0, %xmm0
    d4d2:      	vmovdqu	%xmm0, 0x8(%rdi)
    d4d7:      	movq	%rdi, %rax
    d4da:      	movq	(%rdx), %rdi
    d4dd:      	movq	0x8(%rdx), %rbp
    d4e1:      	subq	%rdi, %rbp
    d4e4:      	je	0xd612 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x152>
    d4ea:      	vmovdqa	0x8bce(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    d4f2:      	movq	0x10(%rsi), %rbx
    d4f6:      	movq	0x70(%rsi), %r11
    d4fa:      	sarq	$0x3, %rbp
    d4fe:      	movabsq	$-0x61c8864e7a143579, %r13 # imm = 0x9E3779B185EBCA87
    d508:      	movq	%rsi, %r8
    d50b:      	movq	%rax, 0x8(%rsp)
    d510:      	xorl	%r14d, %r14d
    d513:      	xorl	%r12d, %r12d
    d516:      	xorl	%r10d, %r10d
    d519:      	movq	%rdi, 0x18(%rsp)
    d51e:      	movq	%rsi, 0x10(%rsp)
    d523:      	jmp	0xd542 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x82>
    d525:      	nopw	%cs:(%rax,%rax)
    d530:      	incq	%r10
    d533:      	addq	%r15, %r14
    d536:      	incq	%r12
    d539:      	cmpq	%r12, %rbp
    d53c:      	je	0xd607 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x147>
    d542:      	movq	(%rdi,%r12,8), %r15
    d546:      	movq	%r11, %rdx
    d549:      	xorq	%r15, %rdx
    d54c:      	mulxq	%r13, %rax, %rdx
    d551:      	movl	%eax, %ecx
    d553:      	shrl	$0xf, %ecx
    d556:      	movl	%eax, %esi
    d558:      	andl	$0x1fffff, %esi         # imm = 0x1FFFFF
    d55e:      	andl	$-0x40, %ecx
    d561:      	vpbroadcastd	%esi, %ymm2
    d567:      	vmovdqu	(%rbx,%rcx), %xmm0
    d56c:      	vmovdqu	0x18(%rbx,%rcx), %xmm1
    d572:      	vinserti128	$0x1, 0x8(%rbx,%rcx), %ymm0, %ymm0
    d57a:      	vpshufb	%ymm3, %ymm0, %ymm0
    d57f:      	vpcmpeqd	%ymm0, %ymm2, %k0
    d585:      	vinserti128	$0x1, 0x20(%rbx,%rcx), %ymm1, %ymm0
    d58d:      	vpshufb	%ymm3, %ymm0, %ymm0
    d592:      	vpcmpeqd	%ymm0, %ymm2, %k1
    d598:      	kortestb	%k0, %k1
    d59c:      	jne	0xd530 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x70>
    d59e:      	cmpb	$0x0, 0x3f(%rbx,%rcx)
    d5a3:      	je	0xd5ff <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x13f>
    d5a5:      	xorq	%rax, %rdx
    d5a8:      	movq	%r8, %rdi
    d5ab:      	movq	%r15, %rsi
    d5ae:      	movq	%r10, 0x20(%rsp)
    d5b3:      	movq	%r11, %r13
    d5b6:      	vzeroupper
    d5b9:      	callq	0xdd00 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    d5be:      	vmovdqa	0x8afa(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    d5c6:      	movq	0x20(%rsp), %r10
    d5cb:      	movq	0x18(%rsp), %rdi
    d5d0:      	movq	0x10(%rsp), %r8
    d5d5:      	andl	$0x100, %eax            # imm = 0x100
    d5da:      	movq	%r13, %r11
    d5dd:      	movabsq	$-0x61c8864e7a143579, %r13 # imm = 0x9E3779B185EBCA87
    d5e7:      	movl	%eax, %ecx
    d5e9:      	shrl	$0x8, %ecx
    d5ec:      	addq	%rcx, %r10
    d5ef:      	testl	%eax, %eax
    d5f1:      	movl	$0x0, %eax
    d5f6:      	cmoveq	%rax, %r15
    d5fa:      	jmp	0xd533 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x73>
    d5ff:      	xorl	%r15d, %r15d
    d602:      	jmp	0xd533 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x73>
    d607:      	movq	0x8(%rsp), %rax
    d60c:      	movq	%r14, 0x8(%rax)
    d610:      	jmp	0xd615 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x155>
    d612:      	xorl	%r10d, %r10d
    d615:      	movq	%r10, (%rax)
    d618:      	addq	$0x28, %rsp
    d61c:      	popq	%rbx
    d61d:      	popq	%r12
    d61f:      	popq	%r13
    d621:      	popq	%r14
    d623:      	popq	%r15
    d625:      	popq	%rbp
    d626:      	vzeroupper
    d629:      	retq
    d62a:      	nopw	%cs:(%rax,%rax)
    d634:      	nopw	%cs:(%rax,%rax)
    d63e:      	nop
