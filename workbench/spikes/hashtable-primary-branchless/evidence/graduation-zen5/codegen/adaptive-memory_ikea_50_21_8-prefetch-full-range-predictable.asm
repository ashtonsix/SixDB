000000000000ff80 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)>:
    ff80:      	pushq	%rbp
    ff81:      	pushq	%r15
    ff83:      	pushq	%r14
    ff85:      	pushq	%r13
    ff87:      	pushq	%r12
    ff89:      	pushq	%rbx
    ff8a:      	subq	$0x28, %rsp
    ff8e:      	vpxor	%xmm0, %xmm0, %xmm0
    ff92:      	movq	%rdi, %rax
    ff95:      	vmovdqu	%xmm0, 0x8(%rdi)
    ff9a:      	movq	%rcx, 0x20(%rsp)
    ff9f:      	cmpq	%rcx, %rdx
    ffa2:      	je	0x101ec <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x26c>
    ffa8:      	vmovdqa	0x8210(%rip), %ymm5     # 0x181c0 <syscall@GLIBC_2.2.5+0x181c0>
    ffb0:      	vpmovsxbd	0x8113(%rip), %xmm6 # 0x180cc <syscall@GLIBC_2.2.5+0x180cc>
    ffb9:      	vpbroadcastd	0x8102(%rip), %xmm7 # 0x180c4 <syscall@GLIBC_2.2.5+0x180c4>
    ffc2:      	vpbroadcastd	0x80fd(%rip), %xmm8 # 0x180c8 <syscall@GLIBC_2.2.5+0x180c8>
    ffcb:      	movq	%rax, 0x10(%rsp)
    ffd0:      	movq	0x70(%rsi), %rax
    ffd4:      	movq	0x10(%rsi), %rbx
    ffd8:      	movq	%r8, %r14
    ffdb:      	subq	%rdx, %r14
    ffde:      	movq	%rdx, %r12
    ffe1:      	movq	%rsi, 0x18(%rsp)
    ffe6:      	movq	$0x0, (%rsp)
    ffee:      	xorl	%r15d, %r15d
    fff1:      	xorl	%r13d, %r13d
    fff4:      	movq	%rax, 0x8(%rsp)
    fff9:      	jmp	0x10027 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xa7>
    fffb:      	nopl	(%rax,%rax)
   10000:      	tzcntq	%rdi, %rax
   10005:      	movq	0x40(%rcx,%rax,8), %rax
   1000a:      	addq	%rax, (%rsp)
   1000e:      	incq	%r13
   10011:      	addq	%rbp, %r15
   10014:      	addq	$0x8, %r12
   10018:      	addq	$-0x8, %r14
   1001c:      	cmpq	0x20(%rsp), %r12
   10021:      	je	0x101d9 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x259>
   10027:      	cmpq	$0x101, %r14            # imm = 0x101
   1002e:      	jl	0x10060 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0xe0>
   10030:      	movq	0x100(%r12), %rax
   10038:      	movabsq	$0x379b185ebca87, %rcx  # imm = 0x379B185EBCA87
   10042:      	xorq	0x8(%rsp), %rax
   10047:      	imulq	%rcx, %rax
   1004b:      	movl	$0x151d, %ecx           # imm = 0x151D
   10050:      	bextrq	%rcx, %rax, %rax
   10055:      	leaq	(%rax,%rax,2), %rax
   10059:      	shll	$0x6, %eax
   1005c:      	prefetcht0	(%rbx,%rax)
   10060:      	movq	(%r12), %rbp
   10064:      	movq	0x8(%rsp), %rdx
   10069:      	movabsq	$-0x61c8864e7a143579, %rax # imm = 0x9E3779B185EBCA87
   10073:      	movl	$0x151d, %ecx           # imm = 0x151D
   10078:      	xorq	%rbp, %rdx
   1007b:      	mulxq	%rax, %rax, %rdx
   10080:      	bextrq	%rcx, %rax, %rcx
   10085:      	movl	%eax, %esi
   10087:      	andl	$0x1fffffff, %esi       # imm = 0x1FFFFFFF
   1008d:      	leaq	(%rcx,%rcx,2), %rdi
   10091:      	movl	%esi, %r8d
   10094:      	shrl	$0x5, %r8d
   10098:      	vpbroadcastd	%esi, %xmm3
   1009e:      	vpbroadcastd	%r8d, %ymm2
   100a4:      	vpsrlvd	%xmm6, %xmm3, %xmm3
   100a9:      	movl	%eax, %r8d
   100ac:      	shll	$0x6, %edi
   100af:      	vpand	%xmm7, %xmm3, %xmm3
   100b3:      	andb	$0x1, %r8b
   100b7:      	vmovdqu	(%rbx,%rdi), %xmm0
   100bc:      	vpaddd	%xmm3, %xmm8, %xmm3
   100c0:      	vmovdqu	0x1d(%rbx,%rdi), %xmm1
   100c6:      	movzbl	0x18(%rbx,%rdi), %r10d
   100cc:      	decb	%r8b
   100cf:      	leaq	(%rbx,%rdi), %rcx
   100d3:      	vinserti128	$0x1, 0x8(%rbx,%rdi), %ymm0, %ymm0
   100db:      	xorb	%r8b, %r10b
   100de:      	xorb	0x35(%rbx,%rdi), %r8b
   100e3:      	vpshufb	%ymm5, %ymm0, %ymm0
   100e8:      	vpcmpeqd	%ymm0, %ymm2, %k0
   100ee:      	vpmovzxbd	0x19(%rbx,%rdi), %xmm0
   100f5:      	kmovd	%k0, %r9d
   100f9:      	vpxor	%xmm0, %xmm3, %xmm0
   100fd:      	vpshufd	$0xee, %xmm0, %xmm4     # xmm4 = xmm0[2,3,2,3]
   10102:      	vpand	%xmm4, %xmm0, %xmm0
   10106:      	vpshufd	$0x55, %xmm0, %xmm4     # xmm4 = xmm0[1,1,1,1]
   1010b:      	vpand	%xmm4, %xmm0, %xmm0
   1010f:      	vmovd	%xmm0, %r11d
   10114:      	vinserti128	$0x1, 0x25(%rbx,%rdi), %ymm1, %ymm0
   1011c:      	andb	%r10b, %r11b
   1011f:      	andb	%r9b, %r11b
   10122:      	movzbl	%r11b, %r9d
   10126:      	vpshufb	%ymm5, %ymm0, %ymm0
   1012b:      	vpcmpeqd	%ymm0, %ymm2, %k0
   10131:      	vpmovzxbd	0x36(%rbx,%rdi), %xmm0
   10138:      	kmovd	%k0, %r10d
   1013c:      	vpxor	%xmm0, %xmm3, %xmm0
   10140:      	vpshufd	$0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
   10145:      	vpand	%xmm1, %xmm0, %xmm0
   10149:      	vpshufd	$0x55, %xmm0, %xmm1     # xmm1 = xmm0[1,1,1,1]
   1014e:      	vpand	%xmm1, %xmm0, %xmm0
   10152:      	vmovd	%xmm0, %edi
   10156:      	andb	%r8b, %dil
   10159:      	andb	%r10b, %dil
   1015c:      	movzbl	%dil, %edi
   10160:      	shll	$0x8, %edi
   10163:      	orq	%r9, %rdi
   10166:      	testq	%rsi, %rsi
   10169:      	je	0x101c2 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x242>
   1016b:      	testq	%rdi, %rdi
   1016e:      	jne	0x10000 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x80>
   10174:      	cmpb	$0x0, 0x3f(%rcx)
   10178:      	je	0x10014 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x94>
   1017e:      	movq	0x18(%rsp), %rdi
   10183:      	xorq	%rax, %rdx
   10186:      	movq	%rbp, %rsi
   10189:      	vzeroupper
   1018c:      	callq	0xe9c0 <ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>::read_overflow(unsigned long, unsigned long) const>
   10191:      	vmovdqa	0x8027(%rip), %ymm5     # 0x181c0 <syscall@GLIBC_2.2.5+0x181c0>
   10199:      	vpmovsxbd	0x7f2a(%rip), %xmm6 # 0x180cc <syscall@GLIBC_2.2.5+0x180cc>
   101a2:      	vpbroadcastd	0x7f19(%rip), %xmm7 # 0x180c4 <syscall@GLIBC_2.2.5+0x180c4>
   101ab:      	vpbroadcastd	0x7f14(%rip), %xmm8 # 0x180c8 <syscall@GLIBC_2.2.5+0x180c8>
   101b4:      	testb	$0x1, %dl
   101b7:      	jne	0x1000a <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x8a>
   101bd:      	jmp	0x10014 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x94>
   101c2:      	movzwl	0x3a(%rcx), %esi
   101c6:      	xorl	$0xffff, %esi           # imm = 0xFFFF
   101cc:      	andl	%esi, %edi
   101ce:      	testq	%rdi, %rdi
   101d1:      	jne	0x10000 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x80>
   101d7:      	jmp	0x10174 <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x1f4>
   101d9:      	movq	0x10(%rsp), %rax
   101de:      	movq	(%rsp), %rcx
   101e2:      	movq	%r15, 0x8(%rax)
   101e6:      	movq	%rcx, 0x10(%rax)
   101ea:      	jmp	0x101ef <Result hint_range<true, true, false, ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage>>(ikea::hashtable::table<50u, 21u, 8u, 29u, (ikea::hashtable::search_kernel)0, CountedStorage> const&, unsigned long const*, unsigned long const*, unsigned long const*)+0x26f>
   101ec:      	xorl	%r13d, %r13d
   101ef:      	movq	%r13, (%rax)
   101f2:      	addq	$0x28, %rsp
   101f6:      	popq	%rbx
   101f7:      	popq	%r12
   101f9:      	popq	%r13
   101fb:      	popq	%r14
   101fd:      	popq	%r15
   101ff:      	popq	%rbp
   10200:      	vzeroupper
   10203:      	retq
   10204:      	nopw	%cs:(%rax,%rax)
   1020e:      	nopw	%cs:(%rax,%rax)
   10218:      	nopw	%cs:(%rax,%rax)
   10222:      	nopw	%cs:(%rax,%rax)
   1022c:      	nopw	%cs:(%rax,%rax)
   10236:      	nopw	%cs:(%rax,%rax)
