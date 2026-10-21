000000000000f700 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    f700:      	vpxor	%xmm0, %xmm0, %xmm0
    f704:      	movq	%rdi, %rax
    f707:      	vmovdqu	%xmm0, 0x8(%rdi)
    f70c:      	cmpq	%rcx, %rdx
    f70f:      	je	0xf905 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x205>
    f715:      	pushq	%rbp
    f716:      	pushq	%r15
    f718:      	pushq	%r14
    f71a:      	pushq	%r13
    f71c:      	pushq	%r12
    f71e:      	pushq	%rbx
    f71f:      	subq	$0x38, %rsp
    f723:      	vmovdqa	0x89f5(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    f72b:      	movq	%rax, 0x30(%rsp)
    f730:      	subq	%rdx, %r8
    f733:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    f73d:      	movq	%rdx, %rbx
    f740:      	xorl	%ebp, %ebp
    f742:      	xorl	%r12d, %r12d
    f745:      	xorl	%r10d, %r10d
    f748:      	nopl	(%rax,%rax)
    f750:      	movq	0x10(%rsi), %rdi
    f754:      	movq	0x70(%rsi), %rdx
    f758:      	cmpq	$0x101, %r8             # imm = 0x101
    f75f:      	jl	0xf783 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x83>
    f761:      	movl	0x100(%rbx), %r9d
    f768:      	xorl	%edx, %r9d
    f76b:      	imull	$0x85ebca87, %r9d, %r9d # imm = 0x85EBCA87
    f772:      	shrl	$0x15, %r9d
    f776:      	leaq	(%r9,%r9,2), %r9
    f77a:      	shll	$0x6, %r9d
    f77e:      	prefetcht0	(%rdi,%r9)
    f783:      	movq	(%rbx), %r14
    f786:      	xorq	%r14, %rdx
    f789:      	mulxq	%rax, %r13, %r11
    f78e:      	movl	%r13d, %r9d
    f791:      	shrl	$0x15, %r9d
    f795:      	movl	%r13d, %edx
    f798:      	andl	$0x1fffff, %edx         # imm = 0x1FFFFF
    f79e:      	leaq	(%r9,%r9,2), %r9
    f7a2:      	shll	$0x6, %r9d
    f7a6:      	leaq	(%rdi,%r9), %r15
    f7aa:      	movzbl	0x3f(%rdi,%r9), %edi
    f7b0:      	testl	%edi, %edi
    f7b2:      	jne	0xf83b <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x13b>
    f7b8:      	vmovdqu	(%r15), %xmm0
    f7bd:      	vmovdqu	0x18(%r15), %xmm1
    f7c3:      	vpbroadcastd	%edx, %ymm2
    f7c9:      	leaq	0x95f0(%rip), %r9       # 0x18dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup_value ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::get<(ikea::hashtable::lookup_hint)1>(unsigned long) const::zero>
    f7d0:      	vinserti128	$0x1, 0x8(%r15), %ymm0, %ymm0
    f7d7:      	vpshufb	%ymm3, %ymm0, %ymm0
    f7dc:      	vpcmpeqd	%ymm0, %ymm2, %k0
    f7e2:      	vinserti128	$0x1, 0x20(%r15), %ymm1, %ymm0
    f7e9:      	addq	$0x40, %r15
    f7ed:      	vpshufb	%ymm3, %ymm0, %ymm0
    f7f2:      	vpcmpeqd	%ymm0, %ymm2, %k1
    f7f8:      	kunpckbw	%k0, %k1, %k0
    f7fc:      	kmovw	%k0, %edx
    f800:      	tzcntq	%rdx, %rdi
    f805:      	leaq	(%r15,%rdi,8), %rdi
    f809:      	cmovbq	%r9, %rdi
    f80d:      	setae	%dl
    f810:      	movq	(%rdi), %rdi
    f813:      	movzbl	%dl, %edx
    f816:      	addq	%rdi, %rbp
    f819:      	addq	$0x8, %rbx
    f81d:      	addq	$-0x8, %r8
    f821:      	addq	%rdx, %r10
    f824:      	negq	%rdx
    f827:      	andq	%rdx, %r14
    f82a:      	addq	%r14, %r12
    f82d:      	cmpq	%rcx, %rbx
    f830:      	jne	0xf750 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x50>
    f836:      	jmp	0xf8e3 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1e3>
    f83b:      	movq	%rsi, 0x20(%rsp)
    f840:      	movq	%r11, 0x28(%rsp)
    f845:      	movq	%r10, 0x8(%rsp)
    f84a:      	movq	%rcx, 0x18(%rsp)
    f84f:      	movq	%r8, 0x10(%rsp)
    f854:      	movq	%r15, %rdi
    f857:      	movq	%rdx, %rsi
    f85a:      	vzeroupper
    f85d:      	callq	0xe400 <ikea::hashtable::detail::bucket_ops<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    f862:      	testq	%rax, %rax
    f865:      	je	0xf884 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x184>
    f867:      	tzcntq	%rax, %rax
    f86c:      	movq	0x10(%rsp), %r8
    f871:      	movq	0x18(%rsp), %rcx
    f876:      	movq	0x20(%rsp), %rsi
    f87b:      	movb	$0x1, %dl
    f87d:      	movq	0x40(%r15,%rax,8), %rdi
    f882:      	jmp	0xf8c7 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1c7>
    f884:      	cmpb	$0x0, 0x3f(%r15)
    f889:      	movq	0x20(%rsp), %rsi
    f88e:      	je	0xf8b9 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1b9>
    f890:      	movq	0x28(%rsp), %rdx
    f895:      	movq	%rsi, %rdi
    f898:      	movq	%r14, %rsi
    f89b:      	xorq	%r13, %rdx
    f89e:      	callq	0xe480 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    f8a3:      	movq	0x20(%rsp), %rsi
    f8a8:      	movq	%rax, %rdi
    f8ab:      	andb	$0x1, %dl
    f8ae:      	movl	$0x0, %eax
    f8b3:      	cmoveq	%rax, %rdi
    f8b7:      	jmp	0xf8bd <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1bd>
    f8b9:      	xorl	%edx, %edx
    f8bb:      	xorl	%edi, %edi
    f8bd:      	movq	0x10(%rsp), %r8
    f8c2:      	movq	0x18(%rsp), %rcx
    f8c7:      	vmovdqa	0x8851(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    f8cf:      	movq	0x8(%rsp), %r10
    f8d4:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    f8de:      	jmp	0xf813 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x113>
    f8e3:      	movq	0x30(%rsp), %rax
    f8e8:      	movq	%r12, 0x8(%rax)
    f8ec:      	movq	%rbp, 0x10(%rax)
    f8f0:      	addq	$0x38, %rsp
    f8f4:      	popq	%rbx
    f8f5:      	popq	%r12
    f8f7:      	popq	%r13
    f8f9:      	popq	%r14
    f8fb:      	popq	%r15
    f8fd:      	popq	%rbp
    f8fe:      	movq	%r10, (%rax)
    f901:      	vzeroupper
    f904:      	retq
    f905:      	xorl	%r10d, %r10d
    f908:      	movq	%r10, (%rax)
    f90b:      	retq
    f90c:      	nopw	%cs:(%rax,%rax)
    f916:      	nopw	%cs:(%rax,%rax)
    f920:      	nopw	%cs:(%rax,%rax)
    f92a:      	nopw	%cs:(%rax,%rax)
    f934:      	nopw	%cs:(%rax,%rax)
    f93e:      	nop
