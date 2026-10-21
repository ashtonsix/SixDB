000000000000d340 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    d340:      	pushq	%rbp
    d341:      	pushq	%r15
    d343:      	pushq	%r14
    d345:      	pushq	%r13
    d347:      	pushq	%r12
    d349:      	pushq	%rbx
    d34a:      	subq	$0x28, %rsp
    d34e:      	vpxor	%xmm0, %xmm0, %xmm0
    d352:      	vmovdqu	%xmm0, 0x8(%rdi)
    d357:      	movq	%rdi, %rax
    d35a:      	movq	(%rdx), %rdi
    d35d:      	movq	0x8(%rdx), %r9
    d361:      	subq	%rdi, %r9
    d364:      	je	0xd47b <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x13b>
    d36a:      	vmovdqa	0x7d4e(%rip), %ymm3     # 0x150c0 <syscall@GLIBC_2.2.5+0x150c0>
    d372:      	movq	0x70(%rsi), %r10
    d376:      	movq	0x10(%rsi), %rbp
    d37a:      	sarq	$0x3, %r9
    d37e:      	movabsq	$-0x61c8864e7a143579, %r13 # imm = 0x9E3779B185EBCA87
    d388:      	movq	%rsi, %r8
    d38b:      	movq	%rax, (%rsp)
    d38f:      	xorl	%r14d, %r14d
    d392:      	xorl	%r12d, %r12d
    d395:      	xorl	%ebx, %ebx
    d397:      	movq	%rdi, 0x20(%rsp)
    d39c:      	movq	%rsi, 0x18(%rsp)
    d3a1:      	movq	%r9, 0x8(%rsp)
    d3a6:      	movq	%r10, 0x10(%rsp)
    d3ab:      	jmp	0xd3c8 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x88>
    d3ad:      	xorl	%esi, %esi
    d3af:      	nop
    d3b0:      	addq	%rsi, %rbx
    d3b3:      	negq	%rsi
    d3b6:      	incq	%r12
    d3b9:      	andq	%rsi, %r15
    d3bc:      	addq	%r15, %r14
    d3bf:      	cmpq	%r12, %r9
    d3c2:      	je	0xd471 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x131>
    d3c8:      	movq	(%rdi,%r12,8), %r15
    d3cc:      	movq	%r10, %rdx
    d3cf:      	xorq	%r15, %rdx
    d3d2:      	mulxq	%r13, %rax, %rdx
    d3d7:      	movl	%eax, %ecx
    d3d9:      	shrl	$0xf, %ecx
    d3dc:      	movl	%eax, %esi
    d3de:      	andl	$0x1fffff, %esi         # imm = 0x1FFFFF
    d3e4:      	andl	$-0x40, %ecx
    d3e7:      	vpbroadcastd	%esi, %ymm2
    d3ed:      	movl	$0x1, %esi
    d3f2:      	vmovdqu	(%rbp,%rcx), %xmm0
    d3f8:      	vmovdqu	0x18(%rbp,%rcx), %xmm1
    d3fe:      	vinserti128	$0x1, 0x8(%rbp,%rcx), %ymm0, %ymm0
    d406:      	vpshufb	%ymm3, %ymm0, %ymm0
    d40b:      	vpcmpeqd	%ymm0, %ymm2, %k0
    d411:      	vinserti128	$0x1, 0x20(%rbp,%rcx), %ymm1, %ymm0
    d419:      	vpshufb	%ymm3, %ymm0, %ymm0
    d41e:      	vpcmpeqd	%ymm0, %ymm2, %k1
    d424:      	kortestb	%k1, %k0
    d428:      	jne	0xd3b0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x70>
    d42a:      	cmpb	$0x0, 0x3f(%rbp,%rcx)
    d42f:      	je	0xd3ad <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x6d>
    d435:      	xorq	%rax, %rdx
    d438:      	movq	%r8, %rdi
    d43b:      	movq	%r15, %rsi
    d43e:      	vzeroupper
    d441:      	callq	0xdb80 <ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    d446:      	vmovdqa	0x7c72(%rip), %ymm3     # 0x150c0 <syscall@GLIBC_2.2.5+0x150c0>
    d44e:      	movq	0x10(%rsp), %r10
    d453:      	movq	0x8(%rsp), %r9
    d458:      	movq	0x20(%rsp), %rdi
    d45d:      	movq	0x18(%rsp), %r8
    d462:      	movl	$0x108, %ecx            # imm = 0x108
    d467:      	bextrl	%ecx, %eax, %esi
    d46c:      	jmp	0xd3b0 <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x70>
    d471:      	movq	(%rsp), %rax
    d475:      	movq	%r14, 0x8(%rax)
    d479:      	jmp	0xd47d <Result read_loop<false, true, ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<32u, 11u, 0u, 24u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x13d>
    d47b:      	xorl	%ebx, %ebx
    d47d:      	movq	%rbx, (%rax)
    d480:      	addq	$0x28, %rsp
    d484:      	popq	%rbx
    d485:      	popq	%r12
    d487:      	popq	%r13
    d489:      	popq	%r14
    d48b:      	popq	%r15
    d48d:      	popq	%rbp
    d48e:      	vzeroupper
    d491:      	retq
    d492:      	nopw	%cs:(%rax,%rax)
    d49c:      	nopw	%cs:(%rax,%rax)
    d4a6:      	nopw	%cs:(%rax,%rax)
    d4b0:      	nopw	%cs:(%rax,%rax)
    d4ba:      	nopw	(%rax,%rax)

