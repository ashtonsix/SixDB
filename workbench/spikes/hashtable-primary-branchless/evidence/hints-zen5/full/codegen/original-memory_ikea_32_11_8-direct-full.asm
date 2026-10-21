000000000000db40 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    db40:      	pushq	%rbp
    db41:      	pushq	%r15
    db43:      	pushq	%r14
    db45:      	pushq	%r13
    db47:      	pushq	%r12
    db49:      	pushq	%rbx
    db4a:      	subq	$0x28, %rsp
    db4e:      	vpxor	%xmm0, %xmm0, %xmm0
    db52:      	vmovdqu	%xmm0, 0x8(%rdi)
    db57:      	movq	%rdi, %rax
    db5a:      	movq	(%rdx), %r9
    db5d:      	movq	0x8(%rdx), %r10
    db61:      	subq	%r9, %r10
    db64:      	je	0xdcc0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x180>
    db6a:      	vmovdqa	0x85ae(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    db72:      	movq	0x70(%rsi), %r11
    db76:      	movq	0x10(%rsi), %r14
    db7a:      	sarq	$0x3, %r10
    db7e:      	movq	%rsi, %r8
    db81:      	movq	%rax, 0x20(%rsp)
    db86:      	xorl	%ebx, %ebx
    db88:      	xorl	%r12d, %r12d
    db8b:      	xorl	%r13d, %r13d
    db8e:      	xorl	%ebp, %ebp
    db90:      	movq	%rsi, 0x18(%rsp)
    db95:      	movq	%r9, 0x10(%rsp)
    db9a:      	movq	%r10, 0x8(%rsp)
    db9f:      	movq	%r11, (%rsp)
    dba3:      	jmp	0xdbd2 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x92>
    dba5:      	vmovdqa	0x8573(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    dbad:      	movq	0x18(%rsp), %r8
    dbb2:      	movq	0x10(%rsp), %r9
    dbb7:      	movq	0x8(%rsp), %r10
    dbbc:      	movq	(%rsp), %r11
    dbc0:      	xorl	%r15d, %r15d
    dbc3:      	addq	%r15, %rbx
    dbc6:      	incq	%r13
    dbc9:      	cmpq	%r13, %r10
    dbcc:      	je	0xdcb1 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x171>
    dbd2:      	movq	(%r9,%r13,8), %r15
    dbd6:      	movq	%r11, %rdx
    dbd9:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    dbe3:      	xorq	%r15, %rdx
    dbe6:      	mulxq	%rax, %rax, %rdx
    dbeb:      	movl	%eax, %ecx
    dbed:      	shrl	$0x15, %ecx
    dbf0:      	movl	%eax, %edi
    dbf2:      	andl	$0x1fffff, %edi         # imm = 0x1FFFFF
    dbf8:      	leaq	(%rcx,%rcx,2), %rsi
    dbfc:      	vpbroadcastd	%edi, %ymm2
    dc02:      	shll	$0x6, %esi
    dc05:      	vmovdqu	(%r14,%rsi), %xmm0
    dc0b:      	vmovdqu	0x18(%r14,%rsi), %xmm1
    dc12:      	leaq	(%r14,%rsi), %rcx
    dc16:      	vinserti128	$0x1, 0x8(%r14,%rsi), %ymm0, %ymm0
    dc1e:      	vpshufb	%ymm3, %ymm0, %ymm0
    dc23:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dc29:      	vinserti128	$0x1, 0x20(%r14,%rsi), %ymm1, %ymm0
    dc31:      	vpshufb	%ymm3, %ymm0, %ymm0
    dc36:      	vpcmpeqd	%ymm0, %ymm2, %k1
    dc3c:      	kunpckbw	%k0, %k1, %k0
    dc40:      	kortestw	%k0, %k0
    dc44:      	je	0xdc70 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x130>
    dc46:      	kmovd	%k0, %eax
    dc4a:      	tzcntl	%eax, %eax
    dc4e:      	movq	0x40(%rcx,%rax,8), %rax
    dc53:      	addq	%rax, %r12
    dc56:      	incq	%rbp
    dc59:      	addq	%r15, %rbx
    dc5c:      	incq	%r13
    dc5f:      	cmpq	%r13, %r10
    dc62:      	jne	0xdbd2 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x92>
    dc68:      	jmp	0xdcb1 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x171>
    dc6a:      	nopw	(%rax,%rax)
    dc70:      	cmpb	$0x0, 0x3f(%rcx)
    dc74:      	je	0xdbc0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x80>
    dc7a:      	xorq	%rax, %rdx
    dc7d:      	movq	%r8, %rdi
    dc80:      	movq	%r15, %rsi
    dc83:      	vzeroupper
    dc86:      	callq	0xe440 <ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    dc8b:      	testb	$0x1, %dl
    dc8e:      	je	0xdba5 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x65>
    dc94:      	vmovdqa	0x8484(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    dc9c:      	movq	0x18(%rsp), %r8
    dca1:      	movq	0x10(%rsp), %r9
    dca6:      	movq	0x8(%rsp), %r10
    dcab:      	movq	(%rsp), %r11
    dcaf:      	jmp	0xdc53 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x113>
    dcb1:      	movq	0x20(%rsp), %rax
    dcb6:      	movq	%r12, 0x10(%rax)
    dcba:      	movq	%rbx, 0x8(%rax)
    dcbe:      	jmp	0xdcc2 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x182>
    dcc0:      	xorl	%ebp, %ebp
    dcc2:      	movq	%rbp, (%rax)
    dcc5:      	addq	$0x28, %rsp
    dcc9:      	popq	%rbx
    dcca:      	popq	%r12
    dccc:      	popq	%r13
    dcce:      	popq	%r14
    dcd0:      	popq	%r15
    dcd2:      	popq	%rbp
    dcd3:      	vzeroupper
    dcd6:      	retq
    dcd7:      	nopw	%cs:(%rax,%rax)
    dce1:      	nopw	%cs:(%rax,%rax)
    dceb:      	nopw	%cs:(%rax,%rax)
    dcf5:      	nopw	%cs:(%rax,%rax)
    dcff:      	nop
