000000000000eb40 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)>:
    eb40:      	pushq	%rbp
    eb41:      	pushq	%r15
    eb43:      	pushq	%r14
    eb45:      	pushq	%r13
    eb47:      	pushq	%r12
    eb49:      	pushq	%rbx
    eb4a:      	subq	$0xc8, %rsp
    eb51:      	testq	%rcx, %rcx
    eb54:      	leaq	0x88(%rsp), %r14
    eb5c:      	movq	%rcx, %rbp
    eb5f:      	movq	0x8(%rdx), %r8
    eb63:      	movq	(%rdx), %r13
    eb66:      	movb	$0x0, 0x88(%rsp)
    eb6e:      	vpxor	%xmm0, %xmm0, %xmm0
    eb72:      	vmovdqu	%ymm0, 0x90(%rsp)
    eb7b:      	vmovdqu	%ymm0, 0xa4(%rsp)
    eb84:      	movq	%rsi, 0x18(%rsp)
    eb89:      	movq	%rdi, %r12
    eb8c:      	vpxor	%xmm0, %xmm0, %xmm0
    eb90:      	cmoveq	%r14, %rbp
    eb94:      	movq	0x20(%rbp), %r11
    eb98:      	movq	0x28(%rbp), %r10
    eb9c:      	movq	0x30(%rbp), %r9
    eba0:      	movq	0x18(%rbp), %rbx
    eba4:      	vmovdqu	%xmm0, (%rdi)
    eba8:      	movq	$0x0, 0x10(%rdi)
    ebb0:      	movq	%r8, 0x8(%rsp)
    ebb5:      	movq	%r9, %rax
    ebb8:      	movq	%r10, %rdx
    ebbb:      	movq	%r11, %rsi
    ebbe:      	movq	%rbx, %rdi
    ebc1:      	cmpq	%r13, %r8
    ebc4:      	je	0xedb8 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x278>
    ebca:      	testq	%rcx, %rcx
    ebcd:      	movq	%rbx, 0x20(%rsp)
    ebd2:      	movq	0x8(%rsp), %r8
    ebd7:      	leaq	0x90(%rsp), %rax
    ebdf:      	leaq	0x8(%rcx), %rdx
    ebe3:      	movq	%r13, %r15
    ebe6:      	movq	%r9, 0x38(%rsp)
    ebeb:      	movq	%r11, 0x28(%rsp)
    ebf0:      	movq	%r10, 0x30(%rsp)
    ebf5:      	movq	%r15, %r9
    ebf8:      	movq	%rbp, 0x48(%rsp)
    ebfd:      	cmovneq	%rcx, %r14
    ec01:      	cmoveq	%rax, %rdx
    ec05:      	movl	0x38(%r14), %ebx
    ec09:      	leaq	0x10(%r14), %rax
    ec0d:      	movq	%r14, %r13
    ec10:      	movq	%rdx, 0x40(%rsp)
    ec15:      	movq	%rax, 0x10(%rsp)
    ec1a:      	jmp	0xec43 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x103>
    ec1c:      	nopl	(%rax)
    ec20:      	incl	%ebx
    ec22:      	movq	0x50(%rsp), %r9
    ec27:      	movl	%ebx, 0x38(%r13)
    ec2b:      	movq	0x58(%rsp), %rax
    ec30:      	vpaddq	0x60(%rsp), %xmm0, %xmm0
    ec36:      	addq	%rax, (%r12)
    ec3a:      	cmpq	%r8, %r9
    ec3d:      	je	0xed8d <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x24d>
    ec43:      	movq	%r8, %r15
    ec46:      	subq	%r9, %r15
    ec49:      	movl	$0x100, %eax            # imm = 0x100
    ec4e:      	movl	%ebx, %edx
    ec50:      	sarq	$0x3, %r15
    ec54:      	cmpq	$0x100, %r15            # imm = 0x100
    ec5b:      	cmovaeq	%rax, %r15
    ec5f:      	xorl	%r14d, %r14d
    ec62:      	andl	$0x3f, %edx
    ec65:      	setne	%al
    ec68:      	cmpl	$0x2, %edx
    ec6b:      	jb	0xec90 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x150>
    ec6d:      	movl	(%rbp), %r14d
    ec71:      	testb	$0x3e, %bl
    ec74:      	jne	0xec98 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x158>
    ec76:      	lfence
    ec79:      	rdtsc
    ec7b:      	lfence
    ec7e:      	movq	%rdx, %rbp
    ec81:      	shlq	$0x20, %rbp
    ec85:      	orq	%rax, %rbp
    ec88:      	jmp	0xec9a <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x15a>
    ec8a:      	nopw	(%rax,%rax)
    ec90:      	movb	%al, %r14b
    ec93:      	testb	$0x3e, %bl
    ec96:      	je	0xec76 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x136>
    ec98:      	xorl	%ebp, %ebp
    ec9a:      	leaq	(%r9,%r15,8), %rcx
    ec9e:      	leaq	0x58(%rsp), %rdi
    eca3:      	vmovdqa	%xmm0, 0x70(%rsp)
    eca9:      	movq	%rcx, 0x50(%rsp)
    ecae:      	testb	%r14b, %r14b
    ecb1:      	je	0xecf0 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x1b0>
    ecb3:      	movq	0x18(%rsp), %rsi
    ecb8:      	movq	%r9, %rdx
    ecbb:      	vzeroupper
    ecbe:      	callq	0xf940 <Result hint_range<false, true, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    ecc3:      	vmovdqa	0x70(%rsp), %xmm0
    ecc9:      	testb	$0x3e, %bl
    eccc:      	jne	0xed0b <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x1cb>
    ecce:      	lfence
    ecd1:      	rdtsc
    ecd3:      	lfence
    ecd6:      	movq	%rax, %rcx
    ecd9:      	movq	%rdx, %rax
    ecdc:      	shlq	$0x20, %rax
    ece0:      	orq	%rcx, %rax
    ece3:      	subq	%rbp, %rax
    ece6:      	shlq	$0xa, %rax
    ecea:      	jmp	0xed0d <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x1cd>
    ecec:      	nopl	(%rax)
    ecf0:      	movq	0x18(%rsp), %rsi
    ecf5:      	movq	%r9, %rdx
    ecf8:      	vzeroupper
    ecfb:      	callq	0xfb40 <Result hint_range<false, true, false, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>
    ed00:      	vmovdqa	0x70(%rsp), %xmm0
    ed06:      	testb	$0x3e, %bl
    ed09:      	je	0xecce <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x18e>
    ed0b:      	xorl	%eax, %eax
    ed0d:      	movq	0x48(%rsp), %rbp
    ed12:      	movzbl	%r14b, %ecx
    ed16:      	movq	0x8(%rsp), %r8
    ed1b:      	addq	%r15, 0x18(%rbp,%rcx,8)
    ed20:      	movl	0x38(%r13), %ebx
    ed24:      	testb	$0x3e, %bl
    ed27:      	jne	0xec20 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0xe0>
    ed2d:      	incq	0x28(%rbp)
    ed31:      	movq	%rax, %rcx
    ed34:      	shrq	$0x20, %rcx
    ed38:      	je	0xed41 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x201>
    ed3a:      	xorl	%edx, %edx
    ed3c:      	divq	%r15
    ed3f:      	jmp	0xed46 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0x206>
    ed41:      	xorl	%edx, %edx
    ed43:      	divl	%r15d
    ed46:      	testb	%r14b, %r14b
    ed49:      	movq	0x40(%rsp), %rcx
    ed4e:      	cmovneq	0x10(%rsp), %rcx
    ed54:      	movq	%rax, (%rcx)
    ed57:      	testb	$0x1, %bl
    ed5a:      	je	0xec20 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0xe0>
    ed60:      	movq	0x10(%rsp), %rax
    ed65:      	imulq	$0x64, 0x8(%r13), %rcx
    ed6a:      	imulq	$0x6e, (%rax), %rax
    ed6e:      	cmpq	%rcx, %rax
    ed71:      	setb	%dl
    ed74:      	xorl	%esi, %esi
    ed76:      	cmpb	%dl, (%rbp)
    ed79:      	setne	%sil
    ed7d:      	addq	%rsi, 0x30(%rbp)
    ed81:      	cmpq	%rcx, %rax
    ed84:      	setb	(%rbp)
    ed88:      	jmp	0xec20 <Result hint_requests<false, true, ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 8u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&, PredictabilitySelector*)+0xe0>
    ed8d:      	vmovdqu	%xmm0, 0x8(%r12)
    ed94:      	movq	0x38(%rsp), %r9
    ed99:      	movq	0x30(%rsp), %r10
    ed9e:      	movq	0x28(%rsp), %r11
    eda3:      	movq	0x20(%rsp), %rbx
    eda8:      	movq	0x18(%rbp), %rdi
    edac:      	movq	0x20(%rbp), %rsi
    edb0:      	movq	0x28(%rbp), %rdx
    edb4:      	movq	0x30(%rbp), %rax
    edb8:      	subq	%rbx, %rdi
    edbb:      	subq	%r11, %rsi
    edbe:      	subq	%r10, %rdx
    edc1:      	subq	%r9, %rax
    edc4:      	movq	%rdi, 0xd3c5(%rip)      # 0x1c190 <last_selection>
    edcb:      	movq	%rsi, 0xd3c6(%rip)      # 0x1c198 <last_selection+0x8>
    edd2:      	movq	%rdx, 0xd3c7(%rip)      # 0x1c1a0 <last_selection+0x10>
    edd9:      	movq	%rax, 0xd3c8(%rip)      # 0x1c1a8 <last_selection+0x18>
    ede0:      	movq	%r12, %rax
    ede3:      	addq	$0xc8, %rsp
    edea:      	popq	%rbx
    edeb:      	popq	%r12
    eded:      	popq	%r13
    edef:      	popq	%r14
    edf1:      	popq	%r15
    edf3:      	popq	%rbp
    edf4:      	vzeroupper
    edf7:      	retq
    edf8:      	nopl	(%rax,%rax)
