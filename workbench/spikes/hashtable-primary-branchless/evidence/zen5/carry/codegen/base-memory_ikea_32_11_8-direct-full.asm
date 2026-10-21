000000000000d9c0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    d9c0:      	pushq	%rbp
    d9c1:      	pushq	%r15
    d9c3:      	pushq	%r14
    d9c5:      	pushq	%r13
    d9c7:      	pushq	%r12
    d9c9:      	pushq	%rbx
    d9ca:      	subq	$0x28, %rsp
    d9ce:      	vpxor	%xmm0, %xmm0, %xmm0
    d9d2:      	vmovdqu	%xmm0, 0x8(%rdi)
    d9d7:      	movq	%rdi, %rax
    d9da:      	movq	(%rdx), %r9
    d9dd:      	movq	0x8(%rdx), %r10
    d9e1:      	subq	%r9, %r10
    d9e4:      	je	0xdb31 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x171>
    d9ea:      	vmovdqa	0x872e(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    d9f2:      	movq	0x70(%rsi), %r11
    d9f6:      	movq	0x10(%rsi), %rbx
    d9fa:      	sarq	$0x3, %r10
    d9fe:      	movq	%rsi, %r8
    da01:      	movq	%rax, (%rsp)
    da05:      	xorl	%r12d, %r12d
    da08:      	xorl	%r14d, %r14d
    da0b:      	xorl	%r13d, %r13d
    da0e:      	xorl	%ebp, %ebp
    da10:      	movq	%rsi, 0x20(%rsp)
    da15:      	movq	%r9, 0x18(%rsp)
    da1a:      	movq	%r10, 0x10(%rsp)
    da1f:      	movq	%r11, 0x8(%rsp)
    da24:      	jmp	0xda5d <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x9d>
    da26:      	nopw	%cs:(%rax,%rax)
    da30:      	kmovd	%k0, %eax
    da34:      	tzcntl	%eax, %eax
    da38:      	movq	0x40(%rcx,%rax,8), %rax
    da3d:      	movl	$0x1, %ecx
    da42:      	addq	%rcx, %rbp
    da45:      	negq	%rcx
    da48:      	addq	%rax, %r12
    da4b:      	incq	%r13
    da4e:      	andq	%rcx, %r15
    da51:      	addq	%r15, %r14
    da54:      	cmpq	%r13, %r10
    da57:      	je	0xdb23 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x163>
    da5d:      	movq	(%r9,%r13,8), %r15
    da61:      	movq	%r11, %rdx
    da64:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    da6e:      	xorq	%r15, %rdx
    da71:      	mulxq	%rax, %rax, %rdx
    da76:      	movl	%eax, %ecx
    da78:      	shrl	$0x15, %ecx
    da7b:      	movl	%eax, %edi
    da7d:      	andl	$0x1fffff, %edi         # imm = 0x1FFFFF
    da83:      	leaq	(%rcx,%rcx,2), %rsi
    da87:      	vpbroadcastd	%edi, %ymm2
    da8d:      	shll	$0x6, %esi
    da90:      	vmovdqu	(%rbx,%rsi), %xmm0
    da95:      	vmovdqu	0x18(%rbx,%rsi), %xmm1
    da9b:      	leaq	(%rbx,%rsi), %rcx
    da9f:      	vinserti128	$0x1, 0x8(%rbx,%rsi), %ymm0, %ymm0
    daa7:      	vpshufb	%ymm3, %ymm0, %ymm0
    daac:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dab2:      	vinserti128	$0x1, 0x20(%rbx,%rsi), %ymm1, %ymm0
    daba:      	vpshufb	%ymm3, %ymm0, %ymm0
    dabf:      	vpcmpeqd	%ymm0, %ymm2, %k1
    dac5:      	kunpckbw	%k0, %k1, %k0
    dac9:      	kortestw	%k0, %k0
    dacd:      	jne	0xda30 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x70>
    dad3:      	cmpb	$0x0, 0x3f(%rcx)
    dad7:      	je	0xdb1a <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x15a>
    dad9:      	xorq	%rax, %rdx
    dadc:      	movq	%r8, %rdi
    dadf:      	movq	%r15, %rsi
    dae2:      	vzeroupper
    dae5:      	callq	0xe2c0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    daea:      	vmovdqa	0x862e(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    daf2:      	movq	0x8(%rsp), %r11
    daf7:      	movq	0x10(%rsp), %r10
    dafc:      	movq	0x18(%rsp), %r9
    db01:      	movq	0x20(%rsp), %r8
    db06:      	andb	$0x1, %dl
    db09:      	movl	$0x0, %ecx
    db0e:      	cmoveq	%rcx, %rax
    db12:      	movzbl	%dl, %ecx
    db15:      	jmp	0xda42 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x82>
    db1a:      	xorl	%eax, %eax
    db1c:      	xorl	%ecx, %ecx
    db1e:      	jmp	0xda42 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x82>
    db23:      	movq	(%rsp), %rax
    db27:      	movq	%r14, 0x8(%rax)
    db2b:      	movq	%r12, 0x10(%rax)
    db2f:      	jmp	0xdb33 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x173>
    db31:      	xorl	%ebp, %ebp
    db33:      	movq	%rbp, (%rax)
    db36:      	addq	$0x28, %rsp
    db3a:      	popq	%rbx
    db3b:      	popq	%r12
    db3d:      	popq	%r13
    db3f:      	popq	%r14
    db41:      	popq	%r15
    db43:      	popq	%rbp
    db44:      	vzeroupper
    db47:      	retq
    db48:      	nopw	%cs:(%rax,%rax)
    db52:      	nopw	%cs:(%rax,%rax)
    db5c:      	nopw	%cs:(%rax,%rax)
    db66:      	nopw	%cs:(%rax,%rax)
    db70:      	nopw	%cs:(%rax,%rax)
    db7a:      	nopw	(%rax,%rax)

