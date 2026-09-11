0000000000000000 <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)>:
       0:      	movq	(%rdi), %rcx
       3:      	movl	0x14(%rcx), %edi
       6:      	movq	0x18(%rcx), %rax
       a:      	movl	%esi, %ecx
       c:      	shrl	$0x3, %ecx
       f:      	leal	(%rcx,%rcx,8), %r9d
      13:      	movl	%edi, %r8d
      16:      	shrl	$0x3, %r8d
      1a:      	addq	%rdi, %rdi
      1d:      	addq	%rax, %r8
      20:      	leaq	(%rdi,%rdi,4), %r10
      24:      	vpmovzxbw	(%r9,%r8), %ymm0
      2a:      	movzwl	0x10(%r9,%r8), %edi
      30:      	movl	%esi, %r8d
      33:      	shrl	$0x7, %r8d
      37:      	shll	$0x5, %r8d
      3b:      	shrq	$0x3, %r10
      3f:      	leal	(%r8,%r8,2), %r9d
      43:      	movl	%esi, %r8d
      46:      	andl	$0x10, %r8d
      4a:      	shrl	$0x5, %esi
      4d:      	orl	%r9d, %r8d
      50:      	movl	%esi, %r9d
      53:      	andl	$0x2, %r9d
      57:      	incl	%esi
      59:      	addq	%rax, %r8
      5c:      	addq	%r10, %r8
      5f:      	movl	%r9d, %r10d
      62:      	shll	$0x5, %r10d
      66:      	vmovdqu	(%r10,%r8), %xmm1
      6c:      	testb	$0x2, %sil
      70:      	jne	0x7e <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x7e>
      72:      	vpandq	(%rip){1to2}, %xmm1, %xmm1 # 0x7c <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x7c>
		0000000000000078:  R_X86_64_PC32	.LCPI13_2-0x4
      7c:      	jmp	0xac <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xac>
      7e:      	vpsrlw	$0x2, %xmm1, %xmm2
      83:      	vmovdqu	0x20(%r8), %xmm1
      89:      	vmovd	%r9d, %xmm3
      8e:      	vpslld	$0x1, %xmm3, %xmm3
      93:      	vpsrlw	%xmm3, %xmm1, %xmm1
      97:      	vpandq	(%rip){1to2}, %xmm1, %xmm1 # 0xa1 <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xa1>
		000000000000009d:  R_X86_64_PC32	.LCPI13_0-0x4
      a1:      	vpternlogq	$0xf8, (%rip){1to2}, %xmm2, %xmm1 # xmm1 = xmm1 | (xmm2 & m64bcst)
                                                # 0xac <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xac>
		00000000000000a7:  R_X86_64_PC32	.LCPI13_1-0x5
      ac:      	vmovd	%edi, %xmm2
      b0:      	vpshufb	(%rip), %xmm2, %xmm2    # 0xb9 <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xb9>
		00000000000000b5:  R_X86_64_PC32	.LCPI13_3-0x4
      b9:      	vpbroadcastq	(%rip), %xmm3   # 0xc2 <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0xc2>
		00000000000000be:  R_X86_64_PC32	.LCPI13_7-0x4
      c2:      	vpmovzxbw	%xmm1, %ymm1    # ymm1 = xmm1[0],zero,xmm1[1],zero,xmm1[2],zero,xmm1[3],zero,xmm1[4],zero,xmm1[5],zero,xmm1[6],zero,xmm1[7],zero,xmm1[8],zero,xmm1[9],zero,xmm1[10],zero,xmm1[11],zero,xmm1[12],zero,xmm1[13],zero,xmm1[14],zero,xmm1[15],zero
      c7:      	vpbroadcastw	(%rax,%rcx), %ymm5
      cd:      	vpaddw	%ymm0, %ymm0, %ymm0
      d1:      	vgf2p8affineqb	$0x0, %xmm2, %xmm3, %xmm2
      d7:      	vpslldq	$0x2, %ymm1, %ymm3      # ymm3 = zero,zero,ymm1[0,1,2,3,4,5,6,7,8,9,10,11,12,13],zero,zero,ymm1[16,17,18,19,20,21,22,23,24,25,26,27,28,29]
      dc:      	vpaddw	%ymm1, %ymm3, %ymm3
      e0:      	vpsubw	%ymm1, %ymm5, %ymm1
      e4:      	vpslldq	$0x4, %ymm3, %ymm4      # ymm4 = zero,zero,zero,zero,ymm3[0,1,2,3,4,5,6,7,8,9,10,11],zero,zero,zero,zero,ymm3[16,17,18,19,20,21,22,23,24,25,26,27]
      e9:      	vpmovzxbw	%xmm2, %ymm2    # ymm2 = xmm2[0],zero,xmm2[1],zero,xmm2[2],zero,xmm2[3],zero,xmm2[4],zero,xmm2[5],zero,xmm2[6],zero,xmm2[7],zero,xmm2[8],zero,xmm2[9],zero,xmm2[10],zero,xmm2[11],zero,xmm2[12],zero,xmm2[13],zero,xmm2[14],zero,xmm2[15],zero
      ee:      	vpaddw	%ymm4, %ymm3, %ymm3
      f2:      	vpor	%ymm2, %ymm0, %ymm0
      f6:      	vmovdqa64	(%rip), %zmm2   # 0x100 <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x100>
		00000000000000fc:  R_X86_64_PC32	.rodata+0x107c
     100:      	vpslldq	$0x8, %ymm3, %ymm4      # ymm4 = zero,zero,zero,zero,zero,zero,zero,zero,ymm3[0,1,2,3,4,5,6,7],zero,zero,zero,zero,zero,zero,zero,zero,ymm3[16,17,18,19,20,21,22,23]
     105:      	vpaddw	%ymm4, %ymm3, %ymm3
     109:      	vpshufb	(%rip), %xmm3, %xmm4    # 0x112 <void bec_metadata::refill_stored<(bec_metadata::Reader)0>(bec_metadata::Source const&, unsigned int, unsigned int*)+0x112>
		000000000000010e:  R_X86_64_PC32	.LCPI13_5-0x4
     112:      	vpaddw	%ymm3, %ymm1, %ymm1
     116:      	vpermq	$0x50, %ymm4, %ymm4     # ymm4 = ymm4[0,0,1,1]
     11c:      	vpaddw	%ymm4, %ymm1, %ymm1
     120:      	vpermi2w	%zmm1, %zmm0, %zmm2
     126:      	vmovdqu64	%zmm2, (%rdx)
     12c:      	vzeroupper
     12f:      	retq

Disassembly of section .text._ZN12bec_metadata13refill_storedILNS_6ReaderE1EEEvRKNS_6SourceEjPj:

