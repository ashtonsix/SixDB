
/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/build/maintenance/striped-initial-heads/native.o:	file format elf64-x86-64

Disassembly of section .text._ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_:

0000000000000000 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%rbx
       6:      	movq	0x10(%rdi), %r8
       a:      	movq	0x28(%rdi), %r9
       e:      	movq	%rsi, %rax
      11:      	shrq	$0x6, %rax
      15:      	vpbroadcastq	(%rip), %xmm0   # 0x1e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1e>
		000000000000001a:  R_X86_64_PC32	.LCPI2598_0-0x4
      1e:      	subl	%esi, %edx
      20:      	andl	$0x3f, %esi
      23:      	movl	$0xffffffff, %r10d      # imm = 0xFFFFFFFF
      29:      	addl	%esi, %edx
      2b:      	imulq	%rax, %r8
      2f:      	imulq	%rax, %r9
      33:      	imulq	0x40(%rdi), %rax
      38:      	addq	(%rdi), %r8
      3b:      	addq	0x18(%rdi), %r9
      3f:      	addq	0x30(%rdi), %rax
      43:      	movl	$0x10, %edi
      48:      	jmp	0xeb <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xeb>
      4d:      	nopl	(%rax)
      50:      	vpmovzxbq	(%r9,%rbx), %zmm4
      57:      	vpmovzxbq	(%rax,%rbx), %zmm5
      5e:      	vpmovzxbq	(%r8,%rbx), %zmm3
      65:      	vpshufd	$0xee, %xmm1, %xmm1     # xmm1 = xmm1[2,3,2,3]
      6a:      	vpmovzxbq	%xmm1, %zmm1    # zmm1 = xmm1[0],zero,zero,zero,zero,zero,zero,zero,xmm1[1],zero,zero,zero,zero,zero,zero,zero,xmm1[2],zero,zero,zero,zero,zero,zero,zero,xmm1[3],zero,zero,zero,zero,zero,zero,zero,xmm1[4],zero,zero,zero,zero,zero,zero,zero,xmm1[5],zero,zero,zero,zero,zero,zero,zero,xmm1[6],zero,zero,zero,zero,zero,zero,zero,xmm1[7],zero,zero,zero,zero,zero,zero,zero
      70:      	vpsllq	$0x14, %zmm4, %zmm4
      77:      	vpsllq	$0xc, %zmm5, %zmm5
      7e:      	vpsllq	$0x4, %zmm3, %zmm3
      85:      	vporq	%zmm4, %zmm5, %zmm4
      8b:      	vpternlogq	$0xfe, %zmm2, %zmm3, %zmm4 # zmm4 = zmm4 | zmm3 | zmm2
      92:      	vmovdqu64	%zmm4, (%rcx)
      98:      	vpmovzxbq	0x8(%r9,%rbx), %zmm3
      a0:      	vpmovzxbq	0x8(%rax,%rbx), %zmm4
      a8:      	vpmovzxbq	0x8(%r8,%rbx), %zmm2
      b0:      	vpsllq	$0x14, %zmm3, %zmm3
      b7:      	vpsllq	$0xc, %zmm4, %zmm4
      be:      	vpsllq	$0x4, %zmm2, %zmm2
      c5:      	vporq	%zmm3, %zmm4, %zmm3
      cb:      	vpternlogq	$0xfe, %zmm1, %zmm2, %zmm3 # zmm3 = zmm3 | zmm2 | zmm1
      d2:      	vmovdqu64	%zmm3, 0x40(%rcx)
      d9:      	addl	%r11d, %esi
      dc:      	movl	%r11d, %r11d
      df:      	leaq	(%rcx,%r11,8), %rcx
      e3:      	cmpl	%edx, %esi
      e5:      	je	0x25e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x25e>
      eb:      	movl	%esi, %r14d
      ee:      	movl	%edx, %r11d
      f1:      	andl	$0x1f, %r14d
      f5:      	movl	$0x20, %ebp
      fa:      	subl	%esi, %r11d
      fd:      	movl	%esi, %ebx
      ff:      	subl	%r14d, %ebp
     102:      	cmpl	$0x10, %r11d
     106:      	cmovael	%edi, %r11d
     10a:      	andl	$-0x20, %ebx
     10d:      	cmpl	$0x10, %r14d
     111:      	cmovael	%edi, %r14d
     115:      	vmovdqu	0x40(%r8,%r14), %xmm1
     11c:      	orl	%r14d, %ebx
     11f:      	cmpl	%r11d, %ebp
     122:      	cmovbl	%ebp, %r11d
     126:      	movl	%esi, %ebp
     128:      	shrl	$0x3, %ebp
     12b:      	andl	$-0x4, %ebp
     12e:      	vmovd	%ebp, %xmm2
     132:      	vpsrlw	%xmm2, %xmm1, %xmm1
     136:      	vpand	%xmm0, %xmm1, %xmm1
     13a:      	vpmovzxbq	%xmm1, %zmm2    # zmm2 = xmm1[0],zero,zero,zero,zero,zero,zero,zero,xmm1[1],zero,zero,zero,zero,zero,zero,zero,xmm1[2],zero,zero,zero,zero,zero,zero,zero,xmm1[3],zero,zero,zero,zero,zero,zero,zero,xmm1[4],zero,zero,zero,zero,zero,zero,zero,xmm1[5],zero,zero,zero,zero,zero,zero,zero,xmm1[6],zero,zero,zero,zero,zero,zero,zero,xmm1[7],zero,zero,zero,zero,zero,zero,zero
     140:      	cmpl	$0x10, %r11d
     144:      	je	0x50 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x50>
     14a:      	movl	%esi, %r14d
     14d:      	subl	%ebx, %r14d
     150:      	movl	$0x8, %r15d
     156:      	leal	(%r11,%r14), %ebp
     15a:      	cmpl	$0x8, %ebp
     15d:      	cmovbl	%ebp, %r15d
     161:      	subl	%r14d, %r15d
     164:      	jbe	0x1c3 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1c3>
     166:      	vpmovzxbq	(%r8,%rbx), %zmm3
     16d:      	vpmovzxbq	(%rax,%rbx), %zmm5
     174:      	vpsllq	$0x4, %zmm3, %zmm4
     17b:      	vpmovzxbq	(%r9,%rbx), %zmm3
     182:      	vpsllq	$0xc, %zmm5, %zmm5
     189:      	vpsllq	$0x14, %zmm3, %zmm3
     190:      	vporq	%zmm3, %zmm5, %zmm3
     196:      	vpternlogq	$0xfe, %zmm2, %zmm4, %zmm3 # zmm3 = zmm3 | zmm4 | zmm2
     19d:      	cmpl	$0x8, %r15d
     1a1:      	jne	0x1ab <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1ab>
     1a3:      	vmovdqu64	%zmm3, (%rcx)
     1a9:      	jmp	0x1c3 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1c3>
     1ab:      	shlxl	%r15d, %r10d, %r15d
     1b0:      	notl	%r15d
     1b3:      	shlxl	%r14d, %r15d, %r15d
     1b8:      	kmovd	%r15d, %k1
     1bd:      	vpcompressq	%zmm3, (%rcx) {%k1}
     1c3:      	cmpl	$0x9, %r14d
     1c7:      	movl	$0x8, %r15d
     1cd:      	cmovael	%r14d, %r15d
     1d1:      	cmpl	$0x10, %ebp
     1d4:      	cmovael	%edi, %ebp
     1d7:      	subl	%r15d, %ebp
     1da:      	jbe	0xd9 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xd9>
     1e0:      	vpshufd	$0xee, %xmm1, %xmm1     # xmm1 = xmm1[2,3,2,3]
     1e5:      	vpmovzxbq	0x8(%rax,%rbx), %zmm4
     1ed:      	vpmovzxbq	%xmm1, %zmm2    # zmm2 = xmm1[0],zero,zero,zero,zero,zero,zero,zero,xmm1[1],zero,zero,zero,zero,zero,zero,zero,xmm1[2],zero,zero,zero,zero,zero,zero,zero,xmm1[3],zero,zero,zero,zero,zero,zero,zero,xmm1[4],zero,zero,zero,zero,zero,zero,zero,xmm1[5],zero,zero,zero,zero,zero,zero,zero,xmm1[6],zero,zero,zero,zero,zero,zero,zero,xmm1[7],zero,zero,zero,zero,zero,zero,zero
     1f3:      	vpmovzxbq	0x8(%rbx,%r8), %zmm1
     1fb:      	vpsllq	$0xc, %zmm4, %zmm4
     202:      	vpsllq	$0x4, %zmm1, %zmm3
     209:      	vpmovzxbq	0x8(%r9,%rbx), %zmm1
     211:      	movl	%r15d, %ebx
     214:      	subl	%r14d, %ebx
     217:      	leaq	(%rcx,%rbx,8), %rbx
     21b:      	vpsllq	$0x14, %zmm1, %zmm1
     222:      	vporq	%zmm1, %zmm4, %zmm1
     228:      	vpternlogq	$0xfe, %zmm2, %zmm3, %zmm1 # zmm1 = zmm1 | zmm3 | zmm2
     22f:      	cmpl	$0x8, %ebp
     232:      	jne	0x23f <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x23f>
     234:      	vmovdqu64	%zmm1, (%rbx)
     23a:      	jmp	0xd9 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xd9>
     23f:      	shlxl	%ebp, %r10d, %ebp
     244:      	addb	$-0x8, %r15b
     248:      	notl	%ebp
     24a:      	shlxl	%r15d, %ebp, %ebp
     24f:      	kmovd	%ebp, %k1
     253:      	vpcompressq	%zmm1, (%rbx) {%k1}
     259:      	jmp	0xd9 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xd9>
     25e:      	popq	%rbx
     25f:      	popq	%r14
     261:      	popq	%r15
     263:      	popq	%rbp
     264:      	vzeroupper
     267:      	retq
