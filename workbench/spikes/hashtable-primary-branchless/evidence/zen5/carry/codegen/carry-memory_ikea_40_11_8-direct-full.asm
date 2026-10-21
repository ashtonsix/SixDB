000000000000dc40 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    dc40:      	pushq	%rbp
    dc41:      	pushq	%r15
    dc43:      	pushq	%r14
    dc45:      	pushq	%r13
    dc47:      	pushq	%r12
    dc49:      	pushq	%rbx
    dc4a:      	subq	$0x28, %rsp
    dc4e:      	vpxor	%xmm0, %xmm0, %xmm0
    dc52:      	vmovdqu	%xmm0, 0x8(%rdi)
    dc57:      	movq	%rdi, %rax
    dc5a:      	movq	(%rdx), %rcx
    dc5d:      	cmpq	%rcx, 0x8(%rdx)
    dc61:      	je	0xdf21 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x2e1>
    dc67:      	vmovdqa	0x9551(%rip), %ymm5     # 0x171c0 <syscall@GLIBC_2.2.5+0x171c0>
    dc6f:      	vpmovsxbd	0x9454(%rip), %xmm6 # 0x170cc <syscall@GLIBC_2.2.5+0x170cc>
    dc78:      	vpbroadcastd	0x9443(%rip), %xmm7 # 0x170c4 <syscall@GLIBC_2.2.5+0x170c4>
    dc81:      	vpbroadcastd	0x943e(%rip), %xmm8 # 0x170c8 <syscall@GLIBC_2.2.5+0x170c8>
    dc8a:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    dc94:      	movl	$0xb1d, %r11d           # imm = 0xB1D
    dc9a:      	movq	%rdx, %r9
    dc9d:      	xorl	%r13d, %r13d
    dca0:      	xorl	%ebx, %ebx
    dca2:      	xorl	%r12d, %r12d
    dca5:      	xorl	%ebp, %ebp
    dca7:      	nopw	(%rax,%rax)
    dcb0:      	movq	(%rcx,%r12,8), %rcx
    dcb4:      	movq	0x70(%rsi), %rdx
    dcb8:      	movq	0x10(%rsi), %rdi
    dcbc:      	xorq	%rcx, %rdx
    dcbf:      	movq	%rcx, (%rsp)
    dcc3:      	mulxq	%r10, %r14, %r8
    dcc8:      	bextrq	%r11, %r14, %rdx
    dccd:      	movl	%r14d, %ecx
    dcd0:      	andl	$0x1fffffff, %ecx       # imm = 0x1FFFFFFF
    dcd6:      	leaq	(%rdx,%rdx,2), %rdx
    dcda:      	shll	$0x6, %edx
    dcdd:      	leaq	(%rdi,%rdx), %r15
    dce1:      	movzbl	0x3f(%rdi,%rdx), %edx
    dce6:      	testl	%edx, %edx
    dce8:      	jne	0xde19 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1d9>
    dcee:      	vmovdqu	(%r15), %xmm0
    dcf3:      	movl	%ecx, %edx
    dcf5:      	shrl	$0x5, %edx
    dcf8:      	vpbroadcastd	%ecx, %xmm3
    dcfe:      	vmovdqu	0x1d(%r15), %xmm1
    dd04:      	movzbl	0x18(%r15), %edi
    dd09:      	andb	$0x1, %r14b
    dd0d:      	vinserti128	$0x1, 0x8(%r15), %ymm0, %ymm0
    dd14:      	vpbroadcastd	%edx, %ymm2
    dd1a:      	vpsrlvd	%xmm6, %xmm3, %xmm3
    dd1f:      	decb	%r14b
    dd22:      	vpand	%xmm7, %xmm3, %xmm3
    dd26:      	vpaddd	%xmm3, %xmm8, %xmm3
    dd2a:      	xorb	%r14b, %dil
    dd2d:      	xorb	0x35(%r15), %r14b
    dd31:      	vpshufb	%ymm5, %ymm0, %ymm0
    dd36:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dd3c:      	vpmovzxbd	0x19(%r15), %xmm0
    dd42:      	kmovd	%k0, %edx
    dd46:      	vpxor	%xmm0, %xmm3, %xmm0
    dd4a:      	vpshufd	$0xee, %xmm0, %xmm4     # xmm4 = xmm0[2,3,2,3]
    dd4f:      	vpand	%xmm4, %xmm0, %xmm0
    dd53:      	vpshufd	$0x55, %xmm0, %xmm4     # xmm4 = xmm0[1,1,1,1]
    dd58:      	vpand	%xmm4, %xmm0, %xmm0
    dd5c:      	vmovd	%xmm0, %r8d
    dd61:      	vinserti128	$0x1, 0x25(%r15), %ymm1, %ymm0
    dd68:      	andb	%dil, %r8b
    dd6b:      	andb	%dl, %r8b
    dd6e:      	movzbl	%r8b, %edi
    dd72:      	vpshufb	%ymm5, %ymm0, %ymm0
    dd77:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dd7d:      	vpmovzxbd	0x36(%r15), %xmm0
    dd83:      	kmovd	%k0, %edx
    dd87:      	vpxor	%xmm0, %xmm3, %xmm0
    dd8b:      	vpshufd	$0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
    dd90:      	vpand	%xmm1, %xmm0, %xmm0
    dd94:      	vpshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
    dd99:      	vpand	%xmm1, %xmm0, %xmm0
    dd9d:      	vmovd	%xmm0, %r8d
    dda2:      	andb	%r14b, %r8b
    dda5:      	andb	%dl, %r8b
    dda8:      	movzbl	%r8b, %edx
    ddac:      	shll	$0x8, %edx
    ddaf:      	orq	%rdi, %rdx
    ddb2:      	testq	%rcx, %rcx
    ddb5:      	je	0xde0a <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1ca>
    ddb7:      	movq	(%rsp), %r14
    ddbb:      	addq	$0x40, %r15
    ddbf:      	leaq	0x9fc2(%rip), %rdi      # 0x17d88 <ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup(unsigned long) const::zero>
    ddc6:      	tzcntq	%rdx, %rcx
    ddcb:      	leaq	(%r15,%rcx,8), %rcx
    ddcf:      	cmovbq	%rdi, %rcx
    ddd3:      	setae	%dl
    ddd6:      	movq	(%rcx), %rcx
    ddd9:      	addq	%rcx, %r13
    dddc:      	movzbl	%dl, %ecx
    dddf:      	movq	0x8(%r9), %rdx
    dde3:      	incq	%r12
    dde6:      	addq	%rcx, %rbp
    dde9:      	negq	%rcx
    ddec:      	andq	%rcx, %r14
    ddef:      	movq	(%r9), %rcx
    ddf2:      	addq	%r14, %rbx
    ddf5:      	subq	%rcx, %rdx
    ddf8:      	sarq	$0x3, %rdx
    ddfc:      	cmpq	%rdx, %r12
    ddff:      	jb	0xdcb0 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x70>
    de05:      	jmp	0xdf17 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x2d7>
    de0a:      	movzwl	0x3a(%r15), %ecx
    de0f:      	xorl	$0xffff, %ecx           # imm = 0xFFFF
    de15:      	andl	%ecx, %edx
    de17:      	jmp	0xddb7 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x177>
    de19:      	movq	%rsi, 0x10(%rsp)
    de1e:      	movq	%r8, 0x20(%rsp)
    de23:      	movq	%r9, 0x8(%rsp)
    de28:      	movq	%rax, 0x18(%rsp)
    de2d:      	movq	%r15, %rdi
    de30:      	movq	%rcx, %rsi
    de33:      	vzeroupper
    de36:      	callq	0xe780 <ikea::hashtable::detail::bucket_ops<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    de3b:      	testq	%rax, %rax
    de3e:      	je	0xde97 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x257>
    de40:      	tzcntq	%rax, %rax
    de45:      	movb	$0x1, %dl
    de47:      	movq	0x40(%r15,%rax,8), %rcx
    de4c:      	vmovdqa	0x936c(%rip), %ymm5     # 0x171c0 <syscall@GLIBC_2.2.5+0x171c0>
    de54:      	vpmovsxbd	0x926f(%rip), %xmm6 # 0x170cc <syscall@GLIBC_2.2.5+0x170cc>
    de5d:      	vpbroadcastd	0x925e(%rip), %xmm7 # 0x170c4 <syscall@GLIBC_2.2.5+0x170c4>
    de66:      	vpbroadcastd	0x9259(%rip), %xmm8 # 0x170c8 <syscall@GLIBC_2.2.5+0x170c8>
    de6f:      	movq	0x18(%rsp), %rax
    de74:      	movq	0x10(%rsp), %rsi
    de79:      	movq	0x8(%rsp), %r9
    de7e:      	movq	(%rsp), %r14
    de82:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    de8c:      	movl	$0xb1d, %r11d           # imm = 0xB1D
    de92:      	jmp	0xddd9 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x199>
    de97:      	cmpb	$0x0, 0x3f(%r15)
    de9c:      	je	0xdf0e <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x2ce>
    de9e:      	movq	0x20(%rsp), %rdx
    dea3:      	movq	0x10(%rsp), %r15
    dea8:      	xorq	%r14, %rdx
    deab:      	movq	(%rsp), %r14
    deaf:      	movq	%r15, %rdi
    deb2:      	movq	%r14, %rsi
    deb5:      	callq	0xe8c0 <ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    deba:      	vmovdqa	0x92fe(%rip), %ymm5     # 0x171c0 <syscall@GLIBC_2.2.5+0x171c0>
    dec2:      	vpmovsxbd	0x9201(%rip), %xmm6 # 0x170cc <syscall@GLIBC_2.2.5+0x170cc>
    decb:      	vpbroadcastd	0x91f0(%rip), %xmm7 # 0x170c4 <syscall@GLIBC_2.2.5+0x170c4>
    ded4:      	vpbroadcastd	0x91eb(%rip), %xmm8 # 0x170c8 <syscall@GLIBC_2.2.5+0x170c8>
    dedd:      	movq	%rax, %rcx
    dee0:      	andb	$0x1, %dl
    dee3:      	movl	$0x0, %eax
    dee8:      	movq	0x8(%rsp), %r9
    deed:      	movq	%r15, %rsi
    def0:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    defa:      	movl	$0xb1d, %r11d           # imm = 0xB1D
    df00:      	cmoveq	%rax, %rcx
    df04:      	movq	0x18(%rsp), %rax
    df09:      	jmp	0xddd9 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x199>
    df0e:      	xorl	%edx, %edx
    df10:      	xorl	%ecx, %ecx
    df12:      	jmp	0xde4c <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x20c>
    df17:      	movq	%rbx, 0x8(%rax)
    df1b:      	movq	%r13, 0x10(%rax)
    df1f:      	jmp	0xdf23 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x2e3>
    df21:      	xorl	%ebp, %ebp
    df23:      	movq	%rbp, (%rax)
    df26:      	addq	$0x28, %rsp
    df2a:      	popq	%rbx
    df2b:      	popq	%r12
    df2d:      	popq	%r13
    df2f:      	popq	%r14
    df31:      	popq	%r15
    df33:      	popq	%rbp
    df34:      	vzeroupper
    df37:      	retq
    df38:      	nopl	(%rax,%rax)

