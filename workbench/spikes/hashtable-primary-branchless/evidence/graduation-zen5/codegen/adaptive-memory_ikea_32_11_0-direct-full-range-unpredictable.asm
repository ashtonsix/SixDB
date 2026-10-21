000000000000f2c0 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    f2c0:      	pushq	%rbp
    f2c1:      	pushq	%r15
    f2c3:      	pushq	%r14
    f2c5:      	pushq	%r13
    f2c7:      	pushq	%r12
    f2c9:      	pushq	%rbx
    f2ca:      	subq	$0x28, %rsp
    f2ce:      	vpxor	%xmm0, %xmm0, %xmm0
    f2d2:      	movq	%rdi, %rax
    f2d5:      	vmovdqu	%xmm0, 0x8(%rdi)
    f2da:      	cmpq	%rcx, %rdx
    f2dd:      	je	0xf445 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x185>
    f2e3:      	vmovdqa	0x7dd5(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f2eb:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    f2f5:      	movq	%rdx, %rbx
    f2f8:      	xorl	%ebp, %ebp
    f2fa:      	xorl	%r13d, %r13d
    f2fd:      	nopl	(%rax)
    f300:      	movq	(%rbx), %r14
    f303:      	movq	0x70(%rsi), %rdx
    f307:      	movq	0x10(%rsi), %rdi
    f30b:      	xorq	%r14, %rdx
    f30e:      	mulxq	%r9, %r15, %r10
    f313:      	movl	%r15d, %r8d
    f316:      	shrl	$0xf, %r8d
    f31a:      	movl	%r15d, %edx
    f31d:      	andl	$0x1fffff, %edx         # imm = 0x1FFFFF
    f323:      	andl	$-0x40, %r8d
    f327:      	leaq	(%rdi,%r8), %r12
    f32b:      	movzbl	0x3f(%rdi,%r8), %edi
    f331:      	testl	%edi, %edi
    f333:      	jne	0xf399 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xd9>
    f335:      	vmovdqu	(%r12), %xmm0
    f33b:      	vmovdqu	0x18(%r12), %xmm1
    f342:      	vpbroadcastd	%edx, %ymm2
    f348:      	xorl	%edi, %edi
    f34a:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    f352:      	vpshufb	%ymm3, %ymm0, %ymm0
    f357:      	vpcmpeqd	%ymm0, %ymm2, %k0
    f35d:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    f365:      	vpshufb	%ymm3, %ymm0, %ymm0
    f36a:      	vpcmpeqd	%ymm0, %ymm2, %k1
    f370:      	kortestb	%k1, %k0
    f374:      	setne	%dil
    f378:      	movzwl	%di, %edx
    f37b:      	addq	$0x8, %rbx
    f37f:      	addq	%rdx, %r13
    f382:      	negq	%rdx
    f385:      	andq	%rdx, %r14
    f388:      	addq	%r14, %rbp
    f38b:      	cmpq	%rcx, %rbx
    f38e:      	jne	0xf300 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x40>
    f394:      	jmp	0xf43f <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x17f>
    f399:      	movq	%rsi, 0x10(%rsp)
    f39e:      	movq	%r10, 0x20(%rsp)
    f3a3:      	movq	%r12, %rdi
    f3a6:      	movq	%rdx, %rsi
    f3a9:      	movq	%rax, 0x18(%rsp)
    f3ae:      	movq	%rcx, 0x8(%rsp)
    f3b3:      	vzeroupper
    f3b6:      	callq	0xdd00 <ikea::hashtable::detail::bucket_ops<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    f3bb:      	vmovdqa	0x7cfd(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f3c3:      	movq	%rax, %rdx
    f3c6:      	movq	0x10(%rsp), %rsi
    f3cb:      	movq	0x8(%rsp), %rcx
    f3d0:      	movq	0x18(%rsp), %rax
    f3d5:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    f3df:      	movw	$0x1, %di
    f3e3:      	testq	%rdx, %rdx
    f3e6:      	jne	0xf378 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xb8>
    f3e8:      	cmpb	$0x0, 0x3f(%r12)
    f3ee:      	je	0xf438 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x178>
    f3f0:      	movq	0x20(%rsp), %rdx
    f3f5:      	movq	%rsi, %rdi
    f3f8:      	movq	%r14, %rsi
    f3fb:      	xorq	%r15, %rdx
    f3fe:      	vzeroupper
    f401:      	callq	0xdd80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    f406:      	vmovdqa	0x7cb2(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f40e:      	movl	%eax, %edx
    f410:      	movq	0x10(%rsp), %rsi
    f415:      	movq	0x8(%rsp), %rcx
    f41a:      	movq	0x18(%rsp), %rax
    f41f:      	movl	$0x108, %edi            # imm = 0x108
    f424:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    f42e:      	bextrl	%edi, %edx, %edi
    f433:      	jmp	0xf378 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xb8>
    f438:      	xorl	%edi, %edi
    f43a:      	jmp	0xf378 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xb8>
    f43f:      	movq	%rbp, 0x8(%rax)
    f443:      	jmp	0xf448 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x188>
    f445:      	xorl	%r13d, %r13d
    f448:      	movq	%r13, (%rax)
    f44b:      	addq	$0x28, %rsp
    f44f:      	popq	%rbx
    f450:      	popq	%r12
    f452:      	popq	%r13
    f454:      	popq	%r14
    f456:      	popq	%r15
    f458:      	popq	%rbp
    f459:      	vzeroupper
    f45c:      	retq
    f45d:      	nopw	%cs:(%rax,%rax)
    f467:      	nopw	%cs:(%rax,%rax)
    f471:      	nopw	%cs:(%rax,%rax)
    f47b:      	nopl	(%rax,%rax)
