000000000000e400 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)>:
    e400:      	pushq	%rbp
    e401:      	pushq	%r15
    e403:      	pushq	%r14
    e405:      	pushq	%r13
    e407:      	pushq	%r12
    e409:      	pushq	%rbx
    e40a:      	subq	$0xc8, %rsp
    e411:      	testq	%rcx, %rcx
    e414:      	leaq	0x88(%rsp), %r14
    e41c:      	movq	%rcx, %rbp
    e41f:      	movq	0x8(%rdx), %r8
    e423:      	movq	(%rdx), %r13
    e426:      	movb	$0x0, 0x88(%rsp)
    e42e:      	vpxor	%xmm0, %xmm0, %xmm0
    e432:      	vmovdqu	%ymm0, 0x90(%rsp)
    e43b:      	vmovdqu	%ymm0, 0xa4(%rsp)
    e444:      	movq	%rsi, 0x18(%rsp)
    e449:      	movq	%rdi, %r12
    e44c:      	vpxor	%xmm0, %xmm0, %xmm0
    e450:      	cmoveq	%r14, %rbp
    e454:      	movq	0x20(%rbp), %r11
    e458:      	movq	0x28(%rbp), %r10
    e45c:      	movq	0x30(%rbp), %r9
    e460:      	movq	0x18(%rbp), %rbx
    e464:      	vmovdqu	%xmm0, (%rdi)
    e468:      	movq	$0x0, 0x10(%rdi)
    e470:      	movq	%r8, 0x8(%rsp)
    e475:      	movq	%r9, %rax
    e478:      	movq	%r10, %rdx
    e47b:      	movq	%r11, %rsi
    e47e:      	movq	%rbx, %rdi
    e481:      	cmpq	%r13, %r8
    e484:      	je	0xe678 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x278>
    e48a:      	testq	%rcx, %rcx
    e48d:      	movq	%rbx, 0x20(%rsp)
    e492:      	movq	0x8(%rsp), %r8
    e497:      	leaq	0x90(%rsp), %rax
    e49f:      	leaq	0x8(%rcx), %rdx
    e4a3:      	movq	%r13, %r15
    e4a6:      	movq	%r9, 0x38(%rsp)
    e4ab:      	movq	%r11, 0x28(%rsp)
    e4b0:      	movq	%r10, 0x30(%rsp)
    e4b5:      	movq	%r15, %r9
    e4b8:      	movq	%rbp, 0x48(%rsp)
    e4bd:      	cmovneq	%rcx, %r14
    e4c1:      	cmoveq	%rax, %rdx
    e4c5:      	movl	0x38(%r14), %ebx
    e4c9:      	leaq	0x10(%r14), %rax
    e4cd:      	movq	%r14, %r13
    e4d0:      	movq	%rdx, 0x40(%rsp)
    e4d5:      	movq	%rax, 0x10(%rsp)
    e4da:      	jmp	0xe503 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x103>
    e4dc:      	nopl	(%rax)
    e4e0:      	incl	%ebx
    e4e2:      	movq	0x50(%rsp), %r9
    e4e7:      	movl	%ebx, 0x38(%r13)
    e4eb:      	movq	0x58(%rsp), %rax
    e4f0:      	vpaddq	0x60(%rsp), %xmm0, %xmm0
    e4f6:      	addq	%rax, (%r12)
    e4fa:      	cmpq	%r8, %r9
    e4fd:      	je	0xe64d <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x24d>
    e503:      	movq	%r8, %r15
    e506:      	subq	%r9, %r15
    e509:      	movl	$0x100, %eax            # imm = 0x100
    e50e:      	movl	%ebx, %edx
    e510:      	sarq	$0x3, %r15
    e514:      	cmpq	$0x100, %r15            # imm = 0x100
    e51b:      	cmovaeq	%rax, %r15
    e51f:      	xorl	%r14d, %r14d
    e522:      	andl	$0x3f, %edx
    e525:      	setne	%al
    e528:      	cmpl	$0x2, %edx
    e52b:      	jb	0xe550 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x150>
    e52d:      	movl	(%rbp), %r14d
    e531:      	testb	$0x3e, %bl
    e534:      	jne	0xe558 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x158>
    e536:      	lfence
    e539:      	rdtsc
    e53b:      	lfence
    e53e:      	movq	%rdx, %rbp
    e541:      	shlq	$0x20, %rbp
    e545:      	orq	%rax, %rbp
    e548:      	jmp	0xe55a <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x15a>
    e54a:      	nopw	(%rax,%rax)
    e550:      	movb	%al, %r14b
    e553:      	testb	$0x3e, %bl
    e556:      	je	0xe536 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x136>
    e558:      	xorl	%ebp, %ebp
    e55a:      	leaq	(%r9,%r15,8), %rcx
    e55e:      	leaq	0x58(%rsp), %rdi
    e563:      	vmovdqa	%xmm0, 0x70(%rsp)
    e569:      	movq	%rcx, 0x50(%rsp)
    e56e:      	testb	%r14b, %r14b
    e571:      	je	0xe5b0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x1b0>
    e573:      	movq	0x18(%rsp), %rsi
    e578:      	movq	%r9, %rdx
    e57b:      	vzeroupper
    e57e:      	callq	0xf140 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    e583:      	vmovdqa	0x70(%rsp), %xmm0
    e589:      	testb	$0x3e, %bl
    e58c:      	jne	0xe5cb <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x1cb>
    e58e:      	lfence
    e591:      	rdtsc
    e593:      	lfence
    e596:      	movq	%rax, %rcx
    e599:      	movq	%rdx, %rax
    e59c:      	shlq	$0x20, %rax
    e5a0:      	orq	%rcx, %rax
    e5a3:      	subq	%rbp, %rax
    e5a6:      	shlq	$0xa, %rax
    e5aa:      	jmp	0xe5cd <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x1cd>
    e5ac:      	nopl	(%rax)
    e5b0:      	movq	0x18(%rsp), %rsi
    e5b5:      	movq	%r9, %rdx
    e5b8:      	vzeroupper
    e5bb:      	callq	0xf300 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    e5c0:      	vmovdqa	0x70(%rsp), %xmm0
    e5c6:      	testb	$0x3e, %bl
    e5c9:      	je	0xe58e <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x18e>
    e5cb:      	xorl	%eax, %eax
    e5cd:      	movq	0x48(%rsp), %rbp
    e5d2:      	movzbl	%r14b, %ecx
    e5d6:      	movq	0x8(%rsp), %r8
    e5db:      	addq	%r15, 0x18(%rbp,%rcx,8)
    e5e0:      	movl	0x38(%r13), %ebx
    e5e4:      	testb	$0x3e, %bl
    e5e7:      	jne	0xe4e0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0xe0>
    e5ed:      	incq	0x28(%rbp)
    e5f1:      	movq	%rax, %rcx
    e5f4:      	shrq	$0x20, %rcx
    e5f8:      	je	0xe601 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x201>
    e5fa:      	xorl	%edx, %edx
    e5fc:      	divq	%r15
    e5ff:      	jmp	0xe606 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x206>
    e601:      	xorl	%edx, %edx
    e603:      	divl	%r15d
    e606:      	testb	%r14b, %r14b
    e609:      	movq	0x40(%rsp), %rcx
    e60e:      	cmovneq	0x10(%rsp), %rcx
    e614:      	movq	%rax, (%rcx)
    e617:      	testb	$0x1, %bl
    e61a:      	je	0xe4e0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0xe0>
    e620:      	movq	0x10(%rsp), %rax
    e625:      	imulq	$0x64, 0x8(%r13), %rcx
    e62a:      	imulq	$0x6e, (%rax), %rax
    e62e:      	cmpq	%rcx, %rax
    e631:      	setb	%dl
    e634:      	xorl	%esi, %esi
    e636:      	cmpb	%dl, (%rbp)
    e639:      	setne	%sil
    e63d:      	addq	%rsi, 0x30(%rbp)
    e641:      	cmpq	%rcx, %rax
    e644:      	setb	(%rbp)
    e648:      	jmp	0xe4e0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0xe0>
    e64d:      	vmovdqu	%xmm0, 0x8(%r12)
    e654:      	movq	0x38(%rsp), %r9
    e659:      	movq	0x30(%rsp), %r10
    e65e:      	movq	0x28(%rsp), %r11
    e663:      	movq	0x20(%rsp), %rbx
    e668:      	movq	0x18(%rbp), %rdi
    e66c:      	movq	0x20(%rbp), %rsi
    e670:      	movq	0x28(%rbp), %rdx
    e674:      	movq	0x30(%rbp), %rax
    e678:      	subq	%rbx, %rdi
    e67b:      	subq	%r11, %rsi
    e67e:      	subq	%r10, %rdx
    e681:      	subq	%r9, %rax
    e684:      	movq	%rdi, 0xcb05(%rip)      # 0x1b190 <last_selection>
    e68b:      	movq	%rsi, 0xcb06(%rip)      # 0x1b198 <last_selection+0x8>
    e692:      	movq	%rdx, 0xcb07(%rip)      # 0x1b1a0 <last_selection+0x10>
    e699:      	movq	%rax, 0xcb08(%rip)      # 0x1b1a8 <last_selection+0x18>
    e6a0:      	movq	%r12, %rax
    e6a3:      	addq	$0xc8, %rsp
    e6aa:      	popq	%rbx
    e6ab:      	popq	%r12
    e6ad:      	popq	%r13
    e6af:      	popq	%r14
    e6b1:      	popq	%r15
    e6b3:      	popq	%rbp
    e6b4:      	vzeroupper
    e6b7:      	retq
    e6b8:      	nopl	(%rax,%rax)
