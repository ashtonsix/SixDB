
/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/build/maintenance/striped-initial-heads/native.o:	file format elf64-x86-64

Disassembly of section .text._ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_:

0000000000000000 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%r12
       7:      	pushq	%rbx
       8:      	movq	0x10(%rdi), %r8
       c:      	movq	0x28(%rdi), %r9
      10:      	movq	%rsi, %rax
      13:      	shrq	$0x6, %rax
      17:      	vpbroadcastq	(%rip), %xmm0   # 0x20 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x20>
		000000000000001c:  R_X86_64_PC32	.LCPI1267_0-0x4
      20:      	vmovdqa	(%rip), %ymm1           # 0x28 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x28>
		0000000000000024:  R_X86_64_PC32	.LCPI1267_1-0x4
      28:      	vmovdqa	(%rip), %ymm2           # 0x30 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x30>
		000000000000002c:  R_X86_64_PC32	.LCPI1267_2-0x4
      30:      	subl	%esi, %edx
      32:      	andl	$0x3f, %esi
      35:      	addl	%esi, %edx
      37:      	imulq	%rax, %r8
      3b:      	imulq	%rax, %r9
      3f:      	imulq	0x40(%rdi), %rax
      44:      	addq	(%rdi), %r8
      47:      	addq	0x18(%rdi), %r9
      4b:      	addq	0x30(%rdi), %rax
      4f:      	movl	$0x10, %edi
      54:      	jmp	0x15c <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x15c>
      59:      	nopl	(%rax)
      60:      	vpmovzxbq	(%r9,%r11), %ymm6
      66:      	vpmovzxbq	(%rax,%r11), %ymm7
      6c:      	vpmovzxbq	(%r8,%r11), %ymm5
      72:      	vpsllq	$0x14, %ymm6, %ymm6
      77:      	vpsllq	$0xc, %ymm7, %ymm7
      7c:      	vpsllq	$0x4, %ymm5, %ymm5
      81:      	vpor	%ymm6, %ymm7, %ymm6
      85:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm6 # ymm6 = ymm6 | ymm5 | ymm4
      8c:      	vpshufd	$0x55, %xmm3, %xmm4     # xmm4 = xmm3[1,1,1,1]
      91:      	vmovdqu	%ymm6, (%rcx)
      95:      	vpmovzxbq	%xmm4, %ymm4    # ymm4 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
      9a:      	vpmovzxbq	0x4(%r9,%r11), %ymm6
      a1:      	vpmovzxbq	0x4(%rax,%r11), %ymm7
      a8:      	vpmovzxbq	0x4(%r8,%r11), %ymm5
      af:      	vpsllq	$0x14, %ymm6, %ymm6
      b4:      	vpsllq	$0xc, %ymm7, %ymm7
      b9:      	vpsllq	$0x4, %ymm5, %ymm5
      be:      	vpor	%ymm6, %ymm7, %ymm6
      c2:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm6 # ymm6 = ymm6 | ymm5 | ymm4
      c9:      	vpshufd	$0xee, %xmm3, %xmm4     # xmm4 = xmm3[2,3,2,3]
      ce:      	vpshufd	$0xff, %xmm3, %xmm3     # xmm3 = xmm3[3,3,3,3]
      d3:      	vmovdqu	%ymm6, 0x20(%rcx)
      d8:      	vpmovzxbq	%xmm4, %ymm4    # ymm4 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
      dd:      	vpmovzxbq	%xmm3, %ymm3    # ymm3 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero
      e2:      	vpmovzxbq	0x8(%r9,%r11), %ymm6
      e9:      	vpmovzxbq	0x8(%rax,%r11), %ymm7
      f0:      	vpmovzxbq	0x8(%r8,%r11), %ymm5
      f7:      	vpsllq	$0x14, %ymm6, %ymm6
      fc:      	vpsllq	$0xc, %ymm7, %ymm7
     101:      	vpsllq	$0x4, %ymm5, %ymm5
     106:      	vpor	%ymm6, %ymm7, %ymm6
     10a:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm6 # ymm6 = ymm6 | ymm5 | ymm4
     111:      	vmovdqu	%ymm6, 0x40(%rcx)
     116:      	vpmovzxbq	0xc(%r9,%r11), %ymm5
     11d:      	vpmovzxbq	0xc(%rax,%r11), %ymm6
     124:      	vpmovzxbq	0xc(%r8,%r11), %ymm4
     12b:      	vpsllq	$0x14, %ymm5, %ymm5
     130:      	vpsllq	$0xc, %ymm6, %ymm6
     135:      	vpsllq	$0x4, %ymm4, %ymm4
     13a:      	vpor	%ymm5, %ymm6, %ymm5
     13e:      	vpternlogq	$0xfe, %ymm3, %ymm4, %ymm5 # ymm5 = ymm5 | ymm4 | ymm3
     145:      	vmovdqu	%ymm5, 0x60(%rcx)
     14a:      	addl	%r10d, %esi
     14d:      	movl	%r10d, %r10d
     150:      	leaq	(%rcx,%r10,8), %rcx
     154:      	cmpl	%edx, %esi
     156:      	je	0x3fc <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x3fc>
     15c:      	movl	%esi, %ebx
     15e:      	movl	%edx, %r10d
     161:      	andl	$0x1f, %ebx
     164:      	movl	$0x20, %ebp
     169:      	subl	%esi, %r10d
     16c:      	movl	%esi, %r11d
     16f:      	subl	%ebx, %ebp
     171:      	cmpl	$0x10, %r10d
     175:      	cmovael	%edi, %r10d
     179:      	andl	$-0x20, %r11d
     17d:      	cmpl	$0x10, %ebx
     180:      	cmovael	%edi, %ebx
     183:      	vmovdqu	0x40(%r8,%rbx), %xmm3
     18a:      	orl	%ebx, %r11d
     18d:      	cmpl	%r10d, %ebp
     190:      	movl	%esi, %ebx
     192:      	cmovbl	%ebp, %r10d
     196:      	shrl	$0x3, %ebx
     199:      	andl	$-0x4, %ebx
     19c:      	vmovd	%ebx, %xmm4
     1a0:      	vpsrlw	%xmm4, %xmm3, %xmm3
     1a4:      	vpand	%xmm0, %xmm3, %xmm3
     1a8:      	vpmovzxbq	%xmm3, %ymm4    # ymm4 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero
     1ad:      	cmpl	$0x10, %r10d
     1b1:      	je	0x60 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x60>
     1b7:      	movl	%esi, %r14d
     1ba:      	subl	%r11d, %r14d
     1bd:      	movl	$0x4, %ebp
     1c2:      	leal	(%r10,%r14), %ebx
     1c6:      	cmpl	$0x4, %ebx
     1c9:      	cmovbl	%ebx, %ebp
     1cc:      	subl	%r14d, %ebp
     1cf:      	jbe	0x230 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x230>
     1d1:      	vpmovzxbq	(%r8,%r11), %ymm5
     1d7:      	vpmovzxbq	(%rax,%r11), %ymm7
     1dd:      	vpsllq	$0x4, %ymm5, %ymm6
     1e2:      	vpmovzxbq	(%r9,%r11), %ymm5
     1e8:      	vpsllq	$0xc, %ymm7, %ymm7
     1ed:      	vpsllq	$0x14, %ymm5, %ymm5
     1f2:      	vpor	%ymm5, %ymm7, %ymm5
     1f6:      	vpternlogq	$0xfe, %ymm4, %ymm6, %ymm5 # ymm5 = ymm5 | ymm6 | ymm4
     1fd:      	cmpl	$0x4, %ebp
     200:      	jne	0x208 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x208>
     202:      	vmovdqu	%ymm5, (%rcx)
     206:      	jmp	0x230 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x230>
     208:      	leal	(%r14,%r14), %r15d
     20c:      	vpbroadcastd	%r15d, %ymm4
     212:      	movl	%ebp, %r15d
     215:      	vpaddd	%ymm1, %ymm4, %ymm4
     219:      	vpermd	%ymm5, %ymm4, %ymm4
     21e:      	vpbroadcastq	%r15, %ymm5
     224:      	vpcmpgtq	%ymm2, %ymm5, %k1
     22a:      	vmovdqu64	%ymm4, (%rcx) {%k1}
     230:      	cmpl	$0x5, %r14d
     234:      	movl	$0x4, %r15d
     23a:      	movl	$0x8, %ebp
     23f:      	cmovael	%r14d, %r15d
     243:      	cmpl	$0x8, %ebx
     246:      	cmovbl	%ebx, %ebp
     249:      	subl	%r15d, %ebp
     24c:      	jbe	0x2c8 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2c8>
     24e:      	vpshufd	$0x55, %xmm3, %xmm4     # xmm4 = xmm3[1,1,1,1]
     253:      	vpmovzxbq	0x4(%rax,%r11), %ymm7
     25a:      	movl	%r15d, %r12d
     25d:      	subl	%r14d, %r12d
     260:      	vpmovzxbq	%xmm4, %ymm5    # ymm5 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
     265:      	vpmovzxbq	0x4(%r11,%r8), %ymm4
     26c:      	leaq	(%rcx,%r12,8), %r12
     270:      	vpsllq	$0xc, %ymm7, %ymm7
     275:      	vpsllq	$0x4, %ymm4, %ymm6
     27a:      	vpmovzxbq	0x4(%r9,%r11), %ymm4
     281:      	vpsllq	$0x14, %ymm4, %ymm4
     286:      	vpor	%ymm4, %ymm7, %ymm4
     28a:      	vpternlogq	$0xfe, %ymm5, %ymm6, %ymm4 # ymm4 = ymm4 | ymm6 | ymm5
     291:      	cmpl	$0x4, %ebp
     294:      	jne	0x29e <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x29e>
     296:      	vmovdqu	%ymm4, (%r12)
     29c:      	jmp	0x2c8 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2c8>
     29e:      	leal	-0x8(%r15,%r15), %r15d
     2a3:      	vpbroadcastd	%r15d, %ymm5
     2a9:      	movl	%ebp, %r15d
     2ac:      	vpaddd	%ymm1, %ymm5, %ymm5
     2b0:      	vpermd	%ymm4, %ymm5, %ymm4
     2b5:      	vpbroadcastq	%r15, %ymm5
     2bb:      	vpcmpgtq	%ymm2, %ymm5, %k1
     2c1:      	vmovdqu64	%ymm4, (%r12) {%k1}
     2c8:      	cmpl	$0x9, %r14d
     2cc:      	movl	$0x8, %r15d
     2d2:      	movl	$0xc, %ebp
     2d7:      	cmovael	%r14d, %r15d
     2db:      	cmpl	$0xc, %ebx
     2de:      	cmovbl	%ebx, %ebp
     2e1:      	subl	%r15d, %ebp
     2e4:      	jbe	0x360 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x360>
     2e6:      	vpshufd	$0xee, %xmm3, %xmm4     # xmm4 = xmm3[2,3,2,3]
     2eb:      	vpmovzxbq	0x8(%rax,%r11), %ymm7
     2f2:      	movl	%r15d, %r12d
     2f5:      	subl	%r14d, %r12d
     2f8:      	vpmovzxbq	%xmm4, %ymm5    # ymm5 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
     2fd:      	vpmovzxbq	0x8(%r11,%r8), %ymm4
     304:      	leaq	(%rcx,%r12,8), %r12
     308:      	vpsllq	$0xc, %ymm7, %ymm7
     30d:      	vpsllq	$0x4, %ymm4, %ymm6
     312:      	vpmovzxbq	0x8(%r9,%r11), %ymm4
     319:      	vpsllq	$0x14, %ymm4, %ymm4
     31e:      	vpor	%ymm4, %ymm7, %ymm4
     322:      	vpternlogq	$0xfe, %ymm5, %ymm6, %ymm4 # ymm4 = ymm4 | ymm6 | ymm5
     329:      	cmpl	$0x4, %ebp
     32c:      	jne	0x336 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x336>
     32e:      	vmovdqu	%ymm4, (%r12)
     334:      	jmp	0x360 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x360>
     336:      	leal	-0x10(%r15,%r15), %r15d
     33b:      	vpbroadcastd	%r15d, %ymm5
     341:      	movl	%ebp, %r15d
     344:      	vpaddd	%ymm1, %ymm5, %ymm5
     348:      	vpermd	%ymm4, %ymm5, %ymm4
     34d:      	vpbroadcastq	%r15, %ymm5
     353:      	vpcmpgtq	%ymm2, %ymm5, %k1
     359:      	vmovdqu64	%ymm4, (%r12) {%k1}
     360:      	cmpl	$0xd, %r14d
     364:      	movl	$0xc, %r15d
     36a:      	cmovael	%r14d, %r15d
     36e:      	cmpl	$0x10, %ebx
     371:      	cmovael	%edi, %ebx
     374:      	subl	%r15d, %ebx
     377:      	jbe	0x14a <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x14a>
     37d:      	vpshufd	$0xff, %xmm3, %xmm3     # xmm3 = xmm3[3,3,3,3]
     382:      	vpmovzxbq	0xc(%rax,%r11), %ymm6
     389:      	vpmovzxbq	%xmm3, %ymm4    # ymm4 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero
     38e:      	vpmovzxbq	0xc(%r11,%r8), %ymm3
     395:      	vpsllq	$0xc, %ymm6, %ymm6
     39a:      	vpsllq	$0x4, %ymm3, %ymm5
     39f:      	vpmovzxbq	0xc(%r9,%r11), %ymm3
     3a6:      	movl	%r15d, %r11d
     3a9:      	subl	%r14d, %r11d
     3ac:      	leaq	(%rcx,%r11,8), %r11
     3b0:      	vpsllq	$0x14, %ymm3, %ymm3
     3b5:      	vpor	%ymm3, %ymm6, %ymm3
     3b9:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm3 # ymm3 = ymm3 | ymm5 | ymm4
     3c0:      	cmpl	$0x4, %ebx
     3c3:      	jne	0x3cf <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x3cf>
     3c5:      	vmovdqu	%ymm3, (%r11)
     3ca:      	jmp	0x14a <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x14a>
     3cf:      	leal	-0x18(%r15,%r15), %ebp
     3d4:      	movl	%ebx, %ebx
     3d6:      	vpbroadcastd	%ebp, %ymm4
     3dc:      	vpaddd	%ymm1, %ymm4, %ymm4
     3e0:      	vpermd	%ymm3, %ymm4, %ymm3
     3e5:      	vpbroadcastq	%rbx, %ymm4
     3eb:      	vpcmpgtq	%ymm2, %ymm4, %k1
     3f1:      	vmovdqu64	%ymm3, (%r11) {%k1}
     3f7:      	jmp	0x14a <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj12ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x14a>
     3fc:      	popq	%rbx
     3fd:      	popq	%r12
     3ff:      	popq	%r14
     401:      	popq	%r15
     403:      	popq	%rbp
     404:      	vzeroupper
     407:      	retq
