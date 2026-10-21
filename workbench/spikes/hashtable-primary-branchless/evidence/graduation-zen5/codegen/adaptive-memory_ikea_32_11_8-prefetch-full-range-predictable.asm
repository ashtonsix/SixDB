000000000000f940 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    f940:      	pushq	%rbp
    f941:      	pushq	%r15
    f943:      	pushq	%r14
    f945:      	pushq	%r13
    f947:      	pushq	%r12
    f949:      	pushq	%rbx
    f94a:      	subq	$0x28, %rsp
    f94e:      	vpxor	%xmm0, %xmm0, %xmm0
    f952:      	movq	%rdi, %rax
    f955:      	vmovdqu	%xmm0, 0x8(%rdi)
    f95a:      	cmpq	%rcx, %rdx
    f95d:      	je	0xfb01 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1c1>
    f963:      	vmovdqa	0x87b5(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    f96b:      	movq	0x70(%rsi), %r10
    f96f:      	movq	0x10(%rsi), %rbx
    f973:      	movq	%r8, %r14
    f976:      	subq	%rdx, %r14
    f979:      	movabsq	$-0x61c8864e7a143579, %r13 # imm = 0x9E3779B185EBCA87
    f983:      	movq	%rdx, %r12
    f986:      	movq	%rsi, %r9
    f989:      	movq	%rax, 0x20(%rsp)
    f98e:      	xorl	%r11d, %r11d
    f991:      	xorl	%r15d, %r15d
    f994:      	movq	$0x0, 0x18(%rsp)
    f99d:      	movq	%r10, (%rsp)
    f9a1:      	jmp	0xf9e1 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xa1>
    f9a3:      	vmovdqa	0x8775(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    f9ab:      	movq	(%rsp), %r10
    f9af:      	movq	0x10(%rsp), %r11
    f9b4:      	movq	0x8(%rsp), %r15
    f9b9:      	movabsq	$-0x61c8864e7a143579, %r13 # imm = 0x9E3779B185EBCA87
    f9c3:      	nopw	%cs:(%rax,%rax)
    f9d0:      	addq	$0x8, %r12
    f9d4:      	addq	$-0x8, %r14
    f9d8:      	cmpq	%rcx, %r12
    f9db:      	je	0xfaed <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1ad>
    f9e1:      	cmpq	$0x101, %r14            # imm = 0x101
    f9e8:      	jl	0xfa09 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xc9>
    f9ea:      	movl	0x100(%r12), %eax
    f9f2:      	xorl	%r10d, %eax
    f9f5:      	imull	$0x85ebca87, %eax, %eax # imm = 0x85EBCA87
    f9fb:      	shrl	$0x15, %eax
    f9fe:      	leaq	(%rax,%rax,2), %rax
    fa02:      	shll	$0x6, %eax
    fa05:      	prefetcht0	(%rbx,%rax)
    fa09:      	movq	(%r12), %rbp
    fa0d:      	movq	%r10, %rdx
    fa10:      	xorq	%rbp, %rdx
    fa13:      	mulxq	%r13, %rax, %rdx
    fa18:      	movl	%eax, %esi
    fa1a:      	shrl	$0x15, %esi
    fa1d:      	movl	%eax, %edi
    fa1f:      	andl	$0x1fffff, %edi         # imm = 0x1FFFFF
    fa25:      	leaq	(%rsi,%rsi,2), %rsi
    fa29:      	vpbroadcastd	%edi, %ymm2
    fa2f:      	shll	$0x6, %esi
    fa32:      	vmovdqu	(%rbx,%rsi), %xmm0
    fa37:      	vmovdqu	0x18(%rbx,%rsi), %xmm1
    fa3d:      	leaq	(%rbx,%rsi), %r8
    fa41:      	vinserti128	$0x1, 0x8(%rbx,%rsi), %ymm0, %ymm0
    fa49:      	vpshufb	%ymm3, %ymm0, %ymm0
    fa4e:      	vpcmpeqd	%ymm0, %ymm2, %k0
    fa54:      	vinserti128	$0x1, 0x20(%rbx,%rsi), %ymm1, %ymm0
    fa5c:      	vpshufb	%ymm3, %ymm0, %ymm0
    fa61:      	vpcmpeqd	%ymm0, %ymm2, %k1
    fa67:      	kunpckbw	%k0, %k1, %k0
    fa6b:      	kortestw	%k0, %k0
    fa6f:      	je	0xfa90 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x150>
    fa71:      	kmovd	%k0, %eax
    fa75:      	tzcntl	%eax, %eax
    fa79:      	movq	0x40(%r8,%rax,8), %rax
    fa7e:      	addq	%rbp, 0x18(%rsp)
    fa83:      	incq	%r15
    fa86:      	addq	%rax, %r11
    fa89:      	jmp	0xf9d0 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x90>
    fa8e:      	nop
    fa90:      	cmpb	$0x0, 0x3f(%r8)
    fa95:      	je	0xf9d0 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x90>
    fa9b:      	xorq	%rax, %rdx
    fa9e:      	movq	%r15, 0x8(%rsp)
    faa3:      	movq	%r11, 0x10(%rsp)
    faa8:      	movq	%rcx, %r13
    faab:      	movq	%r9, %r15
    faae:      	movq	%r9, %rdi
    fab1:      	movq	%rbp, %rsi
    fab4:      	vzeroupper
    fab7:      	callq	0xe480 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    fabc:      	movq	%r13, %rcx
    fabf:      	movq	%r15, %r9
    fac2:      	testb	$0x1, %dl
    fac5:      	je	0xf9a3 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x63>
    facb:      	vmovdqa	0x864d(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fad3:      	movq	(%rsp), %r10
    fad7:      	movq	0x10(%rsp), %r11
    fadc:      	movq	0x8(%rsp), %r15
    fae1:      	movabsq	$-0x61c8864e7a143579, %r13 # imm = 0x9E3779B185EBCA87
    faeb:      	jmp	0xfa7e <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x13e>
    faed:      	movq	0x20(%rsp), %rax
    faf2:      	movq	0x18(%rsp), %rcx
    faf7:      	movq	%rcx, 0x8(%rax)
    fafb:      	movq	%r11, 0x10(%rax)
    faff:      	jmp	0xfb04 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1c4>
    fb01:      	xorl	%r15d, %r15d
    fb04:      	movq	%r15, (%rax)
    fb07:      	addq	$0x28, %rsp
    fb0b:      	popq	%rbx
    fb0c:      	popq	%r12
    fb0e:      	popq	%r13
    fb10:      	popq	%r14
    fb12:      	popq	%r15
    fb14:      	popq	%rbp
    fb15:      	vzeroupper
    fb18:      	retq
    fb19:      	nopw	%cs:(%rax,%rax)
    fb23:      	nopw	%cs:(%rax,%rax)
    fb2d:      	nopw	%cs:(%rax,%rax)
    fb37:      	nopw	(%rax,%rax)
