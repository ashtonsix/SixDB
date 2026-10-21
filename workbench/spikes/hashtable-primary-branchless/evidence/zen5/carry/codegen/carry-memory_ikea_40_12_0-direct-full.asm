000000000000d9c0 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
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
    d9da:      	movq	(%rdx), %rcx
    d9dd:      	cmpq	%rcx, 0x8(%rdx)
    d9e1:      	je	0xdc2a <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x26a>
    d9e7:      	vmovdqa	0x8731(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    d9ef:      	vpmovsxbd	0x86c4(%rip), %xmm4 # 0x160bc <syscall@GLIBC_2.2.5+0x160bc>
    d9f8:      	vpbroadcastd	0x86b3(%rip), %xmm5 # 0x160b4 <syscall@GLIBC_2.2.5+0x160b4>
    da01:      	vpbroadcastd	0x86ae(%rip), %xmm6 # 0x160b8 <syscall@GLIBC_2.2.5+0x160b8>
    da0a:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    da14:      	movq	%rdx, %r9
    da17:      	xorl	%r13d, %r13d
    da1a:      	xorl	%ebx, %ebx
    da1c:      	xorl	%ebp, %ebp
    da1e:      	nop
    da20:      	movq	(%rcx,%rbx,8), %r14
    da24:      	movq	0x70(%rsi), %rdx
    da28:      	xorq	%r14, %rdx
    da2b:      	mulxq	%r10, %r12, %r8
    da30:      	movq	0x10(%rsi), %rdx
    da34:      	movq	%r12, %rdi
    da37:      	shrq	$0x16, %rdi
    da3b:      	movl	%r12d, %ecx
    da3e:      	andl	$0xfffffff, %ecx        # imm = 0xFFFFFFF
    da44:      	andl	$0x3ffc0, %edi          # imm = 0x3FFC0
    da4a:      	leaq	(%rdx,%rdi), %r15
    da4e:      	movzbl	0x3f(%rdx,%rdi), %edx
    da53:      	testl	%edx, %edx
    da55:      	jne	0xdb45 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x185>
    da5b:      	vmovdqu	(%r15), %xmm0
    da60:      	movl	%ecx, %edx
    da62:      	shrl	$0x5, %edx
    da65:      	vmovdqu	0x1d(%r15), %xmm1
    da6b:      	movzbl	0x18(%r15), %r8d
    da70:      	andb	$0x1, %r12b
    da74:      	vinserti128	$0x1, 0x8(%r15), %ymm0, %ymm0
    da7b:      	vpbroadcastd	%edx, %ymm2
    da81:      	decb	%r12b
    da84:      	xorb	%r12b, %r8b
    da87:      	xorb	0x35(%r15), %r12b
    da8b:      	vpshufb	%ymm3, %ymm0, %ymm0
    da90:      	vpcmpeqd	%ymm0, %ymm2, %k0
    da96:      	vinserti128	$0x1, 0x25(%r15), %ymm1, %ymm0
    da9d:      	vpmovzxbd	0x19(%r15), %xmm1
    daa3:      	kmovd	%k0, %edx
    daa7:      	vpshufb	%ymm3, %ymm0, %ymm0
    daac:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dab2:      	vpbroadcastd	%ecx, %xmm0
    dab8:      	vpsrlvd	%xmm4, %xmm0, %xmm0
    dabd:      	kmovd	%k0, %edi
    dac1:      	vpand	%xmm5, %xmm0, %xmm0
    dac5:      	vpaddd	%xmm6, %xmm0, %xmm0
    dac9:      	vpxor	%xmm1, %xmm0, %xmm1
    dacd:      	vpshufd	$0xee, %xmm1, %xmm2     # xmm2 = xmm1[2,3,2,3]
    dad2:      	vpand	%xmm2, %xmm1, %xmm1
    dad6:      	vpshufd	$0x55, %xmm1, %xmm2     # xmm2 = xmm1[1,1,1,1]
    dadb:      	vpand	%xmm2, %xmm1, %xmm1
    dadf:      	vmovd	%xmm1, %ecx
    dae3:      	vpmovzxbd	0x36(%r15), %xmm1
    dae9:      	andb	%r8b, %cl
    daec:      	andb	%dl, %cl
    daee:      	xorl	%edx, %edx
    daf0:      	vpxor	%xmm1, %xmm0, %xmm0
    daf4:      	vpshufd	$0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
    daf9:      	vpand	%xmm1, %xmm0, %xmm0
    dafd:      	vpshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
    db02:      	vpand	%xmm1, %xmm0, %xmm0
    db06:      	vmovd	%xmm0, %r8d
    db0b:      	andb	%r12b, %r8b
    db0e:      	andb	%dil, %r8b
    db11:      	orb	%cl, %r8b
    db14:      	setne	%dl
    db17:      	movzwl	%dx, %ecx
    db1a:      	movq	0x8(%r9), %rdx
    db1e:      	incq	%rbx
    db21:      	addq	%rcx, %rbp
    db24:      	negq	%rcx
    db27:      	andq	%rcx, %r14
    db2a:      	movq	(%r9), %rcx
    db2d:      	addq	%r14, %r13
    db30:      	subq	%rcx, %rdx
    db33:      	sarq	$0x3, %rdx
    db37:      	cmpq	%rdx, %rbx
    db3a:      	jb	0xda20 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x60>
    db40:      	jmp	0xdc24 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x264>
    db45:      	movq	%rsi, 0x10(%rsp)
    db4a:      	movq	%r8, 0x20(%rsp)
    db4f:      	movq	%r15, %rdi
    db52:      	movq	%rcx, %rsi
    db55:      	movq	%rax, 0x18(%rsp)
    db5a:      	movq	%r9, 0x8(%rsp)
    db5f:      	vzeroupper
    db62:      	callq	0xe440 <ikea::hashtable::detail::bucket_ops<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    db67:      	vpbroadcastd	0x8548(%rip), %xmm6 # 0x160b8 <syscall@GLIBC_2.2.5+0x160b8>
    db70:      	vpbroadcastd	0x853b(%rip), %xmm5 # 0x160b4 <syscall@GLIBC_2.2.5+0x160b4>
    db79:      	vpmovsxbd	0x853a(%rip), %xmm4 # 0x160bc <syscall@GLIBC_2.2.5+0x160bc>
    db82:      	vmovdqa	0x8596(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    db8a:      	movq	%rax, %rcx
    db8d:      	movq	0x8(%rsp), %r9
    db92:      	movq	0x10(%rsp), %rsi
    db97:      	movq	0x18(%rsp), %rax
    db9c:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    dba6:      	movw	$0x1, %dx
    dbaa:      	testq	%rcx, %rcx
    dbad:      	jne	0xdb17 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x157>
    dbb3:      	cmpb	$0x0, 0x3f(%r15)
    dbb8:      	je	0xdc1d <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x25d>
    dbba:      	movq	0x20(%rsp), %rdx
    dbbf:      	movq	%rsi, %rdi
    dbc2:      	movq	%r14, %rsi
    dbc5:      	xorq	%r12, %rdx
    dbc8:      	vzeroupper
    dbcb:      	callq	0xe540 <ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    dbd0:      	vpbroadcastd	0x84df(%rip), %xmm6 # 0x160b8 <syscall@GLIBC_2.2.5+0x160b8>
    dbd9:      	vpbroadcastd	0x84d2(%rip), %xmm5 # 0x160b4 <syscall@GLIBC_2.2.5+0x160b4>
    dbe2:      	vpmovsxbd	0x84d1(%rip), %xmm4 # 0x160bc <syscall@GLIBC_2.2.5+0x160bc>
    dbeb:      	vmovdqa	0x852d(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    dbf3:      	movl	%eax, %ecx
    dbf5:      	movq	0x8(%rsp), %r9
    dbfa:      	movq	0x10(%rsp), %rsi
    dbff:      	movq	0x18(%rsp), %rax
    dc04:      	movl	$0x108, %edx            # imm = 0x108
    dc09:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    dc13:      	bextrl	%edx, %ecx, %edx
    dc18:      	jmp	0xdb17 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x157>
    dc1d:      	xorl	%edx, %edx
    dc1f:      	jmp	0xdb17 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x157>
    dc24:      	movq	%r13, 0x8(%rax)
    dc28:      	jmp	0xdc2c <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x26c>
    dc2a:      	xorl	%ebp, %ebp
    dc2c:      	movq	%rbp, (%rax)
    dc2f:      	addq	$0x28, %rsp
    dc33:      	popq	%rbx
    dc34:      	popq	%r12
    dc36:      	popq	%r13
    dc38:      	popq	%r14
    dc3a:      	popq	%r15
    dc3c:      	popq	%rbp
    dc3d:      	vzeroupper
    dc40:      	retq
    dc41:      	nopw	%cs:(%rax,%rax)
    dc4b:      	nopw	%cs:(%rax,%rax)
    dc55:      	nopw	%cs:(%rax,%rax)
    dc5f:      	nopw	%cs:(%rax,%rax)
    dc69:      	nopw	%cs:(%rax,%rax)
    dc73:      	nopw	%cs:(%rax,%rax)
    dc7d:      	nopl	(%rax)

