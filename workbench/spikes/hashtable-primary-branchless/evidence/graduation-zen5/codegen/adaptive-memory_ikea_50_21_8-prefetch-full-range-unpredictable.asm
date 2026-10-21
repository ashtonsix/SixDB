000000000000fc80 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    fc80:      	vpxor	%xmm0, %xmm0, %xmm0
    fc84:      	movq	%rdi, %rax
    fc87:      	vmovdqu	%xmm0, 0x8(%rdi)
    fc8c:      	cmpq	%rcx, %rdx
    fc8f:      	je	0xff6a <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x2ea>
    fc95:      	pushq	%rbp
    fc96:      	pushq	%r15
    fc98:      	pushq	%r14
    fc9a:      	pushq	%r13
    fc9c:      	pushq	%r12
    fc9e:      	pushq	%rbx
    fc9f:      	subq	$0x38, %rsp
    fca3:      	vmovdqa	0x8515(%rip), %ymm5     # 0x181c0 <syscall@GLIBC_2.2.5+0x181c0>
    fcab:      	vpmovsxbd	0x8418(%rip), %xmm6 # 0x180cc <syscall@GLIBC_2.2.5+0x180cc>
    fcb4:      	vpbroadcastd	0x8407(%rip), %xmm7 # 0x180c4 <syscall@GLIBC_2.2.5+0x180c4>
    fcbd:      	vpbroadcastd	0x8402(%rip), %xmm8 # 0x180c8 <syscall@GLIBC_2.2.5+0x180c8>
    fcc6:      	subq	%rdx, %r8
    fcc9:      	movabsq	$0x379b185ebca87, %r11  # imm = 0x379B185EBCA87
    fcd3:      	movq	%rdx, %rbx
    fcd6:      	xorl	%r13d, %r13d
    fcd9:      	xorl	%r12d, %r12d
    fcdc:      	xorl	%r10d, %r10d
    fcdf:      	nop
    fce0:      	movq	0x10(%rsi), %rdi
    fce4:      	movq	0x70(%rsi), %rdx
    fce8:      	movq	%r10, 0x8(%rsp)
    fced:      	cmpq	$0x101, %r8             # imm = 0x101
    fcf4:      	jl	0xfd1c <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x9c>
    fcf6:      	movq	0x100(%rbx), %r9
    fcfd:      	movl	$0x151d, %r10d          # imm = 0x151D
    fd03:      	xorq	%rdx, %r9
    fd06:      	imulq	%r11, %r9
    fd0a:      	bextrq	%r10, %r9, %r9
    fd0f:      	leaq	(%r9,%r9,2), %r9
    fd13:      	shll	$0x6, %r9d
    fd17:      	prefetcht0	(%rdi,%r9)
    fd1c:      	movq	(%rbx), %r14
    fd1f:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    fd29:      	xorq	%r14, %rdx
    fd2c:      	mulxq	%r9, %rbp, %r10
    fd31:      	movl	$0x151d, %edx           # imm = 0x151D
    fd36:      	bextrq	%rdx, %rbp, %r9
    fd3b:      	movl	%ebp, %edx
    fd3d:      	andl	$0x1fffffff, %edx       # imm = 0x1FFFFFFF
    fd43:      	leaq	(%r9,%r9,2), %r9
    fd47:      	shll	$0x6, %r9d
    fd4b:      	leaq	(%rdi,%r9), %r15
    fd4f:      	movzbl	0x3f(%rdi,%r9), %edi
    fd55:      	testl	%edi, %edi
    fd57:      	jne	0xfe80 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x200>
    fd5d:      	vmovdqu	(%r15), %xmm0
    fd62:      	movl	%edx, %edi
    fd64:      	shrl	$0x5, %edi
    fd67:      	vpbroadcastd	%edx, %xmm3
    fd6d:      	vmovdqu	0x1d(%r15), %xmm1
    fd73:      	movzbl	0x18(%r15), %r9d
    fd78:      	andb	$0x1, %bpl
    fd7c:      	vinserti128	$0x1, 0x8(%r15), %ymm0, %ymm0
    fd83:      	vpbroadcastd	%edi, %ymm2
    fd89:      	vpsrlvd	%xmm6, %xmm3, %xmm3
    fd8e:      	decb	%bpl
    fd91:      	vpand	%xmm7, %xmm3, %xmm3
    fd95:      	vpaddd	%xmm3, %xmm8, %xmm3
    fd99:      	xorb	%bpl, %r9b
    fd9c:      	xorb	0x35(%r15), %bpl
    fda0:      	vpshufb	%ymm5, %ymm0, %ymm0
    fda5:      	vpcmpeqd	%ymm0, %ymm2, %k0
    fdab:      	vpmovzxbd	0x19(%r15), %xmm0
    fdb1:      	kmovd	%k0, %edi
    fdb5:      	vpxor	%xmm0, %xmm3, %xmm0
    fdb9:      	vpshufd	$0xee, %xmm0, %xmm4     # xmm4 = xmm0[2,3,2,3]
    fdbe:      	vpand	%xmm4, %xmm0, %xmm0
    fdc2:      	vpshufd	$0x55, %xmm0, %xmm4     # xmm4 = xmm0[1,1,1,1]
    fdc7:      	vpand	%xmm4, %xmm0, %xmm0
    fdcb:      	vmovd	%xmm0, %r10d
    fdd0:      	vinserti128	$0x1, 0x25(%r15), %ymm1, %ymm0
    fdd7:      	andb	%r9b, %r10b
    fdda:      	andb	%dil, %r10b
    fddd:      	movzbl	%r10b, %r9d
    fde1:      	vpshufb	%ymm5, %ymm0, %ymm0
    fde6:      	vpcmpeqd	%ymm0, %ymm2, %k0
    fdec:      	vpmovzxbd	0x36(%r15), %xmm0
    fdf2:      	kmovd	%k0, %edi
    fdf6:      	vpxor	%xmm0, %xmm3, %xmm0
    fdfa:      	vpshufd	$0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
    fdff:      	vpand	%xmm1, %xmm0, %xmm0
    fe03:      	vpshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
    fe08:      	vpand	%xmm1, %xmm0, %xmm0
    fe0c:      	vmovd	%xmm0, %r10d
    fe11:      	andb	%bpl, %r10b
    fe14:      	andb	%dil, %r10b
    fe17:      	movzbl	%r10b, %edi
    fe1b:      	shll	$0x8, %edi
    fe1e:      	orq	%r9, %rdi
    fe21:      	testq	%rdx, %rdx
    fe24:      	je	0xfe71 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1f1>
    fe26:      	movq	0x8(%rsp), %r10
    fe2b:      	addq	$0x40, %r15
    fe2f:      	leaq	0x8fa2(%rip), %rdx      # 0x18dd8 <ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup_value ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::get<(ikea::hashtable::lookup_hint)1>(unsigned long) const::zero>
    fe36:      	tzcntq	%rdi, %r9
    fe3b:      	leaq	(%r15,%r9,8), %r9
    fe3f:      	cmovbq	%rdx, %r9
    fe43:      	setae	%dl
    fe46:      	movq	(%r9), %rdi
    fe49:      	movzbl	%dl, %edx
    fe4c:      	addq	%rdi, %r13
    fe4f:      	addq	$0x8, %rbx
    fe53:      	addq	$-0x8, %r8
    fe57:      	addq	%rdx, %r10
    fe5a:      	negq	%rdx
    fe5d:      	andq	%rdx, %r14
    fe60:      	addq	%r14, %r12
    fe63:      	cmpq	%rcx, %rbx
    fe66:      	jne	0xfce0 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    fe6c:      	jmp	0xff4d <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x2cd>
    fe71:      	movzwl	0x3a(%r15), %edx
    fe76:      	xorl	$0xffff, %edx           # imm = 0xFFFF
    fe7c:      	andl	%edx, %edi
    fe7e:      	jmp	0xfe26 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1a6>
    fe80:      	movq	%rsi, 0x28(%rsp)
    fe85:      	movq	%r10, 0x30(%rsp)
    fe8a:      	movq	%rcx, 0x20(%rsp)
    fe8f:      	movq	%r8, 0x18(%rsp)
    fe94:      	movq	%rax, 0x10(%rsp)
    fe99:      	movq	%r15, %rdi
    fe9c:      	movq	%rdx, %rsi
    fe9f:      	vzeroupper
    fea2:      	callq	0xe880 <ikea::hashtable::detail::bucket_ops<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    fea7:      	testq	%rax, %rax
    feaa:      	je	0xfece <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x24e>
    feac:      	tzcntq	%rax, %rax
    feb1:      	movq	0x18(%rsp), %r8
    feb6:      	movq	0x20(%rsp), %rcx
    febb:      	movq	0x28(%rsp), %rsi
    fec0:      	movb	$0x1, %dl
    fec2:      	movq	0x40(%r15,%rax,8), %rdi
    fec7:      	movq	0x10(%rsp), %rax
    fecc:      	jmp	0xff16 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x296>
    fece:      	cmpb	$0x0, 0x3f(%r15)
    fed3:      	movq	0x28(%rsp), %rsi
    fed8:      	je	0xff03 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x283>
    feda:      	movq	0x30(%rsp), %rdx
    fedf:      	movq	%rsi, %rdi
    fee2:      	movq	%r14, %rsi
    fee5:      	xorq	%rbp, %rdx
    fee8:      	callq	0xe9c0 <ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    feed:      	movq	0x28(%rsp), %rsi
    fef2:      	movq	%rax, %rdi
    fef5:      	andb	$0x1, %dl
    fef8:      	movl	$0x0, %eax
    fefd:      	cmoveq	%rax, %rdi
    ff01:      	jmp	0xff07 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x287>
    ff03:      	xorl	%edx, %edx
    ff05:      	xorl	%edi, %edi
    ff07:      	movq	0x10(%rsp), %rax
    ff0c:      	movq	0x18(%rsp), %r8
    ff11:      	movq	0x20(%rsp), %rcx
    ff16:      	vmovdqa	0x82a2(%rip), %ymm5     # 0x181c0 <syscall@GLIBC_2.2.5+0x181c0>
    ff1e:      	vpmovsxbd	0x81a5(%rip), %xmm6 # 0x180cc <syscall@GLIBC_2.2.5+0x180cc>
    ff27:      	vpbroadcastd	0x8194(%rip), %xmm7 # 0x180c4 <syscall@GLIBC_2.2.5+0x180c4>
    ff30:      	vpbroadcastd	0x818f(%rip), %xmm8 # 0x180c8 <syscall@GLIBC_2.2.5+0x180c8>
    ff39:      	movq	0x8(%rsp), %r10
    ff3e:      	movabsq	$0x379b185ebca87, %r11  # imm = 0x379B185EBCA87
    ff48:      	jmp	0xfe49 <Result hint_range<true, true, true, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1c9>
    ff4d:      	movq	%r12, 0x8(%rax)
    ff51:      	movq	%r13, 0x10(%rax)
    ff55:      	addq	$0x38, %rsp
    ff59:      	popq	%rbx
    ff5a:      	popq	%r12
    ff5c:      	popq	%r13
    ff5e:      	popq	%r14
    ff60:      	popq	%r15
    ff62:      	popq	%rbp
    ff63:      	movq	%r10, (%rax)
    ff66:      	vzeroupper
    ff69:      	retq
    ff6a:      	xorl	%r10d, %r10d
    ff6d:      	movq	%r10, (%rax)
    ff70:      	retq
    ff71:      	nopw	%cs:(%rax,%rax)
    ff7b:      	nopl	(%rax,%rax)
