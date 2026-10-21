000000000000ebc0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)>:
    ebc0:      	pushq	%rbp
    ebc1:      	pushq	%r15
    ebc3:      	pushq	%r14
    ebc5:      	pushq	%r13
    ebc7:      	pushq	%r12
    ebc9:      	pushq	%rbx
    ebca:      	subq	$0xe8, %rsp
    ebd1:      	movq	0x8(%rdx), %r15
    ebd5:      	movq	(%rdx), %r14
    ebd8:      	leaq	0xa8(%rsp), %rax
    ebe0:      	movq	%rcx, %r13
    ebe3:      	movb	$0x0, 0xa8(%rsp)
    ebeb:      	vpxor	%xmm0, %xmm0, %xmm0
    ebef:      	vmovdqu	%ymm0, 0xb0(%rsp)
    ebf8:      	vmovdqu	%ymm0, 0xc4(%rsp)
    ec01:      	movq	%rsi, 0x18(%rsp)
    ec06:      	movq	%rdi, %r12
    ec09:      	vpxor	%xmm0, %xmm0, %xmm0
    ec0d:      	movq	%r15, %rbp
    ec10:      	subq	%r14, %rbp
    ec13:      	testq	%rcx, %rcx
    ec16:      	movq	%r14, 0x80(%rsp)
    ec1e:      	movq	%r15, 0x10(%rsp)
    ec23:      	cmoveq	%rax, %r13
    ec27:      	movq	0x20(%r13), %r11
    ec2b:      	movq	0x28(%r13), %r10
    ec2f:      	movq	0x30(%r13), %r9
    ec33:      	movq	0x18(%r13), %rbx
    ec37:      	vmovdqu	%xmm0, (%rdi)
    ec3b:      	movq	$0x0, 0x10(%rdi)
    ec43:      	movq	%r9, %r8
    ec46:      	movq	%r10, %rdx
    ec49:      	movq	%r11, %rsi
    ec4c:      	movq	%rbx, %rdi
    ec4f:      	cmpq	%r14, %r15
    ec52:      	je	0xeeb6 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2f6>
    ec58:      	sarq	$0x3, %rbp
    ec5c:      	testq	%rcx, %rcx
    ec5f:      	leaq	0xb0(%rsp), %rdx
    ec67:      	leaq	0x8(%rcx), %rsi
    ec6b:      	movq	%rbx, 0x40(%rsp)
    ec70:      	movq	%r11, 0x48(%rsp)
    ec75:      	movq	%r10, 0x50(%rsp)
    ec7a:      	movq	%r9, 0x58(%rsp)
    ec7f:      	movq	%r12, 0x60(%rsp)
    ec84:      	movq	%r13, 0x78(%rsp)
    ec89:      	cmovneq	%rcx, %rax
    ec8d:      	cmoveq	%rdx, %rsi
    ec91:      	xorl	%r12d, %r12d
    ec94:      	movq	%rbp, 0x70(%rsp)
    ec99:      	movl	0x38(%rax), %r14d
    ec9d:      	leaq	0x10(%rax), %rcx
    eca1:      	movq	%rsi, 0x68(%rsp)
    eca6:      	movq	%rax, %rbx
    eca9:      	movq	%rcx, 0x8(%rsp)
    ecae:      	xorl	%ecx, %ecx
    ecb0:      	jmp	0xecf0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x130>
    ecb2:      	nopw	%cs:(%rax,%rax)
    ecc0:      	vmovdqa	0x90(%rsp), %xmm1
    ecc9:      	movq	0x88(%rsp), %rcx
    ecd1:      	movq	0x70(%rsp), %rbp
    ecd6:      	incl	%r14d
    ecd9:      	addq	%r15, %r12
    ecdc:      	movl	%r14d, 0x38(%rbx)
    ece0:      	addq	%rsi, %rcx
    ece3:      	vpaddq	%xmm0, %xmm1, %xmm0
    ece7:      	cmpq	%rbp, %r12
    ecea:      	jae	0xee82 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2c2>
    ecf0:      	movq	%rbp, %r15
    ecf3:      	subq	%r12, %r15
    ecf6:      	movl	$0x100, %eax            # imm = 0x100
    ecfb:      	movq	%rcx, 0x88(%rsp)
    ed03:      	movl	%r14d, %ecx
    ed06:      	cmpq	$0x100, %r15            # imm = 0x100
    ed0d:      	cmovaeq	%rax, %r15
    ed11:      	xorl	%ebp, %ebp
    ed13:      	andl	$0x3f, %ecx
    ed16:      	setne	%al
    ed19:      	cmpl	$0x2, %ecx
    ed1c:      	jb	0xed40 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x180>
    ed1e:      	movl	(%r13), %ebp
    ed22:      	testb	$0x3e, %r14b
    ed26:      	jne	0xed49 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x189>
    ed28:      	lfence
    ed2b:      	rdtsc
    ed2d:      	lfence
    ed30:      	movq	%rdx, %r13
    ed33:      	shlq	$0x20, %r13
    ed37:      	orq	%rax, %r13
    ed3a:      	jmp	0xed4c <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x18c>
    ed3c:      	nopl	(%rax)
    ed40:      	movb	%al, %bpl
    ed43:      	testb	$0x3e, %r14b
    ed47:      	je	0xed28 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x168>
    ed49:      	xorl	%r13d, %r13d
    ed4c:      	movq	0x80(%rsp), %rax
    ed54:      	leaq	0x20(%rsp), %rdi
    ed59:      	vmovdqa	%xmm0, 0x90(%rsp)
    ed62:      	leaq	(%rax,%r12,8), %rdx
    ed66:      	leaq	(%rdx,%r15,8), %rcx
    ed6a:      	testb	%bpl, %bpl
    ed6d:      	je	0xedb0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x1f0>
    ed6f:      	movq	0x18(%rsp), %rsi
    ed74:      	movq	0x10(%rsp), %r8
    ed79:      	vzeroupper
    ed7c:      	callq	0xfb40 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    ed81:      	vmovdqa	0x20(%rsp), %xmm0
    ed87:      	movq	0x30(%rsp), %rsi
    ed8c:      	testb	$0x3e, %r14b
    ed90:      	jne	0xedd3 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x213>
    ed92:      	lfence
    ed95:      	rdtsc
    ed97:      	lfence
    ed9a:      	movq	%rax, %rcx
    ed9d:      	movq	%rdx, %rax
    eda0:      	shlq	$0x20, %rax
    eda4:      	orq	%rcx, %rax
    eda7:      	subq	%r13, %rax
    edaa:      	jmp	0xedd5 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x215>
    edac:      	nopl	(%rax)
    edb0:      	movq	0x18(%rsp), %rsi
    edb5:      	movq	0x10(%rsp), %r8
    edba:      	vzeroupper
    edbd:      	callq	0xfd40 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    edc2:      	vmovdqa	0x20(%rsp), %xmm0
    edc8:      	movq	0x30(%rsp), %rsi
    edcd:      	testb	$0x3e, %r14b
    edd1:      	je	0xed92 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x1d2>
    edd3:      	xorl	%eax, %eax
    edd5:      	movq	0x78(%rsp), %r13
    edda:      	movzbl	%bpl, %ecx
    edde:      	addq	%r15, 0x18(%r13,%rcx,8)
    ede3:      	movl	0x38(%rbx), %r14d
    ede7:      	testb	$0x3e, %r14b
    edeb:      	jne	0xecc0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x100>
    edf1:      	incq	0x28(%r13)
    edf5:      	movabsq	$0x94f2094f2094, %rcx   # imm = 0x94F2094F2094
    edff:      	cmpq	%rcx, %rax
    ee02:      	jb	0xee0e <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x24e>
    ee04:      	movabsq	$0x94f2094f2094, %rax   # imm = 0x94F2094F2094
    ee0e:      	shlq	$0xa, %rax
    ee12:      	movabsq	$0x3ffffff00000000, %rcx # imm = 0x3FFFFFF00000000
    ee1c:      	testq	%rcx, %rax
    ee1f:      	je	0xee71 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2b1>
    ee21:      	xorl	%edx, %edx
    ee23:      	divq	%r15
    ee26:      	movq	0x8(%rsp), %rcx
    ee2b:      	testb	%bpl, %bpl
    ee2e:      	jne	0xee35 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x275>
    ee30:      	movq	0x68(%rsp), %rcx
    ee35:      	movq	%rax, (%rcx)
    ee38:      	testb	$0x1, %r14b
    ee3c:      	je	0xecc0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x100>
    ee42:      	movq	0x8(%rsp), %rax
    ee47:      	imulq	$0x64, 0x8(%rbx), %rcx
    ee4c:      	imulq	$0x6e, (%rax), %rax
    ee50:      	cmpq	%rcx, %rax
    ee53:      	setb	%dl
    ee56:      	xorl	%edi, %edi
    ee58:      	cmpb	%dl, (%r13)
    ee5c:      	setne	%dil
    ee60:      	addq	%rdi, 0x30(%r13)
    ee64:      	cmpq	%rcx, %rax
    ee67:      	setb	(%r13)
    ee6c:      	jmp	0xecc0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x100>
    ee71:      	xorl	%edx, %edx
    ee73:      	divl	%r15d
    ee76:      	movq	0x8(%rsp), %rcx
    ee7b:      	testb	%bpl, %bpl
    ee7e:      	je	0xee30 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x270>
    ee80:      	jmp	0xee35 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x275>
    ee82:      	movq	0x60(%rsp), %r12
    ee87:      	movq	0x58(%rsp), %r9
    ee8c:      	movq	0x50(%rsp), %r10
    ee91:      	movq	0x48(%rsp), %r11
    ee96:      	movq	0x40(%rsp), %rbx
    ee9b:      	movq	%rcx, 0x10(%r12)
    eea0:      	vmovdqu	%xmm0, (%r12)
    eea6:      	movq	0x18(%r13), %rdi
    eeaa:      	movq	0x20(%r13), %rsi
    eeae:      	movq	0x28(%r13), %rdx
    eeb2:      	movq	0x30(%r13), %r8
    eeb6:      	subq	%rbx, %rdi
    eeb9:      	subq	%r11, %rsi
    eebc:      	subq	%r10, %rdx
    eebf:      	subq	%r9, %r8
    eec2:      	movq	%r12, %rax
    eec5:      	movq	%rdi, 0xd2c4(%rip)      # 0x1c190 <last_selection>
    eecc:      	movq	%rsi, 0xd2c5(%rip)      # 0x1c198 <last_selection+0x8>
    eed3:      	movq	%rdx, 0xd2c6(%rip)      # 0x1c1a0 <last_selection+0x10>
    eeda:      	movq	%r8, 0xd2c7(%rip)       # 0x1c1a8 <last_selection+0x18>
    eee1:      	addq	$0xe8, %rsp
    eee8:      	popq	%rbx
    eee9:      	popq	%r12
    eeeb:      	popq	%r13
    eeed:      	popq	%r14
    eeef:      	popq	%r15
    eef1:      	popq	%rbp
    eef2:      	vzeroupper
    eef5:      	retq
    eef6:      	nopw	%cs:(%rax,%rax)
