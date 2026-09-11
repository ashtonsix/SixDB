0000000000000000 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)>:
       0:      	movl	%esi, %r8d
       3:      	shrl	$0x7, %r8d
       7:      	imulq	0x58(%rdi), %r8
       c:      	movl	%esi, %r10d
       f:      	shrl	$0x5, %r10d
      13:      	movq	(%rdi), %rcx
      16:      	movl	%r10d, %r9d
      19:      	andl	$0x2, %r9d
      1d:      	incl	%r10d
      20:      	movl	%r9d, %r11d
      23:      	shll	$0x5, %r11d
      27:      	addq	0x48(%rdi), %r8
      2b:      	movl	%esi, %edi
      2d:      	andl	$0x10, %edi
      30:      	movq	0x18(%rcx), %rax
      34:      	movl	0x14(%rcx), %ecx
      37:      	addq	%r8, %r11
      3a:      	vmovdqu	(%rdi,%r11), %xmm0
      40:      	testb	$0x2, %r10b
      44:      	jne	0x52 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x52>
      46:      	vpandq	(%rip){1to2}, %xmm0, %xmm0 # 0x50 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x50>
		000000000000004c:  R_X86_64_PC32	.LCPI14_2-0x4
      50:      	jmp	0x81 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x81>
      52:      	vmovdqu	0x20(%r8,%rdi), %xmm1
      59:      	vpsrlw	$0x2, %xmm0, %xmm2
      5e:      	vmovd	%r9d, %xmm0
      63:      	vpslld	$0x1, %xmm0, %xmm0
      68:      	vpsrlw	%xmm0, %xmm1, %xmm0
      6c:      	vpandq	(%rip){1to2}, %xmm0, %xmm0 # 0x76 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x76>
		0000000000000072:  R_X86_64_PC32	.LCPI14_0-0x4
      76:      	vpternlogq	$0xf8, (%rip){1to2}, %xmm2, %xmm0 # xmm0 = xmm0 | (xmm2 & m64bcst)
                                                # 0x81 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x81>
		000000000000007c:  R_X86_64_PC32	.LCPI14_1-0x5
      81:      	shrl	$0x3, %esi
      84:      	shrl	$0x3, %ecx
      87:      	vpbroadcastq	(%rip), %xmm3   # 0x90 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x90>
		000000000000008c:  R_X86_64_PC32	.LCPI14_7-0x4
      90:      	vpmovzxbw	%xmm0, %ymm0    # ymm0 = xmm0[0],zero,xmm0[1],zero,xmm0[2],zero,xmm0[3],zero,xmm0[4],zero,xmm0[5],zero,xmm0[6],zero,xmm0[7],zero,xmm0[8],zero,xmm0[9],zero,xmm0[10],zero,xmm0[11],zero,xmm0[12],zero,xmm0[13],zero,xmm0[14],zero,xmm0[15],zero
      95:      	leal	(%rsi,%rsi,8), %edi
      98:      	addq	%rax, %rcx
      9b:      	vpbroadcastw	(%rax,%rsi), %ymm5
      a1:      	vpmovzxbw	(%rdi,%rcx), %ymm1
      a7:      	movzwl	0x10(%rdi,%rcx), %ecx
      ac:      	vmovd	%ecx, %xmm2
      b0:      	vpshufb	(%rip), %xmm2, %xmm2    # 0xb9 <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xb9>
		00000000000000b5:  R_X86_64_PC32	.LCPI14_3-0x4
      b9:      	vpaddw	%ymm1, %ymm1, %ymm1
      bd:      	vgf2p8affineqb	$0x0, %xmm2, %xmm3, %xmm2
      c3:      	vpslldq	$0x2, %ymm0, %ymm3      # ymm3 = zero,zero,ymm0[0,1,2,3,4,5,6,7,8,9,10,11,12,13],zero,zero,ymm0[16,17,18,19,20,21,22,23,24,25,26,27,28,29]
      c8:      	vpaddw	%ymm0, %ymm3, %ymm3
      cc:      	vpsubw	%ymm0, %ymm5, %ymm0
      d0:      	vpslldq	$0x4, %ymm3, %ymm4      # ymm4 = zero,zero,zero,zero,ymm3[0,1,2,3,4,5,6,7,8,9,10,11],zero,zero,zero,zero,ymm3[16,17,18,19,20,21,22,23,24,25,26,27]
      d5:      	vpmovzxbw	%xmm2, %ymm2    # ymm2 = xmm2[0],zero,xmm2[1],zero,xmm2[2],zero,xmm2[3],zero,xmm2[4],zero,xmm2[5],zero,xmm2[6],zero,xmm2[7],zero,xmm2[8],zero,xmm2[9],zero,xmm2[10],zero,xmm2[11],zero,xmm2[12],zero,xmm2[13],zero,xmm2[14],zero,xmm2[15],zero
      da:      	vpaddw	%ymm4, %ymm3, %ymm3
      de:      	vpor	%ymm2, %ymm1, %ymm1
      e2:      	vmovdqa64	(%rip), %zmm2   # 0xec <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xec>
		00000000000000e8:  R_X86_64_PC32	.rodata+0x10bc
      ec:      	vpslldq	$0x8, %ymm3, %ymm4      # ymm4 = zero,zero,zero,zero,zero,zero,zero,zero,ymm3[0,1,2,3,4,5,6,7],zero,zero,zero,zero,zero,zero,zero,zero,ymm3[16,17,18,19,20,21,22,23]
      f1:      	vpaddw	%ymm4, %ymm3, %ymm3
      f5:      	vpshufb	(%rip), %xmm3, %xmm4    # 0xfe <void bec_metadata::refill_stored<(bec_metadata::Reader)1>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xfe>
		00000000000000fa:  R_X86_64_PC32	.LCPI14_5-0x4
      fe:      	vpaddw	%ymm3, %ymm0, %ymm0
     102:      	vpermq	$0x50, %ymm4, %ymm4     # ymm4 = ymm4[0,0,1,1]
     108:      	vpaddw	%ymm4, %ymm0, %ymm0
     10c:      	vpermi2w	%zmm0, %zmm1, %zmm2
     112:      	vmovdqu64	%zmm2, (%rdx)
     118:      	vzeroupper
     11b:      	retq

Disassembly of section .text._ZN12bec_metadata13refill_storedILNS_6ReaderE2EEEvRKNS_6SourceEjPj:

