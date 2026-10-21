000000000000fb40 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    fb40:      	pushq	%rbp
    fb41:      	pushq	%r15
    fb43:      	pushq	%r14
    fb45:      	pushq	%r13
    fb47:      	pushq	%r12
    fb49:      	pushq	%rbx
    fb4a:      	subq	$0x28, %rsp
    fb4e:      	vpxor	%xmm0, %xmm0, %xmm0
    fb52:      	movq	%rdi, %rax
    fb55:      	vmovdqu	%xmm0, 0x8(%rdi)
    fb5a:      	cmpq	%rcx, %rdx
    fb5d:      	je	0xfd1a <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1da>
    fb63:      	vmovdqa	0x85b5(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fb6b:      	movq	%rax, 0x20(%rsp)
    fb70:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    fb7a:      	leaq	0x923f(%rip), %r10      # 0x18dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup_value ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::get<(ikea::hashtable::lookup_hint)1>(unsigned long) const::zero>
    fb81:      	movq	%rdx, %rbx
    fb84:      	xorl	%ebp, %ebp
    fb86:      	xorl	%r15d, %r15d
    fb89:      	xorl	%r13d, %r13d
    fb8c:      	nopl	(%rax)
    fb90:      	movq	(%rbx), %r11
    fb93:      	movq	0x70(%rsi), %rdx
    fb97:      	movq	0x10(%rsi), %r8
    fb9b:      	xorq	%r11, %rdx
    fb9e:      	mulxq	%rax, %r14, %r9
    fba3:      	movl	%r14d, %edi
    fba6:      	shrl	$0x15, %edi
    fba9:      	movl	%r14d, %edx
    fbac:      	andl	$0x1fffff, %edx         # imm = 0x1FFFFF
    fbb2:      	leaq	(%rdi,%rdi,2), %rdi
    fbb6:      	shll	$0x6, %edi
    fbb9:      	leaq	(%r8,%rdi), %r12
    fbbd:      	movzbl	0x3f(%r8,%rdi), %edi
    fbc3:      	testl	%edi, %edi
    fbc5:      	jne	0xfc43 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x103>
    fbc7:      	vmovdqu	(%r12), %xmm0
    fbcd:      	vmovdqu	0x18(%r12), %xmm1
    fbd4:      	vpbroadcastd	%edx, %ymm2
    fbda:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    fbe2:      	vpshufb	%ymm3, %ymm0, %ymm0
    fbe7:      	vpcmpeqd	%ymm0, %ymm2, %k0
    fbed:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    fbf5:      	addq	$0x40, %r12
    fbf9:      	vpshufb	%ymm3, %ymm0, %ymm0
    fbfe:      	vpcmpeqd	%ymm0, %ymm2, %k1
    fc04:      	kunpckbw	%k0, %k1, %k0
    fc08:      	kmovw	%k0, %edx
    fc0c:      	tzcntq	%rdx, %rdi
    fc11:      	leaq	(%r12,%rdi,8), %rdi
    fc15:      	cmovbq	%r10, %rdi
    fc19:      	setae	%dl
    fc1c:      	movq	(%rdi), %rdi
    fc1f:      	movzbl	%dl, %edx
    fc22:      	addq	%rdi, %rbp
    fc25:      	addq	$0x8, %rbx
    fc29:      	addq	%rdx, %r13
    fc2c:      	negq	%rdx
    fc2f:      	andq	%rdx, %r11
    fc32:      	addq	%r11, %r15
    fc35:      	cmpq	%rcx, %rbx
    fc38:      	jne	0xfb90 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x50>
    fc3e:      	jmp	0xfd0b <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1cb>
    fc43:      	movq	%rsi, (%rsp)
    fc47:      	movq	%r9, 0x18(%rsp)
    fc4c:      	movq	%r11, 0x10(%rsp)
    fc51:      	movq	%rcx, 0x8(%rsp)
    fc56:      	movq	%r12, %rdi
    fc59:      	movq	%rdx, %rsi
    fc5c:      	vzeroupper
    fc5f:      	callq	0xe400 <ikea::hashtable::detail::bucket_ops<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    fc64:      	testq	%rax, %rax
    fc67:      	je	0xfc9e <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x15e>
    fc69:      	vmovdqa	0x84af(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fc71:      	tzcntq	%rax, %rax
    fc76:      	movq	0x8(%rsp), %rcx
    fc7b:      	movq	(%rsp), %rsi
    fc7f:      	movq	0x10(%rsp), %r11
    fc84:      	movb	$0x1, %dl
    fc86:      	leaq	0x9133(%rip), %r10      # 0x18dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup_value ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::get<(ikea::hashtable::lookup_hint)1>(unsigned long) const::zero>
    fc8d:      	movq	0x40(%r12,%rax,8), %rdi
    fc92:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    fc9c:      	jmp	0xfc1f <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xdf>
    fc9e:      	cmpb	$0x0, 0x3f(%r12)
    fca4:      	movq	0x10(%rsp), %r11
    fca9:      	je	0xfce0 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1a0>
    fcab:      	movq	0x18(%rsp), %rdx
    fcb0:      	movq	(%rsp), %r12
    fcb4:      	movq	%r11, %rsi
    fcb7:      	xorq	%r14, %rdx
    fcba:      	movq	%r12, %rdi
    fcbd:      	callq	0xe480 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    fcc2:      	movq	0x10(%rsp), %r11
    fcc7:      	movq	0x8(%rsp), %rcx
    fccc:      	movq	%rax, %rdi
    fccf:      	andb	$0x1, %dl
    fcd2:      	movl	$0x0, %eax
    fcd7:      	movq	%r12, %rsi
    fcda:      	cmoveq	%rax, %rdi
    fcde:      	jmp	0xfced <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1ad>
    fce0:      	movq	0x8(%rsp), %rcx
    fce5:      	movq	(%rsp), %rsi
    fce9:      	xorl	%edx, %edx
    fceb:      	xorl	%edi, %edi
    fced:      	vmovdqa	0x842b(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fcf5:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    fcff:      	leaq	0x90ba(%rip), %r10      # 0x18dc0 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::lookup_value ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::get<(ikea::hashtable::lookup_hint)1>(unsigned long) const::zero>
    fd06:      	jmp	0xfc1f <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xdf>
    fd0b:      	movq	0x20(%rsp), %rax
    fd10:      	movq	%r15, 0x8(%rax)
    fd14:      	movq	%rbp, 0x10(%rax)
    fd18:      	jmp	0xfd1d <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1dd>
    fd1a:      	xorl	%r13d, %r13d
    fd1d:      	movq	%r13, (%rax)
    fd20:      	addq	$0x28, %rsp
    fd24:      	popq	%rbx
    fd25:      	popq	%r12
    fd27:      	popq	%r13
    fd29:      	popq	%r14
    fd2b:      	popq	%r15
    fd2d:      	popq	%rbp
    fd2e:      	vzeroupper
    fd31:      	retq
    fd32:      	nopw	%cs:(%rax,%rax)
    fd3c:      	nopl	(%rax)
