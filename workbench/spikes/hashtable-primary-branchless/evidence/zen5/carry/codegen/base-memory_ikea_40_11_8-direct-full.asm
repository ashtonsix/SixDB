000000000000dc00 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)>:
    dc00:      	pushq	%rbp
    dc01:      	pushq	%r15
    dc03:      	pushq	%r14
    dc05:      	pushq	%r13
    dc07:      	pushq	%r12
    dc09:      	pushq	%rbx
    dc0a:      	subq	$0x28, %rsp
    dc0e:      	vpxor	%xmm0, %xmm0, %xmm0
    dc12:      	vmovdqu	%xmm0, 0x8(%rdi)
    dc17:      	movq	%rdi, %rax
    dc1a:      	movq	(%rdx), %rcx
    dc1d:      	movq	0x8(%rdx), %rdx
    dc21:      	subq	%rcx, %rdx
    dc24:      	movq	%rcx, 0x20(%rsp)
    dc29:      	je	0xde48 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x248>
    dc2f:      	vmovdqa	0x8589(%rip), %ymm5     # 0x161c0 <syscall@GLIBC_2.2.5+0x161c0>
    dc37:      	vpmovsxbd	0x848c(%rip), %xmm6 # 0x160cc <syscall@GLIBC_2.2.5+0x160cc>
    dc40:      	vpbroadcastd	0x847b(%rip), %xmm7 # 0x160c4 <syscall@GLIBC_2.2.5+0x160c4>
    dc49:      	vpbroadcastd	0x8476(%rip), %xmm8 # 0x160c8 <syscall@GLIBC_2.2.5+0x160c8>
    dc52:      	movq	%rax, (%rsp)
    dc56:      	movq	0x70(%rsi), %rax
    dc5a:      	movq	0x10(%rsi), %rbp
    dc5e:      	sarq	$0x3, %rdx
    dc62:      	movq	%rsi, 0x8(%rsp)
    dc67:      	xorl	%ebx, %ebx
    dc69:      	xorl	%r12d, %r12d
    dc6c:      	xorl	%r13d, %r13d
    dc6f:      	xorl	%r14d, %r14d
    dc72:      	movq	%rdx, 0x18(%rsp)
    dc77:      	movq	%rax, 0x10(%rsp)
    dc7c:      	jmp	0xdcaf <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0xaf>
    dc7e:      	nop
    dc80:      	tzcntq	%rdi, %rax
    dc85:      	movq	0x40(%rcx,%rax,8), %rax
    dc8a:      	movl	$0x1, %ecx
    dc8f:      	movq	0x18(%rsp), %rdx
    dc94:      	addq	%rcx, %r14
    dc97:      	negq	%rcx
    dc9a:      	addq	%rax, %rbx
    dc9d:      	incq	%r13
    dca0:      	andq	%rcx, %r15
    dca3:      	addq	%r15, %r12
    dca6:      	cmpq	%r13, %rdx
    dca9:      	je	0xde3a <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x23a>
    dcaf:      	movq	0x20(%rsp), %rax
    dcb4:      	movq	0x10(%rsp), %rdx
    dcb9:      	movl	$0xb1d, %ecx            # imm = 0xB1D
    dcbe:      	movq	(%rax,%r13,8), %r15
    dcc2:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
    dccc:      	xorq	%r15, %rdx
    dccf:      	mulxq	%rax, %rax, %rdx
    dcd4:      	bextrq	%rcx, %rax, %rcx
    dcd9:      	movl	%eax, %esi
    dcdb:      	andl	$0x1fffffff, %esi       # imm = 0x1FFFFFFF
    dce1:      	leaq	(%rcx,%rcx,2), %rdi
    dce5:      	movl	%esi, %r8d
    dce8:      	shrl	$0x5, %r8d
    dcec:      	vpbroadcastd	%esi, %xmm3
    dcf2:      	vpbroadcastd	%r8d, %ymm2
    dcf8:      	vpsrlvd	%xmm6, %xmm3, %xmm3
    dcfd:      	movl	%eax, %r8d
    dd00:      	shll	$0x6, %edi
    dd03:      	vpand	%xmm7, %xmm3, %xmm3
    dd07:      	andb	$0x1, %r8b
    dd0b:      	vmovdqu	(%rbp,%rdi), %xmm0
    dd11:      	vpaddd	%xmm3, %xmm8, %xmm3
    dd15:      	vmovdqu	0x1d(%rbp,%rdi), %xmm1
    dd1b:      	movzbl	0x18(%rbp,%rdi), %r10d
    dd21:      	decb	%r8b
    dd24:      	leaq	(%rbp,%rdi), %rcx
    dd29:      	vinserti128	$0x1, 0x8(%rbp,%rdi), %ymm0, %ymm0
    dd31:      	xorb	%r8b, %r10b
    dd34:      	xorb	0x35(%rbp,%rdi), %r8b
    dd39:      	vpshufb	%ymm5, %ymm0, %ymm0
    dd3e:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dd44:      	vpmovzxbd	0x19(%rbp,%rdi), %xmm0
    dd4b:      	kmovd	%k0, %r9d
    dd4f:      	vpxor	%xmm0, %xmm3, %xmm0
    dd53:      	vpshufd	$0xee, %xmm0, %xmm4     # xmm4 = xmm0[2,3,2,3]
    dd58:      	vpand	%xmm4, %xmm0, %xmm0
    dd5c:      	vpshufd	$0x55, %xmm0, %xmm4     # xmm4 = xmm0[1,1,1,1]
    dd61:      	vpand	%xmm4, %xmm0, %xmm0
    dd65:      	vmovd	%xmm0, %r11d
    dd6a:      	vinserti128	$0x1, 0x25(%rbp,%rdi), %ymm1, %ymm0
    dd72:      	andb	%r10b, %r11b
    dd75:      	andb	%r9b, %r11b
    dd78:      	movzbl	%r11b, %r9d
    dd7c:      	vpshufb	%ymm5, %ymm0, %ymm0
    dd81:      	vpcmpeqd	%ymm0, %ymm2, %k0
    dd87:      	vpmovzxbd	0x36(%rbp,%rdi), %xmm0
    dd8e:      	kmovd	%k0, %r10d
    dd92:      	vpxor	%xmm0, %xmm3, %xmm0
    dd96:      	vpshufd	$0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
    dd9b:      	vpand	%xmm1, %xmm0, %xmm0
    dd9f:      	vpshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
    dda4:      	vpand	%xmm1, %xmm0, %xmm0
    dda8:      	vmovd	%xmm0, %edi
    ddac:      	andb	%r8b, %dil
    ddaf:      	andb	%r10b, %dil
    ddb2:      	movzbl	%dil, %edi
    ddb6:      	shll	$0x8, %edi
    ddb9:      	orq	%r9, %rdi
    ddbc:      	testq	%rsi, %rsi
    ddbf:      	je	0xde23 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x223>
    ddc1:      	testq	%rdi, %rdi
    ddc4:      	jne	0xdc80 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x80>
    ddca:      	cmpb	$0x0, 0x3f(%rcx)
    ddce:      	je	0xde1a <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x21a>
    ddd0:      	movq	0x8(%rsp), %rdi
    ddd5:      	xorq	%rax, %rdx
    ddd8:      	movq	%r15, %rsi
    dddb:      	vzeroupper
    ddde:      	callq	0xe6c0 <ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
    dde3:      	vpbroadcastd	0x82dc(%rip), %xmm8 # 0x160c8 <syscall@GLIBC_2.2.5+0x160c8>
    ddec:      	vpbroadcastd	0x82cf(%rip), %xmm7 # 0x160c4 <syscall@GLIBC_2.2.5+0x160c4>
    ddf5:      	vpmovsxbd	0x82ce(%rip), %xmm6 # 0x160cc <syscall@GLIBC_2.2.5+0x160cc>
    ddfe:      	vmovdqa	0x83ba(%rip), %ymm5     # 0x161c0 <syscall@GLIBC_2.2.5+0x161c0>
    de06:      	andb	$0x1, %dl
    de09:      	movl	$0x0, %ecx
    de0e:      	cmoveq	%rcx, %rax
    de12:      	movzbl	%dl, %ecx
    de15:      	jmp	0xdc8f <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x8f>
    de1a:      	xorl	%eax, %eax
    de1c:      	xorl	%ecx, %ecx
    de1e:      	jmp	0xdc8f <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x8f>
    de23:      	movzwl	0x3a(%rcx), %esi
    de27:      	xorl	$0xffff, %esi           # imm = 0xFFFF
    de2d:      	andl	%esi, %edi
    de2f:      	testq	%rdi, %rdi
    de32:      	jne	0xdc80 <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x80>
    de38:      	jmp	0xddca <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x1ca>
    de3a:      	movq	(%rsp), %rax
    de3e:      	movq	%r12, 0x8(%rax)
    de42:      	movq	%rbx, 0x10(%rax)
    de46:      	jmp	0xde4b <Result read_loop<false, true, ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<40u, 11u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, std::vector<unsigned long, std::allocator<unsigned long>> const&)+0x24b>
    de48:      	xorl	%r14d, %r14d
    de4b:      	movq	%r14, (%rax)
    de4e:      	addq	$0x28, %rsp
    de52:      	popq	%rbx
    de53:      	popq	%r12
    de55:      	popq	%r13
    de57:      	popq	%r14
    de59:      	popq	%r15
    de5b:      	popq	%rbp
    de5c:      	vzeroupper
    de5f:      	retq
    de60:      	nopw	%cs:(%rax,%rax)
    de6a:      	nopw	%cs:(%rax,%rax)
    de74:      	nopw	%cs:(%rax,%rax)
    de7e:      	nop

