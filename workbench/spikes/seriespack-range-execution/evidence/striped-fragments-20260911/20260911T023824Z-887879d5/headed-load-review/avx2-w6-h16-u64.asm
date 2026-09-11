
/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/build/maintenance/striped-initial-heads/native.o:	file format elf64-x86-64

Disassembly of section .text._ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_:

0000000000000000 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%r12
       7:      	pushq	%rbx
       8:      	movq	0x10(%rdi), %r8
       c:      	movq	0x28(%rdi), %r9
      10:      	movq	%rsi, %rax
      13:      	shrq	$0x7, %rax
      17:      	vpbroadcastq	(%rip), %xmm0   # 0x20 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x20>
		000000000000001c:  R_X86_64_PC32	.LCPI1189_2-0x4
      20:      	vmovdqa	(%rip), %ymm1           # 0x28 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x28>
		0000000000000024:  R_X86_64_PC32	.LCPI1189_3-0x4
      28:      	vmovdqa	(%rip), %ymm2           # 0x30 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x30>
		000000000000002c:  R_X86_64_PC32	.LCPI1189_4-0x4
      30:      	vpbroadcastq	(%rip), %xmm3   # 0x39 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x39>
		0000000000000035:  R_X86_64_PC32	.LCPI1189_0-0x4
      39:      	vpbroadcastq	(%rip), %xmm4   # 0x42 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x42>
		000000000000003e:  R_X86_64_PC32	.LCPI1189_1-0x4
      42:      	subl	%esi, %edx
      44:      	andl	$0x7f, %esi
      47:      	addl	%esi, %edx
      49:      	imulq	%rax, %r8
      4d:      	imulq	%rax, %r9
      51:      	imulq	0x40(%rdi), %rax
      56:      	addq	(%rdi), %r8
      59:      	addq	0x18(%rdi), %r9
      5d:      	addq	0x30(%rdi), %rax
      61:      	movl	$0x10, %edi
      66:      	jmp	0x130 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x130>
      6b:      	nopl	(%rax,%rax)
      70:      	vpmovzxbq	(%r9,%r11), %ymm7
      76:      	vpmovzxbq	(%rax,%r11), %ymm8
      7c:      	vpsllq	$0xe, %ymm7, %ymm7
      81:      	vpsllq	$0x6, %ymm8, %ymm8
      87:      	vpternlogq	$0xfe, %ymm6, %ymm7, %ymm8 # ymm8 = ymm8 | ymm7 | ymm6
      8e:      	vpshufd	$0x55, %xmm5, %xmm6     # xmm6 = xmm5[1,1,1,1]
      93:      	vmovdqu	%ymm8, (%rcx)
      97:      	vpmovzxbq	%xmm6, %ymm6    # ymm6 = xmm6[0],zero,zero,zero,zero,zero,zero,zero,xmm6[1],zero,zero,zero,zero,zero,zero,zero,xmm6[2],zero,zero,zero,zero,zero,zero,zero,xmm6[3],zero,zero,zero,zero,zero,zero,zero
      9c:      	vpmovzxbq	0x4(%r9,%r11), %ymm7
      a3:      	vpmovzxbq	0x4(%rax,%r11), %ymm8
      aa:      	vpsllq	$0xe, %ymm7, %ymm7
      af:      	vpsllq	$0x6, %ymm8, %ymm8
      b5:      	vpternlogq	$0xfe, %ymm6, %ymm7, %ymm8 # ymm8 = ymm8 | ymm7 | ymm6
      bc:      	vpshufd	$0xee, %xmm5, %xmm6     # xmm6 = xmm5[2,3,2,3]
      c1:      	vpshufd	$0xff, %xmm5, %xmm5     # xmm5 = xmm5[3,3,3,3]
      c6:      	vmovdqu	%ymm8, 0x20(%rcx)
      cb:      	vpmovzxbq	%xmm6, %ymm6    # ymm6 = xmm6[0],zero,zero,zero,zero,zero,zero,zero,xmm6[1],zero,zero,zero,zero,zero,zero,zero,xmm6[2],zero,zero,zero,zero,zero,zero,zero,xmm6[3],zero,zero,zero,zero,zero,zero,zero
      d0:      	vpmovzxbq	%xmm5, %ymm5    # ymm5 = xmm5[0],zero,zero,zero,zero,zero,zero,zero,xmm5[1],zero,zero,zero,zero,zero,zero,zero,xmm5[2],zero,zero,zero,zero,zero,zero,zero,xmm5[3],zero,zero,zero,zero,zero,zero,zero
      d5:      	vpmovzxbq	0x8(%r9,%r11), %ymm7
      dc:      	vpmovzxbq	0x8(%rax,%r11), %ymm8
      e3:      	vpsllq	$0xe, %ymm7, %ymm7
      e8:      	vpsllq	$0x6, %ymm8, %ymm8
      ee:      	vpternlogq	$0xfe, %ymm6, %ymm7, %ymm8 # ymm8 = ymm8 | ymm7 | ymm6
      f5:      	vmovdqu	%ymm8, 0x40(%rcx)
      fa:      	vpmovzxbq	0xc(%r9,%r11), %ymm6
     101:      	vpmovzxbq	0xc(%rax,%r11), %ymm7
     108:      	vpsllq	$0xe, %ymm6, %ymm6
     10d:      	vpsllq	$0x6, %ymm7, %ymm7
     112:      	vpternlogq	$0xfe, %ymm5, %ymm6, %ymm7 # ymm7 = ymm7 | ymm6 | ymm5
     119:      	vmovdqu	%ymm7, 0x60(%rcx)
     11e:      	addl	%r10d, %esi
     121:      	movl	%r10d, %r10d
     124:      	leaq	(%rcx,%r10,8), %rcx
     128:      	cmpl	%edx, %esi
     12a:      	je	0x3d4 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x3d4>
     130:      	movl	%esi, %r11d
     133:      	movl	%edx, %r10d
     136:      	andl	$0x1f, %r11d
     13a:      	movl	$0x20, %ebx
     13f:      	subl	%esi, %r10d
     142:      	movl	%esi, %ebp
     144:      	subl	%r11d, %ebx
     147:      	cmpl	$0x10, %r10d
     14b:      	cmovael	%edi, %r10d
     14f:      	cmpl	$0x10, %r11d
     153:      	cmovael	%edi, %r11d
     157:      	cmpl	%r10d, %ebx
     15a:      	cmovbl	%ebx, %r10d
     15e:      	shrl	$0x5, %ebp
     161:      	movl	%ebp, %ebx
     163:      	andl	$0x2, %ebx
     166:      	incl	%ebp
     168:      	movl	%ebx, %r14d
     16b:      	shll	$0x5, %r14d
     16f:      	addq	%r8, %r14
     172:      	vmovdqu	(%r11,%r14), %xmm5
     178:      	testb	$0x2, %bpl
     17c:      	jne	0x190 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x190>
     17e:      	vpand	%xmm0, %xmm5, %xmm5
     182:      	jmp	0x1b4 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1b4>
     184:      	nopw	%cs:(%rax,%rax)
     190:      	vmovdqu	0x20(%r8,%r11), %xmm6
     197:      	vpsrlw	$0x2, %xmm5, %xmm7
     19c:      	vmovd	%ebx, %xmm5
     1a0:      	vpslld	$0x1, %xmm5, %xmm5
     1a5:      	vpsrlw	%xmm5, %xmm6, %xmm5
     1a9:      	vpand	%xmm3, %xmm5, %xmm5
     1ad:      	vpternlogq	$0xf8, %xmm4, %xmm7, %xmm5 # xmm5 = xmm5 | (xmm7 & xmm4)
     1b4:      	movl	%esi, %ebx
     1b6:      	andl	$-0x20, %ebx
     1b9:      	vpmovzxbq	%xmm5, %ymm6    # ymm6 = xmm5[0],zero,zero,zero,zero,zero,zero,zero,xmm5[1],zero,zero,zero,zero,zero,zero,zero,xmm5[2],zero,zero,zero,zero,zero,zero,zero,xmm5[3],zero,zero,zero,zero,zero,zero,zero
     1be:      	orl	%ebx, %r11d
     1c1:      	cmpl	$0x10, %r10d
     1c5:      	je	0x70 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x70>
     1cb:      	movl	%esi, %r14d
     1ce:      	subl	%r11d, %r14d
     1d1:      	movl	$0x4, %ebp
     1d6:      	leal	(%r10,%r14), %ebx
     1da:      	cmpl	$0x4, %ebx
     1dd:      	cmovbl	%ebx, %ebp
     1e0:      	subl	%r14d, %ebp
     1e3:      	jbe	0x236 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x236>
     1e5:      	vpmovzxbq	(%r9,%r11), %ymm7
     1eb:      	vpmovzxbq	(%rax,%r11), %ymm8
     1f1:      	vpsllq	$0xe, %ymm7, %ymm9
     1f6:      	vpsllq	$0x6, %ymm8, %ymm7
     1fc:      	vpternlogq	$0xfe, %ymm6, %ymm9, %ymm7 # ymm7 = ymm7 | ymm9 | ymm6
     203:      	cmpl	$0x4, %ebp
     206:      	jne	0x20e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x20e>
     208:      	vmovdqu	%ymm7, (%rcx)
     20c:      	jmp	0x236 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x236>
     20e:      	leal	(%r14,%r14), %r15d
     212:      	vpbroadcastd	%r15d, %ymm6
     218:      	movl	%ebp, %r15d
     21b:      	vpaddd	%ymm1, %ymm6, %ymm6
     21f:      	vpermd	%ymm7, %ymm6, %ymm6
     224:      	vpbroadcastq	%r15, %ymm7
     22a:      	vpcmpgtq	%ymm2, %ymm7, %k1
     230:      	vmovdqu64	%ymm6, (%rcx) {%k1}
     236:      	cmpl	$0x5, %r14d
     23a:      	movl	$0x4, %r15d
     240:      	movl	$0x8, %ebp
     245:      	cmovael	%r14d, %r15d
     249:      	cmpl	$0x8, %ebx
     24c:      	cmovbl	%ebx, %ebp
     24f:      	subl	%r15d, %ebp
     252:      	jbe	0x2bf <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2bf>
     254:      	vpshufd	$0x55, %xmm5, %xmm6     # xmm6 = xmm5[1,1,1,1]
     259:      	vpmovzxbq	0x4(%rax,%r11), %ymm8
     260:      	movl	%r15d, %r12d
     263:      	subl	%r14d, %r12d
     266:      	vpmovzxbq	%xmm6, %ymm7    # ymm7 = xmm6[0],zero,zero,zero,zero,zero,zero,zero,xmm6[1],zero,zero,zero,zero,zero,zero,zero,xmm6[2],zero,zero,zero,zero,zero,zero,zero,xmm6[3],zero,zero,zero,zero,zero,zero,zero
     26b:      	vpmovzxbq	0x4(%r9,%r11), %ymm6
     272:      	leaq	(%rcx,%r12,8), %r12
     276:      	vpsllq	$0xe, %ymm6, %ymm9
     27b:      	vpsllq	$0x6, %ymm8, %ymm6
     281:      	vpternlogq	$0xfe, %ymm7, %ymm9, %ymm6 # ymm6 = ymm6 | ymm9 | ymm7
     288:      	cmpl	$0x4, %ebp
     28b:      	jne	0x295 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x295>
     28d:      	vmovdqu	%ymm6, (%r12)
     293:      	jmp	0x2bf <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2bf>
     295:      	leal	-0x8(%r15,%r15), %r15d
     29a:      	vpbroadcastd	%r15d, %ymm7
     2a0:      	movl	%ebp, %r15d
     2a3:      	vpaddd	%ymm1, %ymm7, %ymm7
     2a7:      	vpermd	%ymm6, %ymm7, %ymm6
     2ac:      	vpbroadcastq	%r15, %ymm7
     2b2:      	vpcmpgtq	%ymm2, %ymm7, %k1
     2b8:      	vmovdqu64	%ymm6, (%r12) {%k1}
     2bf:      	cmpl	$0x9, %r14d
     2c3:      	movl	$0x8, %r15d
     2c9:      	movl	$0xc, %ebp
     2ce:      	cmovael	%r14d, %r15d
     2d2:      	cmpl	$0xc, %ebx
     2d5:      	cmovbl	%ebx, %ebp
     2d8:      	subl	%r15d, %ebp
     2db:      	jbe	0x348 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x348>
     2dd:      	vpshufd	$0xee, %xmm5, %xmm6     # xmm6 = xmm5[2,3,2,3]
     2e2:      	vpmovzxbq	0x8(%rax,%r11), %ymm8
     2e9:      	movl	%r15d, %r12d
     2ec:      	subl	%r14d, %r12d
     2ef:      	vpmovzxbq	%xmm6, %ymm7    # ymm7 = xmm6[0],zero,zero,zero,zero,zero,zero,zero,xmm6[1],zero,zero,zero,zero,zero,zero,zero,xmm6[2],zero,zero,zero,zero,zero,zero,zero,xmm6[3],zero,zero,zero,zero,zero,zero,zero
     2f4:      	vpmovzxbq	0x8(%r9,%r11), %ymm6
     2fb:      	leaq	(%rcx,%r12,8), %r12
     2ff:      	vpsllq	$0xe, %ymm6, %ymm9
     304:      	vpsllq	$0x6, %ymm8, %ymm6
     30a:      	vpternlogq	$0xfe, %ymm7, %ymm9, %ymm6 # ymm6 = ymm6 | ymm9 | ymm7
     311:      	cmpl	$0x4, %ebp
     314:      	jne	0x31e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x31e>
     316:      	vmovdqu	%ymm6, (%r12)
     31c:      	jmp	0x348 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x348>
     31e:      	leal	-0x10(%r15,%r15), %r15d
     323:      	vpbroadcastd	%r15d, %ymm7
     329:      	movl	%ebp, %r15d
     32c:      	vpaddd	%ymm1, %ymm7, %ymm7
     330:      	vpermd	%ymm6, %ymm7, %ymm6
     335:      	vpbroadcastq	%r15, %ymm7
     33b:      	vpcmpgtq	%ymm2, %ymm7, %k1
     341:      	vmovdqu64	%ymm6, (%r12) {%k1}
     348:      	cmpl	$0xd, %r14d
     34c:      	movl	$0xc, %r15d
     352:      	cmovael	%r14d, %r15d
     356:      	cmpl	$0x10, %ebx
     359:      	cmovael	%edi, %ebx
     35c:      	subl	%r15d, %ebx
     35f:      	jbe	0x11e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x11e>
     365:      	vpshufd	$0xff, %xmm5, %xmm5     # xmm5 = xmm5[3,3,3,3]
     36a:      	vpmovzxbq	0xc(%rax,%r11), %ymm7
     371:      	vpmovzxbq	%xmm5, %ymm6    # ymm6 = xmm5[0],zero,zero,zero,zero,zero,zero,zero,xmm5[1],zero,zero,zero,zero,zero,zero,zero,xmm5[2],zero,zero,zero,zero,zero,zero,zero,xmm5[3],zero,zero,zero,zero,zero,zero,zero
     376:      	vpmovzxbq	0xc(%r9,%r11), %ymm5
     37d:      	movl	%r15d, %r11d
     380:      	subl	%r14d, %r11d
     383:      	leaq	(%rcx,%r11,8), %r11
     387:      	vpsllq	$0xe, %ymm5, %ymm8
     38c:      	vpsllq	$0x6, %ymm7, %ymm5
     391:      	vpternlogq	$0xfe, %ymm6, %ymm8, %ymm5 # ymm5 = ymm5 | ymm8 | ymm6
     398:      	cmpl	$0x4, %ebx
     39b:      	jne	0x3a7 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x3a7>
     39d:      	vmovdqu	%ymm5, (%r11)
     3a2:      	jmp	0x11e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x11e>
     3a7:      	leal	-0x18(%r15,%r15), %ebp
     3ac:      	movl	%ebx, %ebx
     3ae:      	vpbroadcastd	%ebp, %ymm6
     3b4:      	vpaddd	%ymm1, %ymm6, %ymm6
     3b8:      	vpermd	%ymm5, %ymm6, %ymm5
     3bd:      	vpbroadcastq	%rbx, %ymm6
     3c3:      	vpcmpgtq	%ymm2, %ymm6, %k1
     3c9:      	vmovdqu64	%ymm5, (%r11) {%k1}
     3cf:      	jmp	0x11e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj6ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x11e>
     3d4:      	popq	%rbx
     3d5:      	popq	%r12
     3d7:      	popq	%r14
     3d9:      	popq	%r15
     3db:      	popq	%rbp
     3dc:      	vzeroupper
     3df:      	retq
