000000000000e540 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    e540:      	vpxor	%xmm0, %xmm0, %xmm0
    e544:      	movq	%rdi, %rax
    e547:      	vmovdqu	%xmm0, 0x8(%rdi)
    e54c:      	cmpq	%rcx, %rdx
    e54f:      	je	0xe675 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x135>
    e555:      	pushq	%rbp
    e556:      	pushq	%r15
    e558:      	pushq	%r14
    e55a:      	pushq	%r13
    e55c:      	pushq	%r12
    e55e:      	pushq	%rbx
    e55f:      	subq	$0x18, %rsp
    e563:      	vmovdqa	0x7b55(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    e56b:      	movq	0x70(%rsi), %r11
    e56f:      	movq	0x10(%rsi), %rbx
    e573:      	movabsq	$-0x61c8864e7a143579, %r14 # imm = 0x9E3779B185EBCA87
    e57d:      	movq	%rdx, %r15
    e580:      	movq	%rsi, %r12
    e583:      	movq	%rax, (%rsp)
    e587:      	xorl	%ebp, %ebp
    e589:      	xorl	%r10d, %r10d
    e58c:      	movq	%rcx, 0x10(%rsp)
    e591:      	movq	%r11, 0x8(%rsp)
    e596:      	jmp	0xe5b3 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x73>
    e598:      	nopl	(%rax,%rax)
    e5a0:      	incq	%r10
    e5a3:      	addq	%r13, %rbp
    e5a6:      	addq	$0x8, %r15
    e5aa:      	cmpq	%rcx, %r15
    e5ad:      	je	0xe658 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x118>
    e5b3:      	movq	(%r15), %r13
    e5b6:      	movq	%r11, %rdx
    e5b9:      	xorq	%r13, %rdx
    e5bc:      	mulxq	%r14, %rax, %rdx
    e5c1:      	movl	%eax, %edi
    e5c3:      	shrl	$0xf, %edi
    e5c6:      	movl	%eax, %esi
    e5c8:      	andl	$0x1fffff, %esi         # imm = 0x1FFFFF
    e5ce:      	andl	$-0x40, %edi
    e5d1:      	vpbroadcastd	%esi, %ymm2
    e5d7:      	vmovdqu	(%rbx,%rdi), %xmm0
    e5dc:      	vmovdqu	0x18(%rbx,%rdi), %xmm1
    e5e2:      	vinserti128	$0x1, 0x8(%rbx,%rdi), %ymm0, %ymm0
    e5ea:      	vpshufb	%ymm3, %ymm0, %ymm0
    e5ef:      	vpcmpeqd	%ymm0, %ymm2, %k0
    e5f5:      	vinserti128	$0x1, 0x20(%rbx,%rdi), %ymm1, %ymm0
    e5fd:      	vpshufb	%ymm3, %ymm0, %ymm0
    e602:      	vpcmpeqd	%ymm0, %ymm2, %k1
    e608:      	kortestb	%k1, %k0
    e60c:      	jne	0xe5a0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    e60e:      	cmpb	$0x0, 0x3f(%rbx,%rdi)
    e613:      	je	0xe5a6 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x66>
    e615:      	xorq	%rax, %rdx
    e618:      	movq	%r12, %rdi
    e61b:      	movq	%r13, %rsi
    e61e:      	movq	%r10, %r14
    e621:      	vzeroupper
    e624:      	callq	0xdd00 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    e629:      	vmovdqa	0x7a8f(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    e631:      	movq	0x8(%rsp), %r11
    e636:      	movq	0x10(%rsp), %rcx
    e63b:      	movq	%r14, %r10
    e63e:      	movabsq	$-0x61c8864e7a143579, %r14 # imm = 0x9E3779B185EBCA87
    e648:      	testl	$0x100, %eax            # imm = 0x100
    e64d:      	jne	0xe5a0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    e653:      	jmp	0xe5a6 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x66>
    e658:      	movq	(%rsp), %rax
    e65c:      	movq	%rbp, 0x8(%rax)
    e660:      	addq	$0x18, %rsp
    e664:      	popq	%rbx
    e665:      	popq	%r12
    e667:      	popq	%r13
    e669:      	popq	%r14
    e66b:      	popq	%r15
    e66d:      	popq	%rbp
    e66e:      	movq	%r10, (%rax)
    e671:      	vzeroupper
    e674:      	retq
    e675:      	xorl	%r10d, %r10d
    e678:      	movq	%r10, (%rax)
    e67b:      	retq
    e67c:      	nopl	(%rax)
