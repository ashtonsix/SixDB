000000000000d380 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    d380:      	pushq	%rbp
    d381:      	pushq	%r15
    d383:      	pushq	%r14
    d385:      	pushq	%r13
    d387:      	pushq	%r12
    d389:      	pushq	%rbx
    d38a:      	subq	$0x28, %rsp
    d38e:      	vpxor	%xmm0, %xmm0, %xmm0
    d392:      	vmovdqu	%xmm0, 0x8(%rdi)
    d397:      	movq	%rdi, %rax
    d39a:      	movq	(%rdx), %rcx
    d39d:      	cmpq	%rcx, 0x8(%rdx)
    d3a1:      	je	0xd51f <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x19f>
    d3a7:      	vmovdqa	0x8d11(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    d3af:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    d3b9:      	movq	%rdx, %r8
    d3bc:      	xorl	%r13d, %r13d
    d3bf:      	xorl	%ebx, %ebx
    d3c1:      	xorl	%ebp, %ebp
    d3c3:      	nopw	%cs:(%rax,%rax)
    d3d0:      	movq	(%rcx,%rbx,8), %r14
    d3d4:      	movq	0x70(%rsi), %rdx
    d3d8:      	xorq	%r14, %rdx
    d3db:      	mulxq	%r9, %r15, %r10
    d3e0:      	movq	0x10(%rsi), %rdx
    d3e4:      	movl	%r15d, %edi
    d3e7:      	shrl	$0xf, %edi
    d3ea:      	movl	%r15d, %ecx
    d3ed:      	andl	$0x1fffff, %ecx         # imm = 0x1FFFFF
    d3f3:      	andl	$-0x40, %edi
    d3f6:      	leaq	(%rdx,%rdi), %r12
    d3fa:      	movzbl	0x3f(%rdx,%rdi), %edx
    d3ff:      	testl	%edx, %edx
    d401:      	jne	0xd473 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0xf3>
    d403:      	vmovdqu	(%r12), %xmm0
    d409:      	vmovdqu	0x18(%r12), %xmm1
    d410:      	vpbroadcastd	%ecx, %ymm2
    d416:      	xorl	%edx, %edx
    d418:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    d420:      	vpshufb	%ymm3, %ymm0, %ymm0
    d425:      	vpcmpeqd	%ymm0, %ymm2, %k0
    d42b:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    d433:      	vpshufb	%ymm3, %ymm0, %ymm0
    d438:      	vpcmpeqd	%ymm0, %ymm2, %k1
    d43e:      	kortestb	%k1, %k0
    d442:      	setne	%dl
    d445:      	movzwl	%dx, %ecx
    d448:      	movq	0x8(%r8), %rdx
    d44c:      	incq	%rbx
    d44f:      	addq	%rcx, %rbp
    d452:      	negq	%rcx
    d455:      	andq	%rcx, %r14
    d458:      	movq	(%r8), %rcx
    d45b:      	addq	%r14, %r13
    d45e:      	subq	%rcx, %rdx
    d461:      	sarq	$0x3, %rdx
    d465:      	cmpq	%rdx, %rbx
    d468:      	jb	0xd3d0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x50>
    d46e:      	jmp	0xd519 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x199>
    d473:      	movq	%rsi, 0x10(%rsp)
    d478:      	movq	%r10, 0x20(%rsp)
    d47d:      	movq	%r12, %rdi
    d480:      	movq	%rcx, %rsi
    d483:      	movq	%rax, 0x18(%rsp)
    d488:      	movq	%r8, 0x8(%rsp)
    d48d:      	vzeroupper
    d490:      	callq	0xdc00 <ikea::hashtable::detail::bucket_ops<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    d495:      	vmovdqa	0x8c23(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    d49d:      	movq	%rax, %rcx
    d4a0:      	movq	0x8(%rsp), %r8
    d4a5:      	movq	0x10(%rsp), %rsi
    d4aa:      	movq	0x18(%rsp), %rax
    d4af:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    d4b9:      	movw	$0x1, %dx
    d4bd:      	testq	%rcx, %rcx
    d4c0:      	jne	0xd445 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0xc5>
    d4c2:      	cmpb	$0x0, 0x3f(%r12)
    d4c8:      	je	0xd512 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x192>
    d4ca:      	movq	0x20(%rsp), %rdx
    d4cf:      	movq	%rsi, %rdi
    d4d2:      	movq	%r14, %rsi
    d4d5:      	xorq	%r15, %rdx
    d4d8:      	vzeroupper
    d4db:      	callq	0xdc80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    d4e0:      	vmovdqa	0x8bd8(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    d4e8:      	movl	%eax, %ecx
    d4ea:      	movq	0x8(%rsp), %r8
    d4ef:      	movq	0x10(%rsp), %rsi
    d4f4:      	movq	0x18(%rsp), %rax
    d4f9:      	movl	$0x108, %edx            # imm = 0x108
    d4fe:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    d508:      	bextrl	%edx, %ecx, %edx
    d50d:      	jmp	0xd445 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0xc5>
    d512:      	xorl	%edx, %edx
    d514:      	jmp	0xd445 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0xc5>
    d519:      	movq	%r13, 0x8(%rax)
    d51d:      	jmp	0xd521 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1a1>
    d51f:      	xorl	%ebp, %ebp
    d521:      	movq	%rbp, (%rax)
    d524:      	addq	$0x28, %rsp
    d528:      	popq	%rbx
    d529:      	popq	%r12
    d52b:      	popq	%r13
    d52d:      	popq	%r14
    d52f:      	popq	%r15
    d531:      	popq	%rbp
    d532:      	vzeroupper
    d535:      	retq
    d536:      	nopw	%cs:(%rax,%rax)

