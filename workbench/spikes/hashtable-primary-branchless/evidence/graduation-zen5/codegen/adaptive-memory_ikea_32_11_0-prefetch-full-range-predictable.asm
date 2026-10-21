000000000000f140 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    f140:      	pushq	%rbp
    f141:      	pushq	%r15
    f143:      	pushq	%r14
    f145:      	pushq	%r13
    f147:      	pushq	%r12
    f149:      	pushq	%rbx
    f14a:      	subq	$0x28, %rsp
    f14e:      	vpxor	%xmm0, %xmm0, %xmm0
    f152:      	movq	%rdi, %rax
    f155:      	vmovdqu	%xmm0, 0x8(%rdi)
    f15a:      	cmpq	%rcx, %rdx
    f15d:      	je	0xf29f <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x15f>
    f163:      	vmovdqa	0x7f55(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f16b:      	movq	0x70(%rsi), %r9
    f16f:      	movq	0x10(%rsi), %rbx
    f173:      	movq	%r8, %r14
    f176:      	subq	%rdx, %r14
    f179:      	movabsq	$-0x61c8864e7a143579, %r15 # imm = 0x9E3779B185EBCA87
    f183:      	movq	%rdx, %r12
    f186:      	movq	%rsi, %r8
    f189:      	movq	%rax, 0x8(%rsp)
    f18e:      	xorl	%r11d, %r11d
    f191:      	xorl	%r13d, %r13d
    f194:      	movq	%rcx, 0x10(%rsp)
    f199:      	movq	%r9, 0x18(%rsp)
    f19e:      	jmp	0xf213 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xd3>
    f1a0:      	movq	(%r12), %rbp
    f1a4:      	movq	%r9, %rdx
    f1a7:      	xorq	%rbp, %rdx
    f1aa:      	mulxq	%r15, %rax, %rdx
    f1af:      	movl	%eax, %edi
    f1b1:      	shrl	$0xf, %edi
    f1b4:      	movl	%eax, %esi
    f1b6:      	andl	$0x1fffff, %esi         # imm = 0x1FFFFF
    f1bc:      	andl	$-0x40, %edi
    f1bf:      	vpbroadcastd	%esi, %ymm2
    f1c5:      	vmovdqu	(%rbx,%rdi), %xmm0
    f1ca:      	vmovdqu	0x18(%rbx,%rdi), %xmm1
    f1d0:      	vinserti128	$0x1, 0x8(%rbx,%rdi), %ymm0, %ymm0
    f1d8:      	vpshufb	%ymm3, %ymm0, %ymm0
    f1dd:      	vpcmpeqd	%ymm0, %ymm2, %k0
    f1e3:      	vinserti128	$0x1, 0x20(%rbx,%rdi), %ymm1, %ymm0
    f1eb:      	vpshufb	%ymm3, %ymm0, %ymm0
    f1f0:      	vpcmpeqd	%ymm0, %ymm2, %k1
    f1f6:      	kortestb	%k1, %k0
    f1fa:      	je	0xf240 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x100>
    f1fc:      	incq	%r13
    f1ff:      	addq	%rbp, %r11
    f202:      	addq	$0x8, %r12
    f206:      	addq	$-0x8, %r14
    f20a:      	cmpq	%rcx, %r12
    f20d:      	je	0xf294 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x154>
    f213:      	cmpq	$0x101, %r14            # imm = 0x101
    f21a:      	jl	0xf1a0 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    f21c:      	movl	0x100(%r12), %eax
    f224:      	xorl	%r9d, %eax
    f227:      	imull	$0x85ebca87, %eax, %eax # imm = 0x85EBCA87
    f22d:      	shrl	$0xf, %eax
    f230:      	andl	$-0x40, %eax
    f233:      	prefetcht0	(%rbx,%rax)
    f237:      	jmp	0xf1a0 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    f23c:      	nopl	(%rax)
    f240:      	cmpb	$0x0, 0x3f(%rbx,%rdi)
    f245:      	je	0xf202 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xc2>
    f247:      	xorq	%rax, %rdx
    f24a:      	movq	%r8, %rdi
    f24d:      	movq	%rbp, %rsi
    f250:      	movq	%r8, %r15
    f253:      	movq	%r11, 0x20(%rsp)
    f258:      	vzeroupper
    f25b:      	callq	0xdd80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    f260:      	vmovdqa	0x7e58(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f268:      	movq	0x20(%rsp), %r11
    f26d:      	movq	0x18(%rsp), %r9
    f272:      	movq	0x10(%rsp), %rcx
    f277:      	movq	%r15, %r8
    f27a:      	movabsq	$-0x61c8864e7a143579, %r15 # imm = 0x9E3779B185EBCA87
    f284:      	testl	$0x100, %eax            # imm = 0x100
    f289:      	jne	0xf1fc <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xbc>
    f28f:      	jmp	0xf202 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xc2>
    f294:      	movq	0x8(%rsp), %rax
    f299:      	movq	%r11, 0x8(%rax)
    f29d:      	jmp	0xf2a2 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x162>
    f29f:      	xorl	%r13d, %r13d
    f2a2:      	movq	%r13, (%rax)
    f2a5:      	addq	$0x28, %rsp
    f2a9:      	popq	%rbx
    f2aa:      	popq	%r12
    f2ac:      	popq	%r13
    f2ae:      	popq	%r14
    f2b0:      	popq	%r15
    f2b2:      	popq	%rbp
    f2b3:      	vzeroupper
    f2b6:      	retq
    f2b7:      	nopw	(%rax,%rax)
