
/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/build/maintenance/striped-initial-heads/native.o:	file format elf64-x86-64

Disassembly of section .text._ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_:

0000000000000000 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%rbx
       6:      	movq	0x10(%rdi), %r8
       a:      	movq	0x28(%rdi), %r9
       e:      	movq	%rsi, %rax
      11:      	shrq	$0x7, %rax
      15:      	vpbroadcastq	(%rip), %xmm0   # 0x1e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1e>
		000000000000001a:  R_X86_64_PC32	.LCPI2520_2-0x4
      1e:      	vpbroadcastq	(%rip), %xmm1   # 0x27 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x27>
		0000000000000023:  R_X86_64_PC32	.LCPI2520_0-0x4
      27:      	vpbroadcastq	(%rip), %xmm2   # 0x30 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x30>
		000000000000002c:  R_X86_64_PC32	.LCPI2520_1-0x4
      30:      	subl	%esi, %edx
      32:      	andl	$0x7f, %esi
      35:      	movl	$0xffffffff, %r10d      # imm = 0xFFFFFFFF
      3b:      	addl	%esi, %edx
      3d:      	imulq	%rax, %r8
      41:      	imulq	%rax, %r9
      45:      	imulq	0x40(%rdi), %rax
      4a:      	addq	(%rdi), %r8
      4d:      	addq	0x18(%rdi), %r9
      51:      	addq	0x30(%rdi), %rax
      55:      	movl	$0x10, %edi
      5a:      	jmp	0xd2 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xd2>
      5c:      	nopl	(%rax)
      60:      	vpmovzxbq	(%r9,%rbx), %zmm5
      67:      	vpmovzxbq	(%rax,%rbx), %zmm6
      6e:      	vpshufd	$0xee, %xmm3, %xmm3     # xmm3 = xmm3[2,3,2,3]
      73:      	vpmovzxbq	%xmm3, %zmm3    # zmm3 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero,xmm3[4],zero,zero,zero,zero,zero,zero,zero,xmm3[5],zero,zero,zero,zero,zero,zero,zero,xmm3[6],zero,zero,zero,zero,zero,zero,zero,xmm3[7],zero,zero,zero,zero,zero,zero,zero
      79:      	vpsllq	$0xe, %zmm5, %zmm5
      80:      	vpsllq	$0x6, %zmm6, %zmm6
      87:      	vpternlogq	$0xfe, %zmm4, %zmm5, %zmm6 # zmm6 = zmm6 | zmm5 | zmm4
      8e:      	vmovdqu64	%zmm6, (%rcx)
      94:      	vpmovzxbq	0x8(%r9,%rbx), %zmm4
      9c:      	vpmovzxbq	0x8(%rax,%rbx), %zmm5
      a4:      	vpsllq	$0xe, %zmm4, %zmm4
      ab:      	vpsllq	$0x6, %zmm5, %zmm5
      b2:      	vpternlogq	$0xfe, %zmm3, %zmm4, %zmm5 # zmm5 = zmm5 | zmm4 | zmm3
      b9:      	vmovdqu64	%zmm5, 0x40(%rcx)
      c0:      	addl	%r11d, %esi
      c3:      	movl	%r11d, %r11d
      c6:      	leaq	(%rcx,%r11,8), %rcx
      ca:      	cmpl	%edx, %esi
      cc:      	je	0x256 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x256>
      d2:      	movl	%esi, %ebx
      d4:      	movl	%edx, %r11d
      d7:      	andl	$0x1f, %ebx
      da:      	movl	$0x20, %ebp
      df:      	subl	%esi, %r11d
      e2:      	movl	%esi, %r14d
      e5:      	subl	%ebx, %ebp
      e7:      	cmpl	$0x10, %r11d
      eb:      	cmovael	%edi, %r11d
      ef:      	cmpl	$0x10, %ebx
      f2:      	cmovael	%edi, %ebx
      f5:      	cmpl	%r11d, %ebp
      f8:      	cmovbl	%ebp, %r11d
      fc:      	shrl	$0x5, %r14d
     100:      	movl	%r14d, %ebp
     103:      	andl	$0x2, %ebp
     106:      	incl	%r14d
     109:      	movl	%ebp, %r15d
     10c:      	shll	$0x5, %r15d
     110:      	addq	%r8, %r15
     113:      	vmovdqu	(%rbx,%r15), %xmm3
     119:      	testb	$0x2, %r14b
     11d:      	jne	0x130 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x130>
     11f:      	vpand	%xmm0, %xmm3, %xmm3
     123:      	jmp	0x154 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x154>
     125:      	nopw	%cs:(%rax,%rax)
     130:      	vmovdqu	0x20(%r8,%rbx), %xmm4
     137:      	vpsrlw	$0x2, %xmm3, %xmm5
     13c:      	vmovd	%ebp, %xmm3
     140:      	vpslld	$0x1, %xmm3, %xmm3
     145:      	vpsrlw	%xmm3, %xmm4, %xmm3
     149:      	vpand	%xmm1, %xmm3, %xmm3
     14d:      	vpternlogq	$0xf8, %xmm2, %xmm5, %xmm3 # xmm3 = xmm3 | (xmm5 & xmm2)
     154:      	vpmovzxbq	%xmm3, %zmm4    # zmm4 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero,xmm3[4],zero,zero,zero,zero,zero,zero,zero,xmm3[5],zero,zero,zero,zero,zero,zero,zero,xmm3[6],zero,zero,zero,zero,zero,zero,zero,xmm3[7],zero,zero,zero,zero,zero,zero,zero
     15a:      	movl	%esi, %ebp
     15c:      	andl	$-0x20, %ebp
     15f:      	orl	%ebp, %ebx
     161:      	cmpl	$0x10, %r11d
     165:      	je	0x60 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x60>
     16b:      	movl	%esi, %r14d
     16e:      	subl	%ebx, %r14d
     171:      	movl	$0x8, %r15d
     177:      	leal	(%r11,%r14), %ebp
     17b:      	cmpl	$0x8, %ebp
     17e:      	cmovbl	%ebp, %r15d
     182:      	subl	%r14d, %r15d
     185:      	jbe	0x1d0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1d0>
     187:      	vpmovzxbq	(%r9,%rbx), %zmm5
     18e:      	vpmovzxbq	(%rax,%rbx), %zmm6
     195:      	vpsllq	$0xe, %zmm5, %zmm7
     19c:      	vpsllq	$0x6, %zmm6, %zmm5
     1a3:      	vpternlogq	$0xfe, %zmm4, %zmm7, %zmm5 # zmm5 = zmm5 | zmm7 | zmm4
     1aa:      	cmpl	$0x8, %r15d
     1ae:      	jne	0x1b8 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1b8>
     1b0:      	vmovdqu64	%zmm5, (%rcx)
     1b6:      	jmp	0x1d0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1d0>
     1b8:      	shlxl	%r15d, %r10d, %r15d
     1bd:      	notl	%r15d
     1c0:      	shlxl	%r14d, %r15d, %r15d
     1c5:      	kmovd	%r15d, %k1
     1ca:      	vpcompressq	%zmm5, (%rcx) {%k1}
     1d0:      	cmpl	$0x9, %r14d
     1d4:      	movl	$0x8, %r15d
     1da:      	cmovael	%r14d, %r15d
     1de:      	cmpl	$0x10, %ebp
     1e1:      	cmovael	%edi, %ebp
     1e4:      	subl	%r15d, %ebp
     1e7:      	jbe	0xc0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xc0>
     1ed:      	vpshufd	$0xee, %xmm3, %xmm3     # xmm3 = xmm3[2,3,2,3]
     1f2:      	vpmovzxbq	0x8(%rax,%rbx), %zmm5
     1fa:      	vpmovzxbq	%xmm3, %zmm4    # zmm4 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero,xmm3[4],zero,zero,zero,zero,zero,zero,zero,xmm3[5],zero,zero,zero,zero,zero,zero,zero,xmm3[6],zero,zero,zero,zero,zero,zero,zero,xmm3[7],zero,zero,zero,zero,zero,zero,zero
     200:      	vpmovzxbq	0x8(%r9,%rbx), %zmm3
     208:      	movl	%r15d, %ebx
     20b:      	subl	%r14d, %ebx
     20e:      	leaq	(%rcx,%rbx,8), %rbx
     212:      	vpsllq	$0xe, %zmm3, %zmm6
     219:      	vpsllq	$0x6, %zmm5, %zmm3
     220:      	vpternlogq	$0xfe, %zmm4, %zmm6, %zmm3 # zmm3 = zmm3 | zmm6 | zmm4
     227:      	cmpl	$0x8, %ebp
     22a:      	jne	0x237 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x237>
     22c:      	vmovdqu64	%zmm3, (%rbx)
     232:      	jmp	0xc0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xc0>
     237:      	shlxl	%ebp, %r10d, %ebp
     23c:      	addb	$-0x8, %r15b
     240:      	notl	%ebp
     242:      	shlxl	%r15d, %ebp, %ebp
     247:      	kmovd	%ebp, %k1
     24b:      	vpcompressq	%zmm3, (%rbx) {%k1}
     251:      	jmp	0xc0 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_10avx512_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0xc0>
     256:      	popq	%rbx
     257:      	popq	%r14
     259:      	popq	%r15
     25b:      	popq	%rbp
     25c:      	vzeroupper
     25f:      	retq
