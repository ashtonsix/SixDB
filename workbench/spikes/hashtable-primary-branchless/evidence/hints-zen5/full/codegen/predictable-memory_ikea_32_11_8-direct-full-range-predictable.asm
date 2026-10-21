000000000000ed80 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    ed80:      	pushq	%rbp
    ed81:      	pushq	%r15
    ed83:      	pushq	%r14
    ed85:      	pushq	%r13
    ed87:      	pushq	%r12
    ed89:      	pushq	%rbx
    ed8a:      	subq	$0x28, %rsp
    ed8e:      	vpxor	%xmm0, %xmm0, %xmm0
    ed92:      	movq	%rdi, %rax
    ed95:      	vmovdqu	%xmm0, 0x8(%rdi)
    ed9a:      	cmpq	%rcx, %rdx
    ed9d:      	je	0xef07 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x187>
    eda3:      	vmovdqa	0x7375(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    edab:      	movq	0x70(%rsi), %r10
    edaf:      	movq	0x10(%rsi), %r12
    edb3:      	movabsq	$-0x61c8864e7a143579, %r11 # imm = 0x9E3779B185EBCA87
    edbd:      	movq	%rdx, %r15
    edc0:      	movq	%rsi, %r9
    edc3:      	movq	%rax, 0x20(%rsp)
    edc8:      	xorl	%ebx, %ebx
    edca:      	xorl	%r14d, %r14d
    edcd:      	xorl	%ebp, %ebp
    edcf:      	movq	%rcx, 0x18(%rsp)
    edd4:      	movq	%rsi, 0x10(%rsp)
    edd9:      	movq	%r10, 0x8(%rsp)
    edde:      	jmp	0xee1d <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x9d>
    ede0:      	vmovdqa	0x7338(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    ede8:      	movq	0x18(%rsp), %rcx
    eded:      	movq	0x10(%rsp), %r9
    edf2:      	movq	0x8(%rsp), %r10
    edf7:      	movabsq	$-0x61c8864e7a143579, %r11 # imm = 0x9E3779B185EBCA87
    ee01:      	nopw	%cs:(%rax,%rax)
    ee10:      	addq	$0x8, %r15
    ee14:      	cmpq	%rcx, %r15
    ee17:      	je	0xeef8 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x178>
    ee1d:      	movq	(%r15), %r13
    ee20:      	movq	%r10, %rdx
    ee23:      	xorq	%r13, %rdx
    ee26:      	mulxq	%r11, %rax, %rdx
    ee2b:      	movl	%eax, %esi
    ee2d:      	shrl	$0x15, %esi
    ee30:      	movl	%eax, %edi
    ee32:      	andl	$0x1fffff, %edi         # imm = 0x1FFFFF
    ee38:      	leaq	(%rsi,%rsi,2), %rsi
    ee3c:      	vpbroadcastd	%edi, %ymm2
    ee42:      	shll	$0x6, %esi
    ee45:      	vmovdqu	(%r12,%rsi), %xmm0
    ee4b:      	vmovdqu	0x18(%r12,%rsi), %xmm1
    ee52:      	leaq	(%r12,%rsi), %r8
    ee56:      	vinserti128	$0x1, 0x8(%r12,%rsi), %ymm0, %ymm0
    ee5e:      	vpshufb	%ymm3, %ymm0, %ymm0
    ee63:      	vpcmpeqd	%ymm0, %ymm2, %k0
    ee69:      	vinserti128	$0x1, 0x20(%r12,%rsi), %ymm1, %ymm0
    ee71:      	vpshufb	%ymm3, %ymm0, %ymm0
    ee76:      	vpcmpeqd	%ymm0, %ymm2, %k1
    ee7c:      	kunpckbw	%k0, %k1, %k0
    ee80:      	kortestw	%k0, %k0
    ee84:      	je	0xeeb0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x130>
    ee86:      	kmovd	%k0, %eax
    ee8a:      	tzcntl	%eax, %eax
    ee8e:      	movq	0x40(%r8,%rax,8), %rax
    ee93:      	incq	%rbp
    ee96:      	addq	%r13, %r14
    ee99:      	addq	%rax, %rbx
    ee9c:      	jmp	0xee10 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x90>
    eea1:      	nopw	%cs:(%rax,%rax)
    eeb0:      	cmpb	$0x0, 0x3f(%r8)
    eeb5:      	je	0xee10 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x90>
    eebb:      	xorq	%rax, %rdx
    eebe:      	movq	%r9, %rdi
    eec1:      	movq	%r13, %rsi
    eec4:      	vzeroupper
    eec7:      	callq	0xe400 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    eecc:      	testb	$0x1, %dl
    eecf:      	je	0xede0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    eed5:      	vmovdqa	0x7243(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    eedd:      	movq	0x18(%rsp), %rcx
    eee2:      	movq	0x10(%rsp), %r9
    eee7:      	movq	0x8(%rsp), %r10
    eeec:      	movabsq	$-0x61c8864e7a143579, %r11 # imm = 0x9E3779B185EBCA87
    eef6:      	jmp	0xee93 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x113>
    eef8:      	movq	0x20(%rsp), %rax
    eefd:      	movq	%r14, 0x8(%rax)
    ef01:      	movq	%rbx, 0x10(%rax)
    ef05:      	jmp	0xef09 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x189>
    ef07:      	xorl	%ebp, %ebp
    ef09:      	movq	%rbp, (%rax)
    ef0c:      	addq	$0x28, %rsp
    ef10:      	popq	%rbx
    ef11:      	popq	%r12
    ef13:      	popq	%r13
    ef15:      	popq	%r14
    ef17:      	popq	%r15
    ef19:      	popq	%rbp
    ef1a:      	vzeroupper
    ef1d:      	retq
    ef1e:      	nopw	%cs:(%rax,%rax)
    ef28:      	nopw	%cs:(%rax,%rax)
    ef32:      	nopw	%cs:(%rax,%rax)
    ef3c:      	nopl	(%rax)
