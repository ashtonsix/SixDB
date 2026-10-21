000000000000f480 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    f480:      	vpxor	%xmm0, %xmm0, %xmm0
    f484:      	movq	%rdi, %rax
    f487:      	vmovdqu	%xmm0, 0x8(%rdi)
    f48c:      	cmpq	%rcx, %rdx
    f48f:      	je	0xf5b5 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x135>
    f495:      	pushq	%rbp
    f496:      	pushq	%r15
    f498:      	pushq	%r14
    f49a:      	pushq	%r13
    f49c:      	pushq	%r12
    f49e:      	pushq	%rbx
    f49f:      	subq	$0x18, %rsp
    f4a3:      	vmovdqa	0x7c15(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f4ab:      	movq	0x70(%rsi), %r11
    f4af:      	movq	0x10(%rsi), %rbx
    f4b3:      	movabsq	$-0x61c8864e7a143579, %r14 # imm = 0x9E3779B185EBCA87
    f4bd:      	movq	%rdx, %r15
    f4c0:      	movq	%rsi, %r12
    f4c3:      	movq	%rax, (%rsp)
    f4c7:      	xorl	%ebp, %ebp
    f4c9:      	xorl	%r10d, %r10d
    f4cc:      	movq	%rcx, 0x10(%rsp)
    f4d1:      	movq	%r11, 0x8(%rsp)
    f4d6:      	jmp	0xf4f3 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x73>
    f4d8:      	nopl	(%rax,%rax)
    f4e0:      	incq	%r10
    f4e3:      	addq	%r13, %rbp
    f4e6:      	addq	$0x8, %r15
    f4ea:      	cmpq	%rcx, %r15
    f4ed:      	je	0xf598 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x118>
    f4f3:      	movq	(%r15), %r13
    f4f6:      	movq	%r11, %rdx
    f4f9:      	xorq	%r13, %rdx
    f4fc:      	mulxq	%r14, %rax, %rdx
    f501:      	movl	%eax, %edi
    f503:      	shrl	$0xf, %edi
    f506:      	movl	%eax, %esi
    f508:      	andl	$0x1fffff, %esi         # imm = 0x1FFFFF
    f50e:      	andl	$-0x40, %edi
    f511:      	vpbroadcastd	%esi, %ymm2
    f517:      	vmovdqu	(%rbx,%rdi), %xmm0
    f51c:      	vmovdqu	0x18(%rbx,%rdi), %xmm1
    f522:      	vinserti128	$0x1, 0x8(%rbx,%rdi), %ymm0, %ymm0
    f52a:      	vpshufb	%ymm3, %ymm0, %ymm0
    f52f:      	vpcmpeqd	%ymm0, %ymm2, %k0
    f535:      	vinserti128	$0x1, 0x20(%rbx,%rdi), %ymm1, %ymm0
    f53d:      	vpshufb	%ymm3, %ymm0, %ymm0
    f542:      	vpcmpeqd	%ymm0, %ymm2, %k1
    f548:      	kortestb	%k1, %k0
    f54c:      	jne	0xf4e0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    f54e:      	cmpb	$0x0, 0x3f(%rbx,%rdi)
    f553:      	je	0xf4e6 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x66>
    f555:      	xorq	%rax, %rdx
    f558:      	movq	%r12, %rdi
    f55b:      	movq	%r13, %rsi
    f55e:      	movq	%r10, %r14
    f561:      	vzeroupper
    f564:      	callq	0xdd80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    f569:      	vmovdqa	0x7b4f(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f571:      	movq	0x8(%rsp), %r11
    f576:      	movq	0x10(%rsp), %rcx
    f57b:      	movq	%r14, %r10
    f57e:      	movabsq	$-0x61c8864e7a143579, %r14 # imm = 0x9E3779B185EBCA87
    f588:      	testl	$0x100, %eax            # imm = 0x100
    f58d:      	jne	0xf4e0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    f593:      	jmp	0xf4e6 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x66>
    f598:      	movq	(%rsp), %rax
    f59c:      	movq	%rbp, 0x8(%rax)
    f5a0:      	addq	$0x18, %rsp
    f5a4:      	popq	%rbx
    f5a5:      	popq	%r12
    f5a7:      	popq	%r13
    f5a9:      	popq	%r14
    f5ab:      	popq	%r15
    f5ad:      	popq	%rbp
    f5ae:      	movq	%r10, (%rax)
    f5b1:      	vzeroupper
    f5b4:      	retq
    f5b5:      	xorl	%r10d, %r10d
    f5b8:      	movq	%r10, (%rax)
    f5bb:      	retq
    f5bc:      	nopl	(%rax)
