000000000000d980 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    d980:      	pushq	%rbp
    d981:      	pushq	%r15
    d983:      	pushq	%r14
    d985:      	pushq	%r13
    d987:      	pushq	%r12
    d989:      	pushq	%rbx
    d98a:      	subq	$0x28, %rsp
    d98e:      	vpxor	%xmm0, %xmm0, %xmm0
    d992:      	vmovdqu	%xmm0, 0x8(%rdi)
    d997:      	movq	%rdi, %rax
    d99a:      	movq	(%rdx), %r13
    d99d:      	movq	0x8(%rdx), %rcx
    d9a1:      	subq	%r13, %rcx
    d9a4:      	je	0xdb7c <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1fc>
    d9aa:      	vmovdqa	0x876e(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    d9b2:      	vpmovsxbd	0x8701(%rip), %xmm4 # 0x160bc <syscall@GLIBC_2.2.5+0x160bc>
    d9bb:      	vpbroadcastd	0x86f0(%rip), %xmm5 # 0x160b4 <syscall@GLIBC_2.2.5+0x160b4>
    d9c4:      	vpbroadcastd	0x86eb(%rip), %xmm6 # 0x160b8 <syscall@GLIBC_2.2.5+0x160b8>
    d9cd:      	movq	%rax, 0x8(%rsp)
    d9d2:      	movq	0x70(%rsi), %rax
    d9d6:      	movq	0x10(%rsi), %rbp
    d9da:      	sarq	$0x3, %rcx
    d9de:      	movq	%rsi, %r11
    d9e1:      	xorl	%ebx, %ebx
    d9e3:      	xorl	%r12d, %r12d
    d9e6:      	xorl	%r14d, %r14d
    d9e9:      	movq	%r13, 0x10(%rsp)
    d9ee:      	movq	%rcx, 0x20(%rsp)
    d9f3:      	movq	%rax, 0x18(%rsp)
    d9f8:      	jmp	0xda1d <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x9d>
    d9fa:      	xorl	%esi, %esi
    d9fc:      	nopl	(%rax)
    da00:      	movq	0x20(%rsp), %rcx
    da05:      	addq	%rsi, %r14
    da08:      	negq	%rsi
    da0b:      	incq	%r12
    da0e:      	andq	%rsi, %r15
    da11:      	addq	%r15, %rbx
    da14:      	cmpq	%r12, %rcx
    da17:      	je	0xdb71 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1f1>
    da1d:      	movq	(%r13,%r12,8), %r15
    da22:      	movq	0x18(%rsp), %rdx
    da27:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    da31:      	xorq	%r15, %rdx
    da34:      	mulxq	%rax, %rax, %rdx
    da39:      	movq	%rax, %rcx
    da3c:      	shrq	$0x16, %rcx
    da40:      	movl	%eax, %esi
    da42:      	andl	$0xfffffff, %esi        # imm = 0xFFFFFFF
    da48:      	andl	$0x3ffc0, %ecx          # imm = 0x3FFC0
    da4e:      	vpbroadcastd	%esi, %xmm0
    da54:      	shrl	$0x5, %esi
    da57:      	vmovdqu	(%rbp,%rcx), %xmm1
    da5d:      	vpbroadcastd	%esi, %ymm2
    da63:      	vpsrlvd	%xmm4, %xmm0, %xmm0
    da68:      	movzbl	0x18(%rbp,%rcx), %r8d
    da6e:      	movl	%eax, %esi
    da70:      	vinserti128	$0x1, 0x8(%rbp,%rcx), %ymm1, %ymm1
    da78:      	vpand	%xmm5, %xmm0, %xmm0
    da7c:      	andb	$0x1, %sil
    da80:      	vpaddd	%xmm6, %xmm0, %xmm0
    da84:      	decb	%sil
    da87:      	xorb	%sil, %r8b
    da8a:      	xorb	0x35(%rbp,%rcx), %sil
    da8f:      	vpshufb	%ymm3, %ymm1, %ymm1
    da94:      	vpcmpeqd	%ymm1, %ymm2, %k0
    da9a:      	vmovdqu	0x1d(%rbp,%rcx), %xmm1
    daa0:      	vinserti128	$0x1, 0x25(%rbp,%rcx), %ymm1, %ymm1
    daa8:      	kmovd	%k0, %edi
    daac:      	vpshufb	%ymm3, %ymm1, %ymm1
    dab1:      	vpcmpeqd	%ymm1, %ymm2, %k0
    dab7:      	vpmovzxbd	0x19(%rbp,%rcx), %xmm1
    dabe:      	kmovd	%k0, %r9d
    dac2:      	vpxor	%xmm1, %xmm0, %xmm1
    dac6:      	vpshufd	$0xee, %xmm1, %xmm2     # xmm2 = xmm1[2,3,2,3]
    dacb:      	vpand	%xmm2, %xmm1, %xmm1
    dacf:      	vpshufd	$0x55, %xmm1, %xmm2     # xmm2 = xmm1[1,1,1,1]
    dad4:      	vpand	%xmm2, %xmm1, %xmm1
    dad8:      	vmovd	%xmm1, %r10d
    dadd:      	vpmovzxbd	0x36(%rbp,%rcx), %xmm1
    dae4:      	andb	%r8b, %r10b
    dae7:      	andb	%dil, %r10b
    daea:      	vpxor	%xmm1, %xmm0, %xmm0
    daee:      	vpshufd	$0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
    daf3:      	vpand	%xmm1, %xmm0, %xmm0
    daf7:      	vpshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
    dafc:      	vpand	%xmm1, %xmm0, %xmm0
    db00:      	vmovd	%xmm0, %edi
    db04:      	andb	%sil, %dil
    db07:      	movl	$0x1, %esi
    db0c:      	andb	%r9b, %dil
    db0f:      	orb	%r10b, %dil
    db12:      	jne	0xda00 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x80>
    db18:      	cmpb	$0x0, 0x3f(%rbp,%rcx)
    db1d:      	je	0xd9fa <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x7a>
    db23:      	xorq	%rax, %rdx
    db26:      	movq	%r11, %rdi
    db29:      	movq	%r15, %rsi
    db2c:      	movq	%r11, %r13
    db2f:      	vzeroupper
    db32:      	callq	0xe380 <ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    db37:      	vpbroadcastd	0x8578(%rip), %xmm6 # 0x160b8 <syscall@GLIBC_2.2.5+0x160b8>
    db40:      	vpbroadcastd	0x856b(%rip), %xmm5 # 0x160b4 <syscall@GLIBC_2.2.5+0x160b4>
    db49:      	vpmovsxbd	0x856a(%rip), %xmm4 # 0x160bc <syscall@GLIBC_2.2.5+0x160bc>
    db52:      	vmovdqa	0x85c6(%rip), %ymm3     # 0x16120 <syscall@GLIBC_2.2.5+0x16120>
    db5a:      	movq	%r13, %r11
    db5d:      	movq	0x10(%rsp), %r13
    db62:      	movl	$0x108, %ecx            # imm = 0x108
    db67:      	bextrl	%ecx, %eax, %esi
    db6c:      	jmp	0xda00 <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x80>
    db71:      	movq	0x8(%rsp), %rax
    db76:      	movq	%rbx, 0x8(%rax)
    db7a:      	jmp	0xdb7f <Result read_loop<false, true, ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 12u, 0u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1ff>
    db7c:      	xorl	%r14d, %r14d
    db7f:      	movq	%r14, (%rax)
    db82:      	addq	$0x28, %rsp
    db86:      	popq	%rbx
    db87:      	popq	%r12
    db89:      	popq	%r13
    db8b:      	popq	%r14
    db8d:      	popq	%r15
    db8f:      	popq	%rbp
    db90:      	vzeroupper
    db93:      	retq
    db94:      	nopw	%cs:(%rax,%rax)
    db9e:      	nopw	%cs:(%rax,%rax)
    dba8:      	nopw	%cs:(%rax,%rax)
    dbb2:      	nopw	%cs:(%rax,%rax)
    dbbc:      	nopl	(%rax)

