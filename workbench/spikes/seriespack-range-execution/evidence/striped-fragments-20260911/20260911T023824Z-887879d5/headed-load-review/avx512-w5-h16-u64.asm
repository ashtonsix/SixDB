
/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/build/maintenance/striped-initial-heads/native.o:	file format elf64-x86-64

Disassembly of section .text._ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_:

0000000000000000 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%rbx
       6:      	movq	0x10(%rdi), %r8
       a:      	movq	0x28(%rdi), %r9
       e:      	movzbl	%sil, %eax
      12:      	subl	%esi, %edx
      14:      	shrq	$0x8, %rsi
      18:      	vpbroadcastq	(%rip), %xmm0   # 0x21 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x21>
		000000000000001d:  R_X86_64_PC32	.LCPI2490_0-0x4
      21:      	movl	$0xffffffff, %r10d      # imm = 0xFFFFFFFF
      27:      	addl	%eax, %edx
      29:      	imulq	%rsi, %r8
      2d:      	imulq	%rsi, %r9
      31:      	imulq	0x40(%rdi), %rsi
      36:      	addq	(%rdi), %r8
      39:      	addq	0x18(%rdi), %r9
      3d:      	addq	0x30(%rdi), %rsi
      41:      	movl	$0x10, %edi
      46:      	jmp	0xc2 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xc2>
      48:      	nopl	(%rax,%rax)
      50:      	vpmovzxbq	(%r9,%rbx), %zmm3
      57:      	vpmovzxbq	(%rsi,%rbx), %zmm4
      5e:      	vpshufd	$0xee, %xmm1, %xmm1     # xmm1 = xmm1[2,3,2,3]
      63:      	vpmovzxbq	%xmm1, %zmm1    # zmm1 = xmm1[0],zero,zero,zero,zero,zero,zero,zero,xmm1[1],zero,zero,zero,zero,zero,zero,zero,xmm1[2],zero,zero,zero,zero,zero,zero,zero,xmm1[3],zero,zero,zero,zero,zero,zero,zero,xmm1[4],zero,zero,zero,zero,zero,zero,zero,xmm1[5],zero,zero,zero,zero,zero,zero,zero,xmm1[6],zero,zero,zero,zero,zero,zero,zero,xmm1[7],zero,zero,zero,zero,zero,zero,zero
      69:      	vpsllq	$0xd, %zmm3, %zmm3
      70:      	vpsllq	$0x5, %zmm4, %zmm4
      77:      	vpternlogq	$0xfe, %zmm2, %zmm3, %zmm4 # zmm4 = zmm4 | zmm3 | zmm2
      7e:      	vmovdqu64	%zmm4, (%rcx)
      84:      	vpmovzxbq	0x8(%r9,%rbx), %zmm2
      8c:      	vpmovzxbq	0x8(%rsi,%rbx), %zmm3
      94:      	vpsllq	$0xd, %zmm2, %zmm2
      9b:      	vpsllq	$0x5, %zmm3, %zmm3
      a2:      	vpternlogq	$0xfe, %zmm1, %zmm2, %zmm3 # zmm3 = zmm3 | zmm2 | zmm1
      a9:      	vmovdqu64	%zmm3, 0x40(%rcx)
      b0:      	addl	%r11d, %eax
      b3:      	movl	%r11d, %r11d
      b6:      	leaq	(%rcx,%r11,8), %rcx
      ba:      	cmpl	%edx, %eax
      bc:      	je	0x24e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x24e>
      c2:      	movl	%eax, %ebx
      c4:      	movl	%edx, %r11d
      c7:      	andl	$0x1f, %ebx
      ca:      	movl	$0x20, %ebp
      cf:      	subl	%eax, %r11d
      d2:      	movl	%eax, %r14d
      d5:      	subl	%ebx, %ebp
      d7:      	cmpl	$0x10, %r11d
      db:      	cmovael	%edi, %r11d
      df:      	cmpl	$0x10, %ebx
      e2:      	cmovael	%edi, %ebx
      e5:      	cmpl	%r11d, %ebp
      e8:      	cmovbl	%ebp, %r11d
      ec:      	shrl	$0x5, %r14d
      f0:      	leal	(%r14,%r14,4), %ebp
      f4:      	shll	$0x2, %r14d
      f8:      	leal	(%r14,%r14,4), %r14d
      fc:      	andl	$0x7, %ebp
      ff:      	andl	$-0x20, %r14d
     103:      	addq	%r8, %r14
     106:      	vmovdqu	(%rbx,%r14), %xmm1
     10c:      	cmpl	$0x3, %ebp
     10f:      	ja	0x120 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x120>
     111:      	vmovd	%ebp, %xmm2
     115:      	vpsrlw	%xmm2, %xmm1, %xmm1
     119:      	vpand	%xmm0, %xmm1, %xmm1
     11d:      	jmp	0x14c <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x14c>
     11f:      	nop
     120:      	addb	$-0x3, %bpl
     124:      	vpsrlw	$0x3, %xmm1, %xmm2
     129:      	shlxl	%ebp, %r10d, %ebp
     12e:      	vpbroadcastb	%ebp, %xmm1
     134:      	vpandn	0x20(%rbx,%r14), %xmm1, %xmm1
     13b:      	andb	$0x1f, %bpl
     13f:      	vpbroadcastb	%ebp, %xmm3
     145:      	vpternlogq	$0xf8, %xmm3, %xmm2, %xmm1 # xmm1 = xmm1 | (xmm2 & xmm3)
     14c:      	vpmovzxbq	%xmm1, %zmm2    # zmm2 = xmm1[0],zero,zero,zero,zero,zero,zero,zero,xmm1[1],zero,zero,zero,zero,zero,zero,zero,xmm1[2],zero,zero,zero,zero,zero,zero,zero,xmm1[3],zero,zero,zero,zero,zero,zero,zero,xmm1[4],zero,zero,zero,zero,zero,zero,zero,xmm1[5],zero,zero,zero,zero,zero,zero,zero,xmm1[6],zero,zero,zero,zero,zero,zero,zero,xmm1[7],zero,zero,zero,zero,zero,zero,zero
     152:      	movl	%eax, %ebp
     154:      	andl	$-0x20, %ebp
     157:      	orl	%ebp, %ebx
     159:      	cmpl	$0x10, %r11d
     15d:      	je	0x50 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x50>
     163:      	movl	%eax, %r14d
     166:      	subl	%ebx, %r14d
     169:      	movl	$0x8, %r15d
     16f:      	leal	(%r11,%r14), %ebp
     173:      	cmpl	$0x8, %ebp
     176:      	cmovbl	%ebp, %r15d
     17a:      	subl	%r14d, %r15d
     17d:      	jbe	0x1c8 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1c8>
     17f:      	vpmovzxbq	(%r9,%rbx), %zmm3
     186:      	vpmovzxbq	(%rsi,%rbx), %zmm4
     18d:      	vpsllq	$0xd, %zmm3, %zmm5
     194:      	vpsllq	$0x5, %zmm4, %zmm3
     19b:      	vpternlogq	$0xfe, %zmm2, %zmm5, %zmm3 # zmm3 = zmm3 | zmm5 | zmm2
     1a2:      	cmpl	$0x8, %r15d
     1a6:      	jne	0x1b0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1b0>
     1a8:      	vmovdqu64	%zmm3, (%rcx)
     1ae:      	jmp	0x1c8 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1c8>
     1b0:      	shlxl	%r15d, %r10d, %r15d
     1b5:      	notl	%r15d
     1b8:      	shlxl	%r14d, %r15d, %r15d
     1bd:      	kmovd	%r15d, %k1
     1c2:      	vpcompressq	%zmm3, (%rcx) {%k1}
     1c8:      	cmpl	$0x9, %r14d
     1cc:      	movl	$0x8, %r15d
     1d2:      	cmovael	%r14d, %r15d
     1d6:      	cmpl	$0x10, %ebp
     1d9:      	cmovael	%edi, %ebp
     1dc:      	subl	%r15d, %ebp
     1df:      	jbe	0xb0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xb0>
     1e5:      	vpshufd	$0xee, %xmm1, %xmm1     # xmm1 = xmm1[2,3,2,3]
     1ea:      	vpmovzxbq	0x8(%rsi,%rbx), %zmm3
     1f2:      	vpmovzxbq	%xmm1, %zmm2    # zmm2 = xmm1[0],zero,zero,zero,zero,zero,zero,zero,xmm1[1],zero,zero,zero,zero,zero,zero,zero,xmm1[2],zero,zero,zero,zero,zero,zero,zero,xmm1[3],zero,zero,zero,zero,zero,zero,zero,xmm1[4],zero,zero,zero,zero,zero,zero,zero,xmm1[5],zero,zero,zero,zero,zero,zero,zero,xmm1[6],zero,zero,zero,zero,zero,zero,zero,xmm1[7],zero,zero,zero,zero,zero,zero,zero
     1f8:      	vpmovzxbq	0x8(%r9,%rbx), %zmm1
     200:      	movl	%r15d, %ebx
     203:      	subl	%r14d, %ebx
     206:      	leaq	(%rcx,%rbx,8), %rbx
     20a:      	vpsllq	$0xd, %zmm1, %zmm4
     211:      	vpsllq	$0x5, %zmm3, %zmm1
     218:      	vpternlogq	$0xfe, %zmm2, %zmm4, %zmm1 # zmm1 = zmm1 | zmm4 | zmm2
     21f:      	cmpl	$0x8, %ebp
     222:      	jne	0x22f <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x22f>
     224:      	vmovdqu64	%zmm1, (%rbx)
     22a:      	jmp	0xb0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xb0>
     22f:      	shlxl	%ebp, %r10d, %ebp
     234:      	addb	$-0x8, %r15b
     238:      	notl	%ebp
     23a:      	shlxl	%r15d, %ebp, %ebp
     23f:      	kmovd	%ebp, %k1
     243:      	vpcompressq	%zmm1, (%rbx) {%k1}
     249:      	jmp	0xb0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xb0>
     24e:      	popq	%rbx
     24f:      	popq	%r14
     251:      	popq	%r15
     253:      	popq	%rbp
     254:      	vzeroupper
     257:      	retq
