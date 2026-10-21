000000000000ef40 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    ef40:      	pushq	%rbp
    ef41:      	pushq	%r15
    ef43:      	pushq	%r14
    ef45:      	pushq	%r13
    ef47:      	pushq	%r12
    ef49:      	pushq	%rbx
    ef4a:      	subq	$0x28, %rsp
    ef4e:      	vpxor	%xmm0, %xmm0, %xmm0
    ef52:      	movq	%rdi, %rax
    ef55:      	vmovdqu	%xmm0, 0x8(%rdi)
    ef5a:      	cmpq	%rcx, %rdx
    ef5d:      	je	0xf0fc <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1bc>
    ef63:      	vmovdqa	0x8155(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    ef6b:      	subq	%rdx, %r8
    ef6e:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    ef78:      	movq	%rdx, %rbx
    ef7b:      	xorl	%r13d, %r13d
    ef7e:      	xorl	%ebp, %ebp
    ef80:      	movq	0x10(%rsi), %rdi
    ef84:      	movq	0x70(%rsi), %rdx
    ef88:      	cmpq	$0x101, %r8             # imm = 0x101
    ef8f:      	jl	0xefaf <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x6f>
    ef91:      	movl	0x100(%rbx), %r9d
    ef98:      	xorl	%edx, %r9d
    ef9b:      	imull	$0x85ebca87, %r9d, %r9d # imm = 0x85EBCA87
    efa2:      	shrl	$0xf, %r9d
    efa6:      	andl	$-0x40, %r9d
    efaa:      	prefetcht0	(%rdi,%r9)
    efaf:      	movq	(%rbx), %r14
    efb2:      	xorq	%r14, %rdx
    efb5:      	mulxq	%r10, %r15, %r11
    efba:      	movl	%r15d, %r9d
    efbd:      	shrl	$0xf, %r9d
    efc1:      	movl	%r15d, %edx
    efc4:      	andl	$0x1fffff, %edx         # imm = 0x1FFFFF
    efca:      	andl	$-0x40, %r9d
    efce:      	leaq	(%rdi,%r9), %r12
    efd2:      	movzbl	0x3f(%rdi,%r9), %edi
    efd8:      	testl	%edi, %edi
    efda:      	jne	0xf044 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x104>
    efdc:      	vmovdqu	(%r12), %xmm0
    efe2:      	vmovdqu	0x18(%r12), %xmm1
    efe9:      	vpbroadcastd	%edx, %ymm2
    efef:      	xorl	%edi, %edi
    eff1:      	vinserti128	$0x1, 0x8(%r12), %ymm0, %ymm0
    eff9:      	vpshufb	%ymm3, %ymm0, %ymm0
    effe:      	vpcmpeqd	%ymm0, %ymm2, %k0
    f004:      	vinserti128	$0x1, 0x20(%r12), %ymm1, %ymm0
    f00c:      	vpshufb	%ymm3, %ymm0, %ymm0
    f011:      	vpcmpeqd	%ymm0, %ymm2, %k1
    f017:      	kortestb	%k1, %k0
    f01b:      	setne	%dil
    f01f:      	movzwl	%di, %edx
    f022:      	addq	$0x8, %rbx
    f026:      	addq	$-0x8, %r8
    f02a:      	addq	%rdx, %rbp
    f02d:      	negq	%rdx
    f030:      	andq	%rdx, %r14
    f033:      	addq	%r14, %r13
    f036:      	cmpq	%rcx, %rbx
    f039:      	jne	0xef80 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x40>
    f03f:      	jmp	0xf0f6 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1b6>
    f044:      	movq	%rsi, 0x10(%rsp)
    f049:      	movq	%r11, 0x20(%rsp)
    f04e:      	movq	%r12, %rdi
    f051:      	movq	%rdx, %rsi
    f054:      	movq	%rax, 0x18(%rsp)
    f059:      	movq	%r8, (%rsp)
    f05d:      	movq	%rcx, 0x8(%rsp)
    f062:      	vzeroupper
    f065:      	callq	0xdd00 <ikea::hashtable::detail::bucket_ops<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0>::primary_matches(unsigned char const*, unsigned long)>
    f06a:      	vmovdqa	0x804e(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f072:      	movq	%rax, %rdx
    f075:      	movq	0x10(%rsp), %rsi
    f07a:      	movq	0x8(%rsp), %rcx
    f07f:      	movq	(%rsp), %r8
    f083:      	movq	0x18(%rsp), %rax
    f088:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    f092:      	movw	$0x1, %di
    f096:      	testq	%rdx, %rdx
    f099:      	jne	0xf01f <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xdf>
    f09b:      	cmpb	$0x0, 0x3f(%r12)
    f0a1:      	je	0xf0ef <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1af>
    f0a3:      	movq	0x20(%rsp), %rdx
    f0a8:      	movq	%rsi, %rdi
    f0ab:      	movq	%r14, %rsi
    f0ae:      	xorq	%r15, %rdx
    f0b1:      	vzeroupper
    f0b4:      	callq	0xdd80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    f0b9:      	vmovdqa	0x7fff(%rip), %ymm3     # 0x170c0 <syscall@GLIBC_2.2.5+0x170c0>
    f0c1:      	movl	%eax, %edx
    f0c3:      	movq	0x10(%rsp), %rsi
    f0c8:      	movq	0x8(%rsp), %rcx
    f0cd:      	movq	(%rsp), %r8
    f0d1:      	movq	0x18(%rsp), %rax
    f0d6:      	movl	$0x108, %edi            # imm = 0x108
    f0db:      	movabsq	$-0x61c8864e7a143579, %r10 # imm = 0x9E3779B185EBCA87
    f0e5:      	bextrl	%edi, %edx, %edi
    f0ea:      	jmp	0xf01f <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xdf>
    f0ef:      	xorl	%edi, %edi
    f0f1:      	jmp	0xf01f <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xdf>
    f0f6:      	movq	%r13, 0x8(%rax)
    f0fa:      	jmp	0xf0fe <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1be>
    f0fc:      	xorl	%ebp, %ebp
    f0fe:      	movq	%rbp, (%rax)
    f101:      	addq	$0x28, %rsp
    f105:      	popq	%rbx
    f106:      	popq	%r12
    f108:      	popq	%r13
    f10a:      	popq	%r14
    f10c:      	popq	%r15
    f10e:      	popq	%rbp
    f10f:      	vzeroupper
    f112:      	retq
    f113:      	nopw	%cs:(%rax,%rax)
    f11d:      	nopw	%cs:(%rax,%rax)
    f127:      	nopw	%cs:(%rax,%rax)
    f131:      	nopw	%cs:(%rax,%rax)
    f13b:      	nopl	(%rax,%rax)
