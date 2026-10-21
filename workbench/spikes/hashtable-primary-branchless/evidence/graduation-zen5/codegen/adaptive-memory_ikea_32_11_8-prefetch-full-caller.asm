000000000000e700 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)>:
    e700:      	pushq	%rbp
    e701:      	pushq	%r15
    e703:      	pushq	%r14
    e705:      	pushq	%r13
    e707:      	pushq	%r12
    e709:      	pushq	%rbx
    e70a:      	subq	$0xe8, %rsp
    e711:      	movq	(%rdx), %rbx
    e714:      	movq	0x8(%rdx), %rdx
    e718:      	movq	%rsi, 0x8(%rsp)
    e71d:      	movq	%rdi, %rax
    e720:      	movq	%rdx, %r14
    e723:      	subq	%rbx, %r14
    e726:      	movq	%rdx, 0x10(%rsp)
    e72b:      	sarq	$0x3, %r14
    e72f:      	cmpq	%rbx, %rdx
    e732:      	je	0xe8a4 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x1a4>
    e738:      	movq	0x8(%rsp), %rsi
    e73d:      	cmpq	$0x20, %r14
    e741:      	movl	$0x20, %r8d
    e747:      	movl	$0x85ebca87, %r11d      # imm = 0x85EBCA87
    e74d:      	cmovbq	%r14, %r8
    e751:      	movl	%r8d, %edi
    e754:      	andl	$0x7, %edi
    e757:      	movq	0x10(%rsi), %rdx
    e75b:      	movq	0x70(%rsi), %rsi
    e75f:      	cmpq	$0x8, %r14
    e763:      	jae	0xe76d <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x6d>
    e765:      	xorl	%r9d, %r9d
    e768:      	jmp	0xe874 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x174>
    e76d:      	andl	$0x38, %r8d
    e771:      	xorl	%r9d, %r9d
    e774:      	nopw	%cs:(%rax,%rax)
    e780:      	movl	(%rbx,%r9,8), %r10d
    e784:      	xorl	%esi, %r10d
    e787:      	imull	%r11d, %r10d
    e78b:      	shrl	$0x15, %r10d
    e78f:      	leaq	(%r10,%r10,2), %r10
    e793:      	shll	$0x6, %r10d
    e797:      	prefetcht0	(%rdx,%r10)
    e79c:      	movl	0x8(%rbx,%r9,8), %r10d
    e7a1:      	xorl	%esi, %r10d
    e7a4:      	imull	%r11d, %r10d
    e7a8:      	shrl	$0x15, %r10d
    e7ac:      	leaq	(%r10,%r10,2), %r10
    e7b0:      	shll	$0x6, %r10d
    e7b4:      	prefetcht0	(%rdx,%r10)
    e7b9:      	movl	0x10(%rbx,%r9,8), %r10d
    e7be:      	xorl	%esi, %r10d
    e7c1:      	imull	%r11d, %r10d
    e7c5:      	shrl	$0x15, %r10d
    e7c9:      	leaq	(%r10,%r10,2), %r10
    e7cd:      	shll	$0x6, %r10d
    e7d1:      	prefetcht0	(%rdx,%r10)
    e7d6:      	movl	0x18(%rbx,%r9,8), %r10d
    e7db:      	xorl	%esi, %r10d
    e7de:      	imull	%r11d, %r10d
    e7e2:      	shrl	$0x15, %r10d
    e7e6:      	leaq	(%r10,%r10,2), %r10
    e7ea:      	shll	$0x6, %r10d
    e7ee:      	prefetcht0	(%rdx,%r10)
    e7f3:      	movl	0x20(%rbx,%r9,8), %r10d
    e7f8:      	xorl	%esi, %r10d
    e7fb:      	imull	%r11d, %r10d
    e7ff:      	shrl	$0x15, %r10d
    e803:      	leaq	(%r10,%r10,2), %r10
    e807:      	shll	$0x6, %r10d
    e80b:      	prefetcht0	(%rdx,%r10)
    e810:      	movl	0x28(%rbx,%r9,8), %r10d
    e815:      	xorl	%esi, %r10d
    e818:      	imull	%r11d, %r10d
    e81c:      	shrl	$0x15, %r10d
    e820:      	leaq	(%r10,%r10,2), %r10
    e824:      	shll	$0x6, %r10d
    e828:      	prefetcht0	(%rdx,%r10)
    e82d:      	movl	0x30(%rbx,%r9,8), %r10d
    e832:      	xorl	%esi, %r10d
    e835:      	imull	%r11d, %r10d
    e839:      	shrl	$0x15, %r10d
    e83d:      	leaq	(%r10,%r10,2), %r10
    e841:      	shll	$0x6, %r10d
    e845:      	prefetcht0	(%rdx,%r10)
    e84a:      	movl	0x38(%rbx,%r9,8), %r10d
    e84f:      	addq	$0x8, %r9
    e853:      	xorl	%esi, %r10d
    e856:      	imull	%r11d, %r10d
    e85a:      	shrl	$0x15, %r10d
    e85e:      	leaq	(%r10,%r10,2), %r10
    e862:      	shll	$0x6, %r10d
    e866:      	prefetcht0	(%rdx,%r10)
    e86b:      	cmpq	%r9, %r8
    e86e:      	jne	0xe780 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x80>
    e874:      	testq	%rdi, %rdi
    e877:      	je	0xe8a4 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x1a4>
    e879:      	leaq	(%rbx,%r9,8), %r8
    e87d:      	xorl	%r9d, %r9d
    e880:      	movl	(%r8,%r9,8), %r10d
    e884:      	incq	%r9
    e887:      	xorl	%esi, %r10d
    e88a:      	imull	%r11d, %r10d
    e88e:      	shrl	$0x15, %r10d
    e892:      	leaq	(%r10,%r10,2), %r10
    e896:      	shll	$0x6, %r10d
    e89a:      	prefetcht0	(%rdx,%r10)
    e89f:      	cmpq	%r9, %rdi
    e8a2:      	jne	0xe880 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x180>
    e8a4:      	testq	%rcx, %rcx
    e8a7:      	leaq	0xa8(%rsp), %rbp
    e8af:      	movq	%rcx, %r13
    e8b2:      	movb	$0x0, 0xa8(%rsp)
    e8ba:      	vpxor	%xmm0, %xmm0, %xmm0
    e8be:      	vmovdqu	%ymm0, 0xb0(%rsp)
    e8c7:      	vmovdqu	%ymm0, 0xc4(%rsp)
    e8d0:      	vpxor	%xmm0, %xmm0, %xmm0
    e8d4:      	cmoveq	%rbp, %r13
    e8d8:      	movq	0x18(%r13), %r15
    e8dc:      	movq	0x20(%r13), %r11
    e8e0:      	movq	0x28(%r13), %r10
    e8e4:      	movq	0x30(%r13), %r9
    e8e8:      	vmovdqu	%xmm0, (%rax)
    e8ec:      	movq	$0x0, 0x10(%rax)
    e8f4:      	movq	%r9, %r8
    e8f7:      	movq	%r10, %rdx
    e8fa:      	movq	%r11, %rsi
    e8fd:      	movq	%r15, %rdi
    e900:      	cmpq	%rbx, 0x10(%rsp)
    e905:      	je	0xeb63 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x463>
    e90b:      	testq	%rcx, %rcx
    e90e:      	movq	%rax, 0x60(%rsp)
    e913:      	leaq	0xb0(%rsp), %rax
    e91b:      	leaq	0x8(%rcx), %rdx
    e91f:      	movq	%r15, 0x40(%rsp)
    e924:      	movq	%r11, 0x48(%rsp)
    e929:      	movq	%r10, 0x50(%rsp)
    e92e:      	movq	%r9, 0x58(%rsp)
    e933:      	movq	%rbx, 0x80(%rsp)
    e93b:      	movq	%r14, 0x78(%rsp)
    e940:      	movq	%r13, 0x70(%rsp)
    e945:      	cmovneq	%rcx, %rbp
    e949:      	cmoveq	%rax, %rdx
    e94d:      	xorl	%ecx, %ecx
    e94f:      	xorl	%r15d, %r15d
    e952:      	movl	0x38(%rbp), %r12d
    e956:      	leaq	0x10(%rbp), %rax
    e95a:      	movq	%rbp, %rbx
    e95d:      	movq	%rdx, 0x68(%rsp)
    e962:      	movq	%rax, 0x18(%rsp)
    e967:      	jmp	0xe9a0 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2a0>
    e969:      	nopl	(%rax)
    e970:      	vmovdqa	0x90(%rsp), %xmm1
    e979:      	movq	0x88(%rsp), %rcx
    e981:      	addq	%r14, %r15
    e984:      	movq	0x78(%rsp), %r14
    e989:      	incl	%r12d
    e98c:      	movl	%r12d, 0x38(%rbx)
    e990:      	addq	%rsi, %rcx
    e993:      	vpaddq	%xmm0, %xmm1, %xmm0
    e997:      	cmpq	%r14, %r15
    e99a:      	jae	0xeb32 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x432>
    e9a0:      	subq	%r15, %r14
    e9a3:      	movl	$0x100, %eax            # imm = 0x100
    e9a8:      	movq	%rcx, 0x88(%rsp)
    e9b0:      	movl	%r12d, %ecx
    e9b3:      	cmpq	$0x100, %r14            # imm = 0x100
    e9ba:      	cmovaeq	%rax, %r14
    e9be:      	xorl	%ebp, %ebp
    e9c0:      	andl	$0x3f, %ecx
    e9c3:      	setne	%al
    e9c6:      	cmpl	$0x2, %ecx
    e9c9:      	jb	0xe9f0 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2f0>
    e9cb:      	movl	(%r13), %ebp
    e9cf:      	testb	$0x3e, %r12b
    e9d3:      	jne	0xe9f9 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2f9>
    e9d5:      	lfence
    e9d8:      	rdtsc
    e9da:      	lfence
    e9dd:      	movq	%rdx, %r13
    e9e0:      	shlq	$0x20, %r13
    e9e4:      	orq	%rax, %r13
    e9e7:      	jmp	0xe9fc <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2fc>
    e9e9:      	nopl	(%rax)
    e9f0:      	movb	%al, %bpl
    e9f3:      	testb	$0x3e, %r12b
    e9f7:      	je	0xe9d5 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x2d5>
    e9f9:      	xorl	%r13d, %r13d
    e9fc:      	movq	0x80(%rsp), %rax
    ea04:      	leaq	0x20(%rsp), %rdi
    ea09:      	vmovdqa	%xmm0, 0x90(%rsp)
    ea12:      	leaq	(%rax,%r15,8), %rdx
    ea16:      	leaq	(%rdx,%r14,8), %rcx
    ea1a:      	testb	%bpl, %bpl
    ea1d:      	je	0xea60 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x360>
    ea1f:      	movq	0x8(%rsp), %rsi
    ea24:      	movq	0x10(%rsp), %r8
    ea29:      	vzeroupper
    ea2c:      	callq	0xf700 <Result hint_range<true, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    ea31:      	vmovdqa	0x20(%rsp), %xmm0
    ea37:      	movq	0x30(%rsp), %rsi
    ea3c:      	testb	$0x3e, %r12b
    ea40:      	jne	0xea83 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x383>
    ea42:      	lfence
    ea45:      	rdtsc
    ea47:      	lfence
    ea4a:      	movq	%rax, %rcx
    ea4d:      	movq	%rdx, %rax
    ea50:      	shlq	$0x20, %rax
    ea54:      	orq	%rcx, %rax
    ea57:      	subq	%r13, %rax
    ea5a:      	jmp	0xea85 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x385>
    ea5c:      	nopl	(%rax)
    ea60:      	movq	0x8(%rsp), %rsi
    ea65:      	movq	0x10(%rsp), %r8
    ea6a:      	vzeroupper
    ea6d:      	callq	0xf940 <Result hint_range<true, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    ea72:      	vmovdqa	0x20(%rsp), %xmm0
    ea78:      	movq	0x30(%rsp), %rsi
    ea7d:      	testb	$0x3e, %r12b
    ea81:      	je	0xea42 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x342>
    ea83:      	xorl	%eax, %eax
    ea85:      	movq	0x70(%rsp), %r13
    ea8a:      	movzbl	%bpl, %ecx
    ea8e:      	addq	%r14, 0x18(%r13,%rcx,8)
    ea93:      	movl	0x38(%rbx), %r12d
    ea97:      	testb	$0x3e, %r12b
    ea9b:      	jne	0xe970 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x270>
    eaa1:      	incq	0x28(%r13)
    eaa5:      	movabsq	$0x94f2094f2094, %rcx   # imm = 0x94F2094F2094
    eaaf:      	cmpq	%rcx, %rax
    eab2:      	jb	0xeabe <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x3be>
    eab4:      	movabsq	$0x94f2094f2094, %rax   # imm = 0x94F2094F2094
    eabe:      	shlq	$0xa, %rax
    eac2:      	movabsq	$0x3ffffff00000000, %rcx # imm = 0x3FFFFFF00000000
    eacc:      	testq	%rcx, %rax
    eacf:      	je	0xeb21 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x421>
    ead1:      	xorl	%edx, %edx
    ead3:      	divq	%r14
    ead6:      	movq	0x18(%rsp), %rcx
    eadb:      	testb	%bpl, %bpl
    eade:      	jne	0xeae5 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x3e5>
    eae0:      	movq	0x68(%rsp), %rcx
    eae5:      	movq	%rax, (%rcx)
    eae8:      	testb	$0x1, %r12b
    eaec:      	je	0xe970 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x270>
    eaf2:      	movq	0x18(%rsp), %rax
    eaf7:      	imulq	$0x64, 0x8(%rbx), %rcx
    eafc:      	imulq	$0x6e, (%rax), %rax
    eb00:      	cmpq	%rcx, %rax
    eb03:      	setb	%dl
    eb06:      	xorl	%edi, %edi
    eb08:      	cmpb	%dl, (%r13)
    eb0c:      	setne	%dil
    eb10:      	addq	%rdi, 0x30(%r13)
    eb14:      	cmpq	%rcx, %rax
    eb17:      	setb	(%r13)
    eb1c:      	jmp	0xe970 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x270>
    eb21:      	xorl	%edx, %edx
    eb23:      	divl	%r14d
    eb26:      	movq	0x18(%rsp), %rcx
    eb2b:      	testb	%bpl, %bpl
    eb2e:      	je	0xeae0 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x3e0>
    eb30:      	jmp	0xeae5 <Result hint_requests<true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, ikea::hashtable::lookup_selector*)+0x3e5>
    eb32:      	movq	0x60(%rsp), %rax
    eb37:      	movq	0x58(%rsp), %r9
    eb3c:      	movq	0x50(%rsp), %r10
    eb41:      	movq	0x48(%rsp), %r11
    eb46:      	movq	0x40(%rsp), %r15
    eb4b:      	movq	%rcx, 0x10(%rax)
    eb4f:      	vmovdqu	%xmm0, (%rax)
    eb53:      	movq	0x18(%r13), %rdi
    eb57:      	movq	0x20(%r13), %rsi
    eb5b:      	movq	0x28(%r13), %rdx
    eb5f:      	movq	0x30(%r13), %r8
    eb63:      	subq	%r15, %rdi
    eb66:      	subq	%r11, %rsi
    eb69:      	subq	%r10, %rdx
    eb6c:      	subq	%r9, %r8
    eb6f:      	movq	%rdi, 0xd61a(%rip)      # 0x1c190 <last_selection>
    eb76:      	movq	%rsi, 0xd61b(%rip)      # 0x1c198 <last_selection+0x8>
    eb7d:      	movq	%rdx, 0xd61c(%rip)      # 0x1c1a0 <last_selection+0x10>
    eb84:      	movq	%r8, 0xd61d(%rip)       # 0x1c1a8 <last_selection+0x18>
    eb8b:      	addq	$0xe8, %rsp
    eb92:      	popq	%rbx
    eb93:      	popq	%r12
    eb95:      	popq	%r13
    eb97:      	popq	%r14
    eb99:      	popq	%r15
    eb9b:      	popq	%rbp
    eb9c:      	vzeroupper
    eb9f:      	retq
    eba0:      	nopw	%cs:(%rax,%rax)
    ebaa:      	nopw	%cs:(%rax,%rax)
    ebb4:      	nopw	%cs:(%rax,%rax)
    ebbe:      	nop
