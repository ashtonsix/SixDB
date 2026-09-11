
/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/build/maintenance/striped-initial-heads/native.o:	file format elf64-x86-64

Disassembly of section .text._ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_:

0000000000000000 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_>:
       0:      	pushq	%rbp
       1:      	pushq	%r15
       3:      	pushq	%r14
       5:      	pushq	%r13
       7:      	pushq	%r12
       9:      	pushq	%rbx
       a:      	movq	0x10(%rdi), %r8
       e:      	movq	0x28(%rdi), %r9
      12:      	movzbl	%sil, %eax
      16:      	subl	%esi, %edx
      18:      	shrq	$0x8, %rsi
      1c:      	vpbroadcastq	(%rip), %xmm0   # 0x25 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x25>
		0000000000000021:  R_X86_64_PC32	.LCPI1159_0-0x4
      25:      	vmovdqa	(%rip), %ymm1           # 0x2d <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2d>
		0000000000000029:  R_X86_64_PC32	.LCPI1159_1-0x4
      2d:      	vmovdqa	(%rip), %ymm2           # 0x35 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x35>
		0000000000000031:  R_X86_64_PC32	.LCPI1159_2-0x4
      35:      	movl	$0xffffffff, %r10d      # imm = 0xFFFFFFFF
      3b:      	addl	%eax, %edx
      3d:      	imulq	%rsi, %r8
      41:      	imulq	%rsi, %r9
      45:      	imulq	0x40(%rdi), %rsi
      4a:      	addq	(%rdi), %r8
      4d:      	addq	0x18(%rdi), %r9
      51:      	addq	0x30(%rdi), %rsi
      55:      	movl	$0x10, %edi
      5a:      	jmp	0x11d <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x11d>
      5f:      	nop
      60:      	vpmovzxbq	(%r9,%rbx), %ymm5
      66:      	vpmovzxbq	(%rsi,%rbx), %ymm6
      6c:      	vpsllq	$0xd, %ymm5, %ymm5
      71:      	vpsllq	$0x5, %ymm6, %ymm6
      76:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm6 # ymm6 = ymm6 | ymm5 | ymm4
      7d:      	vpshufd	$0x55, %xmm3, %xmm4     # xmm4 = xmm3[1,1,1,1]
      82:      	vmovdqu	%ymm6, (%rcx)
      86:      	vpmovzxbq	%xmm4, %ymm4    # ymm4 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
      8b:      	vpmovzxbq	0x4(%r9,%rbx), %ymm5
      92:      	vpmovzxbq	0x4(%rsi,%rbx), %ymm6
      99:      	vpsllq	$0xd, %ymm5, %ymm5
      9e:      	vpsllq	$0x5, %ymm6, %ymm6
      a3:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm6 # ymm6 = ymm6 | ymm5 | ymm4
      aa:      	vpshufd	$0xee, %xmm3, %xmm4     # xmm4 = xmm3[2,3,2,3]
      af:      	vpshufd	$0xff, %xmm3, %xmm3     # xmm3 = xmm3[3,3,3,3]
      b4:      	vmovdqu	%ymm6, 0x20(%rcx)
      b9:      	vpmovzxbq	%xmm4, %ymm4    # ymm4 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
      be:      	vpmovzxbq	%xmm3, %ymm3    # ymm3 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero
      c3:      	vpmovzxbq	0x8(%r9,%rbx), %ymm5
      ca:      	vpmovzxbq	0x8(%rsi,%rbx), %ymm6
      d1:      	vpsllq	$0xd, %ymm5, %ymm5
      d6:      	vpsllq	$0x5, %ymm6, %ymm6
      db:      	vpternlogq	$0xfe, %ymm4, %ymm5, %ymm6 # ymm6 = ymm6 | ymm5 | ymm4
      e2:      	vmovdqu	%ymm6, 0x40(%rcx)
      e7:      	vpmovzxbq	0xc(%r9,%rbx), %ymm4
      ee:      	vpmovzxbq	0xc(%rsi,%rbx), %ymm5
      f5:      	vpsllq	$0xd, %ymm4, %ymm4
      fa:      	vpsllq	$0x5, %ymm5, %ymm5
      ff:      	vpternlogq	$0xfe, %ymm3, %ymm4, %ymm5 # ymm5 = ymm5 | ymm4 | ymm3
     106:      	vmovdqu	%ymm5, 0x60(%rcx)
     10b:      	addl	%r11d, %eax
     10e:      	movl	%r11d, %r11d
     111:      	leaq	(%rcx,%r11,8), %rcx
     115:      	cmpl	%edx, %eax
     117:      	je	0x3d1 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x3d1>
     11d:      	movl	%eax, %ebx
     11f:      	movl	%edx, %r11d
     122:      	andl	$0x1f, %ebx
     125:      	movl	$0x20, %ebp
     12a:      	subl	%eax, %r11d
     12d:      	movl	%eax, %r14d
     130:      	subl	%ebx, %ebp
     132:      	cmpl	$0x10, %r11d
     136:      	cmovael	%edi, %r11d
     13a:      	cmpl	$0x10, %ebx
     13d:      	cmovael	%edi, %ebx
     140:      	cmpl	%r11d, %ebp
     143:      	cmovbl	%ebp, %r11d
     147:      	shrl	$0x5, %r14d
     14b:      	leal	(%r14,%r14,4), %ebp
     14f:      	shll	$0x2, %r14d
     153:      	leal	(%r14,%r14,4), %r14d
     157:      	andl	$0x7, %ebp
     15a:      	andl	$-0x20, %r14d
     15e:      	addq	%r8, %r14
     161:      	vmovdqu	(%rbx,%r14), %xmm3
     167:      	cmpl	$0x3, %ebp
     16a:      	ja	0x180 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x180>
     16c:      	vmovd	%ebp, %xmm4
     170:      	vpsrlw	%xmm4, %xmm3, %xmm3
     174:      	vpand	%xmm0, %xmm3, %xmm3
     178:      	jmp	0x1ac <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x1ac>
     17a:      	nopw	(%rax,%rax)
     180:      	addb	$-0x3, %bpl
     184:      	vpsrlw	$0x3, %xmm3, %xmm4
     189:      	shlxl	%ebp, %r10d, %ebp
     18e:      	vpbroadcastb	%ebp, %xmm3
     194:      	vpandn	0x20(%rbx,%r14), %xmm3, %xmm3
     19b:      	andb	$0x1f, %bpl
     19f:      	vpbroadcastb	%ebp, %xmm5
     1a5:      	vpternlogq	$0xf8, %xmm5, %xmm4, %xmm3 # xmm3 = xmm3 | (xmm4 & xmm5)
     1ac:      	movl	%eax, %ebp
     1ae:      	andl	$-0x20, %ebp
     1b1:      	vpmovzxbq	%xmm3, %ymm4    # ymm4 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero
     1b6:      	orl	%ebp, %ebx
     1b8:      	cmpl	$0x10, %r11d
     1bc:      	je	0x60 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x60>
     1c2:      	movl	%eax, %r14d
     1c5:      	subl	%ebx, %r14d
     1c8:      	movl	$0x4, %r15d
     1ce:      	leal	(%r11,%r14), %ebp
     1d2:      	cmpl	$0x4, %ebp
     1d5:      	cmovbl	%ebp, %r15d
     1d9:      	subl	%r14d, %r15d
     1dc:      	jbe	0x22f <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x22f>
     1de:      	vpmovzxbq	(%r9,%rbx), %ymm5
     1e4:      	vpmovzxbq	(%rsi,%rbx), %ymm6
     1ea:      	vpsllq	$0xd, %ymm5, %ymm7
     1ef:      	vpsllq	$0x5, %ymm6, %ymm5
     1f4:      	vpternlogq	$0xfe, %ymm4, %ymm7, %ymm5 # ymm5 = ymm5 | ymm7 | ymm4
     1fb:      	cmpl	$0x4, %r15d
     1ff:      	jne	0x207 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x207>
     201:      	vmovdqu	%ymm5, (%rcx)
     205:      	jmp	0x22f <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x22f>
     207:      	leal	(%r14,%r14), %r12d
     20b:      	movl	%r15d, %r15d
     20e:      	vpbroadcastd	%r12d, %ymm4
     214:      	vpaddd	%ymm1, %ymm4, %ymm4
     218:      	vpermd	%ymm5, %ymm4, %ymm4
     21d:      	vpbroadcastq	%r15, %ymm5
     223:      	vpcmpgtq	%ymm2, %ymm5, %k1
     229:      	vmovdqu64	%ymm4, (%rcx) {%k1}
     22f:      	cmpl	$0x5, %r14d
     233:      	movl	$0x4, %r12d
     239:      	movl	$0x8, %r15d
     23f:      	cmovael	%r14d, %r12d
     243:      	cmpl	$0x8, %ebp
     246:      	cmovbl	%ebp, %r15d
     24a:      	subl	%r12d, %r15d
     24d:      	jbe	0x2ba <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2ba>
     24f:      	vpshufd	$0x55, %xmm3, %xmm4     # xmm4 = xmm3[1,1,1,1]
     254:      	vpmovzxbq	0x4(%rsi,%rbx), %ymm6
     25b:      	movl	%r12d, %r13d
     25e:      	subl	%r14d, %r13d
     261:      	vpmovzxbq	%xmm4, %ymm5    # ymm5 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
     266:      	vpmovzxbq	0x4(%r9,%rbx), %ymm4
     26d:      	leaq	(%rcx,%r13,8), %r13
     271:      	vpsllq	$0xd, %ymm4, %ymm7
     276:      	vpsllq	$0x5, %ymm6, %ymm4
     27b:      	vpternlogq	$0xfe, %ymm5, %ymm7, %ymm4 # ymm4 = ymm4 | ymm7 | ymm5
     282:      	cmpl	$0x4, %r15d
     286:      	jne	0x290 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x290>
     288:      	vmovdqu	%ymm4, (%r13)
     28e:      	jmp	0x2ba <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x2ba>
     290:      	leal	-0x8(%r12,%r12), %r12d
     295:      	movl	%r15d, %r15d
     298:      	vpbroadcastd	%r12d, %ymm5
     29e:      	vpaddd	%ymm1, %ymm5, %ymm5
     2a2:      	vpermd	%ymm4, %ymm5, %ymm4
     2a7:      	vpbroadcastq	%r15, %ymm5
     2ad:      	vpcmpgtq	%ymm2, %ymm5, %k1
     2b3:      	vmovdqu64	%ymm4, (%r13) {%k1}
     2ba:      	cmpl	$0x9, %r14d
     2be:      	movl	$0x8, %r12d
     2c4:      	movl	$0xc, %r15d
     2ca:      	cmovael	%r14d, %r12d
     2ce:      	cmpl	$0xc, %ebp
     2d1:      	cmovbl	%ebp, %r15d
     2d5:      	subl	%r12d, %r15d
     2d8:      	jbe	0x345 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x345>
     2da:      	vpshufd	$0xee, %xmm3, %xmm4     # xmm4 = xmm3[2,3,2,3]
     2df:      	vpmovzxbq	0x8(%rsi,%rbx), %ymm6
     2e6:      	movl	%r12d, %r13d
     2e9:      	subl	%r14d, %r13d
     2ec:      	vpmovzxbq	%xmm4, %ymm5    # ymm5 = xmm4[0],zero,zero,zero,zero,zero,zero,zero,xmm4[1],zero,zero,zero,zero,zero,zero,zero,xmm4[2],zero,zero,zero,zero,zero,zero,zero,xmm4[3],zero,zero,zero,zero,zero,zero,zero
     2f1:      	vpmovzxbq	0x8(%r9,%rbx), %ymm4
     2f8:      	leaq	(%rcx,%r13,8), %r13
     2fc:      	vpsllq	$0xd, %ymm4, %ymm7
     301:      	vpsllq	$0x5, %ymm6, %ymm4
     306:      	vpternlogq	$0xfe, %ymm5, %ymm7, %ymm4 # ymm4 = ymm4 | ymm7 | ymm5
     30d:      	cmpl	$0x4, %r15d
     311:      	jne	0x31b <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x31b>
     313:      	vmovdqu	%ymm4, (%r13)
     319:      	jmp	0x345 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x345>
     31b:      	leal	-0x10(%r12,%r12), %r12d
     320:      	movl	%r15d, %r15d
     323:      	vpbroadcastd	%r12d, %ymm5
     329:      	vpaddd	%ymm1, %ymm5, %ymm5
     32d:      	vpermd	%ymm4, %ymm5, %ymm4
     332:      	vpbroadcastq	%r15, %ymm5
     338:      	vpcmpgtq	%ymm2, %ymm5, %k1
     33e:      	vmovdqu64	%ymm4, (%r13) {%k1}
     345:      	cmpl	$0xd, %r14d
     349:      	movl	$0xc, %r15d
     34f:      	cmovael	%r14d, %r15d
     353:      	cmpl	$0x10, %ebp
     356:      	cmovael	%edi, %ebp
     359:      	subl	%r15d, %ebp
     35c:      	jbe	0x10b <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x10b>
     362:      	vpshufd	$0xff, %xmm3, %xmm3     # xmm3 = xmm3[3,3,3,3]
     367:      	vpmovzxbq	0xc(%rsi,%rbx), %ymm5
     36e:      	vpmovzxbq	%xmm3, %ymm4    # ymm4 = xmm3[0],zero,zero,zero,zero,zero,zero,zero,xmm3[1],zero,zero,zero,zero,zero,zero,zero,xmm3[2],zero,zero,zero,zero,zero,zero,zero,xmm3[3],zero,zero,zero,zero,zero,zero,zero
     373:      	vpmovzxbq	0xc(%r9,%rbx), %ymm3
     37a:      	movl	%r15d, %ebx
     37d:      	subl	%r14d, %ebx
     380:      	leaq	(%rcx,%rbx,8), %rbx
     384:      	vpsllq	$0xd, %ymm3, %ymm6
     389:      	vpsllq	$0x5, %ymm5, %ymm3
     38e:      	vpternlogq	$0xfe, %ymm4, %ymm6, %ymm3 # ymm3 = ymm3 | ymm6 | ymm4
     395:      	cmpl	$0x4, %ebp
     398:      	jne	0x3a3 <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x3a3>
     39a:      	vmovdqu	%ymm3, (%rbx)
     39e:      	jmp	0x10b <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x10b>
     3a3:      	leal	-0x18(%r15,%r15), %r14d
     3a8:      	vpbroadcastd	%r14d, %ymm4
     3ae:      	movl	%ebp, %r14d
     3b1:      	vpaddd	%ymm1, %ymm4, %ymm4
     3b5:      	vpermd	%ymm3, %ymm4, %ymm3
     3ba:      	vpbroadcastq	%r14, %ymm4
     3c0:      	vpcmpgtq	%ymm2, %ymm4, %k1
     3c6:      	vmovdqu64	%ymm3, (%rbx) {%k1}
     3cc:      	jmp	0x10b <_ZN4ikea10seriespack6detail12_GLOBAL__N_115decode_boundaryINS2_8avx2_opsELj5ELNS0_8geometryE1ELj16EmEEvRKNS0_15basic_placementIKSt4byteEENS0_11index_rangeEPT3_+0x10b>
     3d1:      	popq	%rbx
     3d2:      	popq	%r12
     3d4:      	popq	%r13
     3d6:      	popq	%r14
     3d8:      	popq	%r15
     3da:      	popq	%rbp
     3db:      	vzeroupper
     3de:      	retq
