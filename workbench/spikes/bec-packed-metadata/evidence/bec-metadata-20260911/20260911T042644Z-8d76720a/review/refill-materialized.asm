0000000000000000 <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%r13
       7:      	pushq	%r12
       9:      	pushq	%rbx
       a:      	subq	$0x28, %rsp
       e:      	movq	(%rdi), %rcx
      11:      	movq	0x40(%rdi), %rbx
      15:      	movl	%esi, %r14d
      18:      	movl	%esi, %esi
      1a:      	movl	$0x10, %ebp
      1f:      	movq	%rdi, %rax
      22:      	movq	%rdx, 0x8(%rsp)
      27:      	addq	$0x90, %rdi
      2e:      	movl	$0x8, %r8d
      34:      	movl	$0x10, %r15d
      3a:      	subq	%rsi, %rbx
      3d:      	movq	0x18(%rcx), %r12
      41:      	movl	0x14(%rcx), %r13d
      45:      	leaq	0x10(%rsp), %rcx
      4a:      	cmpq	$0x10, %rbx
      4e:      	cmovbq	%rbx, %rbp
      52:      	leaq	(%rbp,%rsi), %rdx
      57:      	callq	*0x100(%rax)
      5d:      	cmpq	$0xf, %rbx
      61:      	ja	0x75 <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x75>
      63:      	leaq	0x10(%rsp,%rbp), %rdi
      68:      	subq	%rbp, %r15
      6b:      	xorl	%esi, %esi
      6d:      	movq	%r15, %rdx
      70:      	callq	0x75 <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x75>
		0000000000000071:  R_X86_64_PLT32	memset-0x4
      75:      	shrl	$0x3, %r14d
      79:      	shrl	$0x3, %r13d
      7d:      	vpmovzxbw	0x10(%rsp), %ymm0
      84:      	vpbroadcastq	(%rip), %xmm3   # 0x8d <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x8d>
		0000000000000089:  R_X86_64_PC32	.LCPI15_4-0x4
      8d:      	leal	(%r14,%r14,8), %eax
      91:      	addq	%r12, %r13
      94:      	vpbroadcastw	(%r12,%r14), %ymm5
      9a:      	vpmovzxbw	(%rax,%r13), %ymm1
      a0:      	movzwl	0x10(%rax,%r13), %eax
      a6:      	vmovd	%eax, %xmm2
      aa:      	vpshufb	(%rip), %xmm2, %xmm2    # 0xb3 <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xb3>
		00000000000000af:  R_X86_64_PC32	.LCPI15_0-0x4
      b3:      	movq	0x8(%rsp), %rax
      b8:      	vpaddw	%ymm1, %ymm1, %ymm1
      bc:      	vgf2p8affineqb	$0x0, %xmm2, %xmm3, %xmm2
      c2:      	vpslldq	$0x2, %ymm0, %ymm3      # ymm3 = zero,zero,ymm0[0,1,2,3,4,5,6,7,8,9,10,11,12,13],zero,zero,ymm0[16,17,18,19,20,21,22,23,24,25,26,27,28,29]
      c7:      	vpaddw	%ymm0, %ymm3, %ymm3
      cb:      	vpsubw	%ymm0, %ymm5, %ymm0
      cf:      	vpslldq	$0x4, %ymm3, %ymm4      # ymm4 = zero,zero,zero,zero,ymm3[0,1,2,3,4,5,6,7,8,9,10,11],zero,zero,zero,zero,ymm3[16,17,18,19,20,21,22,23,24,25,26,27]
      d4:      	vpmovzxbw	%xmm2, %ymm2    # ymm2 = xmm2[0],zero,xmm2[1],zero,xmm2[2],zero,xmm2[3],zero,xmm2[4],zero,xmm2[5],zero,xmm2[6],zero,xmm2[7],zero,xmm2[8],zero,xmm2[9],zero,xmm2[10],zero,xmm2[11],zero,xmm2[12],zero,xmm2[13],zero,xmm2[14],zero,xmm2[15],zero
      d9:      	vpaddw	%ymm4, %ymm3, %ymm3
      dd:      	vpor	%ymm2, %ymm1, %ymm1
      e1:      	vmovdqa64	(%rip), %zmm2   # 0xeb <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xeb>
		00000000000000e7:  R_X86_64_PC32	.rodata+0x10fc
      eb:      	vpslldq	$0x8, %ymm3, %ymm4      # ymm4 = zero,zero,zero,zero,zero,zero,zero,zero,ymm3[0,1,2,3,4,5,6,7],zero,zero,zero,zero,zero,zero,zero,zero,ymm3[16,17,18,19,20,21,22,23]
      f0:      	vpaddw	%ymm4, %ymm3, %ymm3
      f4:      	vpshufb	(%rip), %xmm3, %xmm4    # 0xfd <void bec_metadata::refill_stored<(bec_metadata::Reader)2>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xfd>
		00000000000000f9:  R_X86_64_PC32	.LCPI15_2-0x4
      fd:      	vpaddw	%ymm3, %ymm0, %ymm0
     101:      	vpermq	$0x50, %ymm4, %ymm4     # ymm4 = ymm4[0,0,1,1]
     107:      	vpaddw	%ymm4, %ymm0, %ymm0
     10b:      	vpermi2w	%zmm0, %zmm1, %zmm2
     111:      	vmovdqu64	%zmm2, (%rax)
     117:      	addq	$0x28, %rsp
     11b:      	popq	%rbx
     11c:      	popq	%r12
     11e:      	popq	%r13
     120:      	popq	%r14
     122:      	popq	%r15
     124:      	popq	%rbp
     125:      	vzeroupper
     128:      	retq

Disassembly of section .text._ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE24_M_release_last_use_coldEv:

