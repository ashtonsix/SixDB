000000000000e9c0 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    e9c0:      	pushq	%rbp
    e9c1:      	pushq	%r15
    e9c3:      	pushq	%r14
    e9c5:      	pushq	%r13
    e9c7:      	pushq	%r12
    e9c9:      	pushq	%rbx
    e9ca:      	subq	$0x18, %rsp
    e9ce:      	vpxor	%xmm0, %xmm0, %xmm0
    e9d2:      	movq	%rdi, %rax
    e9d5:      	vmovdqu	%xmm0, 0x8(%rdi)
    e9da:      	cmpq	%rcx, %rdx
    e9dd:      	je	0xeb2b <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x16b>
    e9e3:      	vmovdqa	0x76d5(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    e9eb:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    e9f5:      	movq	%rdx, %rbx
    e9f8:      	xorl	%r13d, %r13d
    e9fb:      	nopl	(%rax,%rax)
    ea00:      	movq	(%rbx), %r14
    ea03:      	movq	0x70(%rsi), %rdx
    ea07:      	movq	0x10(%rsi), %rdi
    ea0b:      	xorq	%r14, %rdx
    ea0e:      	mulxq	%r9, %rbp, %r15
    ea13:      	movl	%ebp, %r8d
    ea16:      	shrl	$0xf, %r8d
    ea1a:      	movl	%ebp, %edx
    ea1c:      	andl	$0x1fffff, %edx         # imm = 0x1FFFFF
    ea22:      	andl	$-0x40, %r8d
    ea26:      	leaq	(%rdi,%r8), %r12
    ea2a:      	movzbl	0x3f(%rdi,%r8), %edi
    ea30:      	testl	%edi, %edi
    ea32:      	jne	0xea8f <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xcf>
    ea34:      	vmovdqu	(%r12), %xmm0
    ea3a:      	vmovdqu	0x18(%r12), %xmm1
    ea41:      	vpbroadcastd	%edx, %ymm2
    ea47:      	xorl	%edi, %edi
    ea49:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    ea51:      	vpshufb	%ymm3, %ymm0, %ymm0
    ea56:      	vpcmpeqd	%ymm0, %ymm2, %k0
    ea5c:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    ea64:      	vpshufb	%ymm3, %ymm0, %ymm0
    ea69:      	vpcmpeqd	%ymm0, %ymm2, %k1
    ea6f:      	kortestb	%k1, %k0
    ea73:      	setne	%dil
    ea77:      	movzwl	%di, %edx
    ea7a:      	addq	$0x8, %rbx
    ea7e:      	addq	%rdx, %r13
    ea81:      	cmpq	%rcx, %rbx
    ea84:      	jne	0xea00 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x40>
    ea8a:      	jmp	0xeb2e <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x16e>
    ea8f:      	movq	%rsi, 0x8(%rsp)
    ea94:      	movq	%r12, %rdi
    ea97:      	movq	%rdx, %rsi
    ea9a:      	movq	%rax, 0x10(%rsp)
    ea9f:      	movq	%rcx, (%rsp)
    eaa3:      	vzeroupper
    eaa6:      	callq	0xdd00 <ikea::hashtable::detail::bucket_ops<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    eaab:      	vmovdqa	0x760d(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    eab3:      	movq	%rax, %rdx
    eab6:      	movq	0x8(%rsp), %rsi
    eabb:      	movq	(%rsp), %rcx
    eabf:      	movq	0x10(%rsp), %rax
    eac4:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    eace:      	movw	$0x1, %di
    ead2:      	testq	%rdx, %rdx
    ead5:      	jne	0xea77 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xb7>
    ead7:      	cmpb	$0x0, 0x3f(%r12)
    eadd:      	je	0xeb24 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x164>
    eadf:      	xorq	%rbp, %r15
    eae2:      	movq	%rsi, %rdi
    eae5:      	movq	%r14, %rsi
    eae8:      	movq	%r15, %rdx
    eaeb:      	vzeroupper
    eaee:      	callq	0xdd80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    eaf3:      	vmovdqa	0x75c5(%rip), %ymm3     # 0x160c0 <syscall@GLIBC_2.2.5+0x160c0>
    eafb:      	movl	%eax, %edx
    eafd:      	movq	0x8(%rsp), %rsi
    eb02:      	movq	(%rsp), %rcx
    eb06:      	movq	0x10(%rsp), %rax
    eb0b:      	movl	$0x108, %edi            # imm = 0x108
    eb10:      	movabsq	$-0x61c8864e7a143579, %r9 # imm = 0x9E3779B185EBCA87
    eb1a:      	bextrl	%edi, %edx, %edi
    eb1f:      	jmp	0xea77 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xb7>
    eb24:      	xorl	%edi, %edi
    eb26:      	jmp	0xea77 <Result hint_range<false, false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xb7>
    eb2b:      	xorl	%r13d, %r13d
    eb2e:      	movq	%r13, (%rax)
    eb31:      	addq	$0x18, %rsp
    eb35:      	popq	%rbx
    eb36:      	popq	%r12
    eb38:      	popq	%r13
    eb3a:      	popq	%r14
    eb3c:      	popq	%r15
    eb3e:      	popq	%rbp
    eb3f:      	vzeroupper
    eb42:      	retq
    eb43:      	nopw	%cs:(%rax,%rax)
    eb4d:      	nopw	%cs:(%rax,%rax)
    eb57:      	nopw	%cs:(%rax,%rax)
    eb61:      	nopw	%cs:(%rax,%rax)
    eb6b:      	nopw	%cs:(%rax,%rax)
    eb75:      	nopw	%cs:(%rax,%rax)
    eb7f:      	nop
