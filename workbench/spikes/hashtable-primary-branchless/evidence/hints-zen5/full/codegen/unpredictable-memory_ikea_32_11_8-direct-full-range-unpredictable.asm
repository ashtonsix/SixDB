000000000000f240 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    f240:      	pushq	%rbp
    f241:      	pushq	%r15
    f243:      	pushq	%r14
    f245:      	pushq	%r13
    f247:      	pushq	%r12
    f249:      	pushq	%rbx
    f24a:      	subq	$0x18, %rsp
    f24e:      	vpxor	%xmm0, %xmm0, %xmm0
    f252:      	movq	%rdi, %rax
    f255:      	vmovdqu	%xmm0, 0x8(%rdi)
    f25a:      	cmpq	%rcx, %rdx
    f25d:      	je	0xf3d1 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x191>
    f263:      	vmovdqa	0x7eb5(%rip), %ymm3     # 0x17120 <syscall@GLIBC_2.2.5+0x17120>
    f26b:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    f275:      	leaq	0x8b44(%rip), %r10      # 0x17dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup(unsigned long) const::zero>
    f27c:      	movq	%rdx, %rbx
    f27f:      	xorl	%r13d, %r13d
    f282:      	nopw	%cs:(%rax,%rax)
    f290:      	movq	(%rbx), %r14
    f293:      	movq	0x70(%rsi), %rdx
    f297:      	movq	0x10(%rsi), %r8
    f29b:      	xorq	%r14, %rdx
    f29e:      	mulxq	%r9, %rbp, %r15
    f2a3:      	movl	%ebp, %edi
    f2a5:      	shrl	$0x15, %edi
    f2a8:      	movl	%ebp, %edx
    f2aa:      	andl	$0x1fffff, %edx         # imm = 0x1FFFFF
    f2b0:      	leaq	(%rdi,%rdi,2), %rdi
    f2b4:      	shll	$0x6, %edi
    f2b7:      	leaq	(%r8,%rdi), %r12
    f2bb:      	movzbl	0x3f(%r8,%rdi), %edi
    f2c1:      	testl	%edi, %edi
    f2c3:      	jne	0xf332 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xf2>
    f2c5:      	vmovdqu	(%r12), %xmm0
    f2cb:      	vmovdqu	0x18(%r12), %xmm1
    f2d2:      	vpbroadcastd	%edx, %ymm2
    f2d8:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    f2e0:      	vpshufb	%ymm3, %ymm0, %ymm0
    f2e5:      	vpcmpeqd	%ymm0, %ymm2, %k0
    f2eb:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    f2f3:      	addq	$0x40, %r12
    f2f7:      	vpshufb	%ymm3, %ymm0, %ymm0
    f2fc:      	vpcmpeqd	%ymm0, %ymm2, %k1
    f302:      	kunpckbw	%k0, %k1, %k0
    f306:      	kmovw	%k0, %edx
    f30a:      	tzcntq	%rdx, %rdi
    f30f:      	leaq	(%r12,%rdi,8), %rdi
    f313:      	cmovbq	%r10, %rdi
    f317:      	setae	%dl
    f31a:      	movzbl	%dl, %edx
    f31d:      	addq	$0x8, %rbx
    f321:      	addq	%rdx, %r13
    f324:      	cmpq	%rcx, %rbx
    f327:      	jne	0xf290 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x50>
    f32d:      	jmp	0xf3d4 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x194>
    f332:      	movq	%rsi, 0x10(%rsp)
    f337:      	movq	%r12, %rdi
    f33a:      	movq	%rdx, %rsi
    f33d:      	movq	%rax, 0x8(%rsp)
    f342:      	movq	%rcx, (%rsp)
    f346:      	vzeroupper
    f349:      	callq	0xe400 <ikea::hashtable::detail::bucket_ops<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    f34e:      	vmovdqa	0x7dca(%rip), %ymm3     # 0x17120 <syscall@GLIBC_2.2.5+0x17120>
    f356:      	movq	%rax, %rdi
    f359:      	movq	0x10(%rsp), %rsi
    f35e:      	movq	(%rsp), %rcx
    f362:      	movq	0x8(%rsp), %rax
    f367:      	leaq	0x8a52(%rip), %r10      # 0x17dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup(unsigned long) const::zero>
    f36e:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    f378:      	movb	$0x1, %dl
    f37a:      	testq	%rdi, %rdi
    f37d:      	jne	0xf31a <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xda>
    f37f:      	cmpb	$0x0, 0x3f(%r12)
    f385:      	je	0xf3ca <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x18a>
    f387:      	xorq	%rbp, %r15
    f38a:      	movq	%rsi, %rdi
    f38d:      	movq	%r14, %rsi
    f390:      	movq	%r15, %rdx
    f393:      	vzeroupper
    f396:      	callq	0xe480 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    f39b:      	vmovdqa	0x7d7d(%rip), %ymm3     # 0x17120 <syscall@GLIBC_2.2.5+0x17120>
    f3a3:      	movq	0x10(%rsp), %rsi
    f3a8:      	movq	(%rsp), %rcx
    f3ac:      	movq	0x8(%rsp), %rax
    f3b1:      	leaq	0x8a08(%rip), %r10      # 0x17dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup(unsigned long) const::zero>
    f3b8:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    f3c2:      	andb	$0x1, %dl
    f3c5:      	jmp	0xf31a <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xda>
    f3ca:      	xorl	%edx, %edx
    f3cc:      	jmp	0xf31a <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xda>
    f3d1:      	xorl	%r13d, %r13d
    f3d4:      	movq	%r13, (%rax)
    f3d7:      	addq	$0x18, %rsp
    f3db:      	popq	%rbx
    f3dc:      	popq	%r12
    f3de:      	popq	%r13
    f3e0:      	popq	%r14
    f3e2:      	popq	%r15
    f3e4:      	popq	%rbp
    f3e5:      	vzeroupper
    f3e8:      	retq
    f3e9:      	nopw	%cs:(%rax,%rax)
    f3f3:      	nopw	%cs:(%rax,%rax)
    f3fd:      	nopl	(%rax)
