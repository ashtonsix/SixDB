000000000000d9c0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    d9c0:      	vpxor	%xmm0, %xmm0, %xmm0
    d9c4:      	vmovdqu	%xmm0, 0x8(%rdi)
    d9c9:      	movq	%rdi, %rax
    d9cc:      	movq	(%rdx), %rcx
    d9cf:      	cmpq	%rcx, 0x8(%rdx)
    d9d3:      	je	0xdb9e <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1de>
    d9d9:      	pushq	%rbp
    d9da:      	pushq	%r15
    d9dc:      	pushq	%r14
    d9de:      	pushq	%r13
    d9e0:      	pushq	%r12
    d9e2:      	pushq	%rbx
    d9e3:      	subq	$0x28, %rsp
    d9e7:      	vmovdqa	0x8731(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    d9ef:      	movq	%rax, 0x20(%rsp)
    d9f4:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    d9fe:      	leaq	0x9363(%rip), %r11      # 0x16d68 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup(unsigned long) const::zero>
    da05:      	movq	%rdx, %r8
    da08:      	xorl	%ebp, %ebp
    da0a:      	xorl	%ebx, %ebx
    da0c:      	xorl	%r15d, %r15d
    da0f:      	xorl	%r9d, %r9d
    da12:      	nopw	%cs:(%rax,%rax)
    da20:      	movq	(%rcx,%r15,8), %r14
    da24:      	movq	0x70(%rsi), %rdx
    da28:      	movq	0x10(%rsi), %rdi
    da2c:      	xorq	%r14, %rdx
    da2f:      	mulxq	%rax, %r13, %r10
    da34:      	movl	%r13d, %edx
    da37:      	shrl	$0x15, %edx
    da3a:      	movl	%r13d, %ecx
    da3d:      	andl	$0x1fffff, %ecx         # imm = 0x1FFFFF
    da43:      	leaq	(%rdx,%rdx,2), %rdx
    da47:      	shll	$0x6, %edx
    da4a:      	leaq	(%rdi,%rdx), %r12
    da4e:      	movzbl	0x3f(%rdi,%rdx), %edx
    da53:      	testl	%edx, %edx
    da55:      	jne	0xdae4 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x124>
    da5b:      	vmovdqu	(%r12), %xmm0
    da61:      	vmovdqu	0x18(%r12), %xmm1
    da68:      	vpbroadcastd	%ecx, %ymm2
    da6e:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    da76:      	vpshufb	%ymm3, %ymm0, %ymm0
    da7b:      	vpcmpeqd	%ymm0, %ymm2, %k0
    da81:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    da89:      	addq	$0x40, %r12
    da8d:      	vpshufb	%ymm3, %ymm0, %ymm0
    da92:      	vpcmpeqd	%ymm0, %ymm2, %k1
    da98:      	kunpckbw	%k0, %k1, %k0
    da9c:      	kmovw	%k0, %ecx
    daa0:      	tzcntq	%rcx, %rdi
    daa5:      	leaq	(%r12,%rdi,8), %rdi
    daa9:      	cmovbq	%r11, %rdi
    daad:      	setae	%dl
    dab0:      	movq	(%rdi), %rcx
    dab3:      	addq	%rcx, %rbp
    dab6:      	movzbl	%dl, %ecx
    dab9:      	movq	0x8(%r8), %rdx
    dabd:      	incq	%r15
    dac0:      	addq	%rcx, %r9
    dac3:      	negq	%rcx
    dac6:      	andq	%rcx, %r14
    dac9:      	movq	(%r8), %rcx
    dacc:      	addq	%r14, %rbx
    dacf:      	subq	%rcx, %rdx
    dad2:      	sarq	$0x3, %rdx
    dad6:      	cmpq	%rdx, %r15
    dad9:      	jb	0xda20 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x60>
    dadf:      	jmp	0xdb7c <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1bc>
    dae4:      	movq	%rsi, 0x10(%rsp)
    dae9:      	movq	%r10, 0x18(%rsp)
    daee:      	movq	%r9, (%rsp)
    daf2:      	movq	%r8, 0x8(%rsp)
    daf7:      	movq	%r12, %rdi
    dafa:      	movq	%rcx, %rsi
    dafd:      	vzeroupper
    db00:      	callq	0xe300 <ikea::hashtable::detail::bucket_ops<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    db05:      	testq	%rax, %rax
    db08:      	je	0xdb1d <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x15d>
    db0a:      	tzcntq	%rax, %rax
    db0f:      	movb	$0x1, %dl
    db11:      	movq	0x40(%r12,%rax,8), %rcx
    db16:      	movq	0x10(%rsp), %rsi
    db1b:      	jmp	0xdb4f <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x18f>
    db1d:      	cmpb	$0x0, 0x3f(%r12)
    db23:      	je	0xdb76 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1b6>
    db25:      	movq	0x18(%rsp), %rdx
    db2a:      	movq	0x10(%rsp), %r12
    db2f:      	movq	%r14, %rsi
    db32:      	xorq	%r13, %rdx
    db35:      	movq	%r12, %rdi
    db38:      	callq	0xe380 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    db3d:      	movq	%rax, %rcx
    db40:      	andb	$0x1, %dl
    db43:      	movl	$0x0, %eax
    db48:      	movq	%r12, %rsi
    db4b:      	cmoveq	%rax, %rcx
    db4f:      	vmovdqa	0x85c9(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    db57:      	movq	0x8(%rsp), %r8
    db5c:      	movq	(%rsp), %r9
    db60:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    db6a:      	leaq	0x91f7(%rip), %r11      # 0x16d68 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup(unsigned long) const::zero>
    db71:      	jmp	0xdab3 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0xf3>
    db76:      	xorl	%edx, %edx
    db78:      	xorl	%ecx, %ecx
    db7a:      	jmp	0xdb16 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x156>
    db7c:      	movq	0x20(%rsp), %rax
    db81:      	movq	%rbx, 0x8(%rax)
    db85:      	movq	%rbp, 0x10(%rax)
    db89:      	addq	$0x28, %rsp
    db8d:      	popq	%rbx
    db8e:      	popq	%r12
    db90:      	popq	%r13
    db92:      	popq	%r14
    db94:      	popq	%r15
    db96:      	popq	%rbp
    db97:      	movq	%r9, (%rax)
    db9a:      	vzeroupper
    db9d:      	retq
    db9e:      	xorl	%r9d, %r9d
    dba1:      	movq	%r9, (%rax)
    dba4:      	retq
    dba5:      	nopw	%cs:(%rax,%rax)
    dbaf:      	nopw	%cs:(%rax,%rax)
    dbb9:      	nopl	(%rax)

