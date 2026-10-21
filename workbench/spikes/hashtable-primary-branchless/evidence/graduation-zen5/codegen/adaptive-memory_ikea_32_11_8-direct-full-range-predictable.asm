000000000000fd40 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    fd40:      	pushq	%rbp
    fd41:      	pushq	%r15
    fd43:      	pushq	%r14
    fd45:      	pushq	%r13
    fd47:      	pushq	%r12
    fd49:      	pushq	%rbx
    fd4a:      	subq	$0x28, %rsp
    fd4e:      	vpxor	%xmm0, %xmm0, %xmm0
    fd52:      	movq	%rdi, %rax
    fd55:      	vmovdqu	%xmm0, 0x8(%rdi)
    fd5a:      	cmpq	%rcx, %rdx
    fd5d:      	je	0xfec7 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x187>
    fd63:      	vmovdqa	0x83b5(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fd6b:      	movq	0x70(%rsi), %r10
    fd6f:      	movq	0x10(%rsi), %r12
    fd73:      	movabsq	$-0x61c8864e7a143579, %r11 # imm = 0x9E3779B185EBCA87
    fd7d:      	movq	%rdx, %r15
    fd80:      	movq	%rsi, %r9
    fd83:      	movq	%rax, 0x20(%rsp)
    fd88:      	xorl	%ebx, %ebx
    fd8a:      	xorl	%r14d, %r14d
    fd8d:      	xorl	%ebp, %ebp
    fd8f:      	movq	%rcx, 0x18(%rsp)
    fd94:      	movq	%rsi, 0x10(%rsp)
    fd99:      	movq	%r10, 0x8(%rsp)
    fd9e:      	jmp	0xfddd <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x9d>
    fda0:      	vmovdqa	0x8378(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fda8:      	movq	0x18(%rsp), %rcx
    fdad:      	movq	0x10(%rsp), %r9
    fdb2:      	movq	0x8(%rsp), %r10
    fdb7:      	movabsq	$-0x61c8864e7a143579, %r11 # imm = 0x9E3779B185EBCA87
    fdc1:      	nopw	%cs:(%rax,%rax)
    fdd0:      	addq	$0x8, %r15
    fdd4:      	cmpq	%rcx, %r15
    fdd7:      	je	0xfeb8 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x178>
    fddd:      	movq	(%r15), %r13
    fde0:      	movq	%r10, %rdx
    fde3:      	xorq	%r13, %rdx
    fde6:      	mulxq	%r11, %rax, %rdx
    fdeb:      	movl	%eax, %esi
    fded:      	shrl	$0x15, %esi
    fdf0:      	movl	%eax, %edi
    fdf2:      	andl	$0x1fffff, %edi         # imm = 0x1FFFFF
    fdf8:      	leaq	(%rsi,%rsi,2), %rsi
    fdfc:      	vpbroadcastd	%edi, %ymm2
    fe02:      	shll	$0x6, %esi
    fe05:      	vmovdqu	(%r12,%rsi), %xmm0
    fe0b:      	vmovdqu	0x18(%r12,%rsi), %xmm1
    fe12:      	leaq	(%r12,%rsi), %r8
    fe16:      	vinserti128	$0x1, 0x8(%r12,%rsi), %ymm0, %ymm0
    fe1e:      	vpshufb	%ymm3, %ymm0, %ymm0
    fe23:      	vpcmpeqd	%ymm0, %ymm2, %k0
    fe29:      	vinserti128	$0x1, 0x20(%r12,%rsi), %ymm1, %ymm0
    fe31:      	vpshufb	%ymm3, %ymm0, %ymm0
    fe36:      	vpcmpeqd	%ymm0, %ymm2, %k1
    fe3c:      	kunpckbw	%k0, %k1, %k0
    fe40:      	kortestw	%k0, %k0
    fe44:      	je	0xfe70 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x130>
    fe46:      	kmovd	%k0, %eax
    fe4a:      	tzcntl	%eax, %eax
    fe4e:      	movq	0x40(%r8,%rax,8), %rax
    fe53:      	incq	%rbp
    fe56:      	addq	%r13, %r14
    fe59:      	addq	%rax, %rbx
    fe5c:      	jmp	0xfdd0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x90>
    fe61:      	nopw	%cs:(%rax,%rax)
    fe70:      	cmpb	$0x0, 0x3f(%r8)
    fe75:      	je	0xfdd0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x90>
    fe7b:      	xorq	%rax, %rdx
    fe7e:      	movq	%r9, %rdi
    fe81:      	movq	%r13, %rsi
    fe84:      	vzeroupper
    fe87:      	callq	0xe480 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    fe8c:      	testb	$0x1, %dl
    fe8f:      	je	0xfda0 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x60>
    fe95:      	vmovdqa	0x8283(%rip), %ymm3     # 0x18120 <syscall@GLIBC_2.2.5+0x18120>
    fe9d:      	movq	0x18(%rsp), %rcx
    fea2:      	movq	0x10(%rsp), %r9
    fea7:      	movq	0x8(%rsp), %r10
    feac:      	movabsq	$-0x61c8864e7a143579, %r11 # imm = 0x9E3779B185EBCA87
    feb6:      	jmp	0xfe53 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x113>
    feb8:      	movq	0x20(%rsp), %rax
    febd:      	movq	%r14, 0x8(%rax)
    fec1:      	movq	%rbx, 0x10(%rax)
    fec5:      	jmp	0xfec9 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x189>
    fec7:      	xorl	%ebp, %ebp
    fec9:      	movq	%rbp, (%rax)
    fecc:      	addq	$0x28, %rsp
    fed0:      	popq	%rbx
    fed1:      	popq	%r12
    fed3:      	popq	%r13
    fed5:      	popq	%r14
    fed7:      	popq	%r15
    fed9:      	popq	%rbp
    feda:      	vzeroupper
    fedd:      	retq
    fede:      	nopw	%cs:(%rax,%rax)
    fee8:      	nopw	%cs:(%rax,%rax)
    fef2:      	nopw	%cs:(%rax,%rax)
    fefc:      	nopl	(%rax)
