
build/seam-leaf/avx2/old:       file format elf64-x86-64

Disassembly of section .text:

<_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_>:
                pushq   %rbp
                pushq   %r15
                pushq   %r14
                pushq   %r13
                pushq   %r12
                pushq   %rbx
                movq    %rdx, %r9
                testq   %rdx, %rdx
                sete    %dl
                testb   $0xf, %sil
                sete    %r10b
                xorl    %eax, %eax
                orb     %dl, %r10b
                jne     0x13d40 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0xc0>
                movq    (%rdi), %rax
                movq    0x8(%rdi), %rdx
                movq    (%rax), %r11
                movq    (%rdx), %rbx
                leal    -0x1(%r8), %r14d
                xorl    %eax, %eax
                xorl    %r10d, %r10d
                nopl    (%rax)
                movq    %r10, %r15
                movq    %rax, %r12
                movl    %esi, %eax
                andl    $0x7, %eax
                movq    %rsi, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %r10
                leaq    (%r11,%r10), %r13
                leaq    (%rax,%rax,2), %rbp
                movl    (%r13,%rbp,2), %edx
                movl    0x2(%r13,%rbp,2), %r13d
                movzbl  0x30(%rbx,%r10), %r10d
                shrxq   %rax, %r10, %rax
                andl    $0x1, %eax
                shlq    $0x11, %r13
                addq    %rdx, %rdx
                orq     %r13, %rdx
                orq     %rax, %rdx
                movl    %esi, %eax
                andl    %r14d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                addq    %r12, %rax
                adcq    %r15, %r10
                incq    %rsi
                cmpq    $0x1, %r9
                leaq    -0x1(%r9), %r9
                je      0x13d43 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0xc3>
                movl    %esi, %edx
                andl    $0xf, %edx
                jne     0x13cc0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x40>
                jmp     0x13d43 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0xc3>
                nopw    %cs:(%rax,%rax)
                nopl    (%rax)
                xorl    %r10d, %r10d
                cmpq    $0x10, %r9
                jb      0x14067 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x3e7>
                movabsq $0x101010101010101, %r11 # imm = 0x101010101010101
                movq    (%rdi), %rdx
                movq    0x8(%rdi), %r14
                movq    (%rdx), %rbx
                movq    (%r14), %r14
                leal    -0x1(%r8), %r15d
                vbroadcasti128  0x738f(%rip), %ymm0 # ymm0 = mem[0,1,0,1]
                                                # 0x1b100 <_IO_stdin_used+0x100>
                vmovdqa 0x7307(%rip), %ymm1     # 0x1b080 <_IO_stdin_used+0x80>
                vpbroadcastq    0x74fe(%rip), %xmm2 # 0x1b280 <_IO_stdin_used+0x280>
                vpbroadcastd    0x7279(%rip), %xmm3 # 0x1b004 <_IO_stdin_used+0x4>
                nopl    (%rax,%rax)
                movq    %rsi, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %rdx
                vmovdqu (%rbx,%rdx), %xmm4
                vmovdqu 0x18(%rbx,%rdx), %xmm5
                vinserti128     $0x1, 0xc(%rbx,%rdx), %ymm4, %ymm8
                vinserti128     $0x1, 0x20(%rbx,%rdx), %ymm5, %ymm7
                leaq    0xa(%rsi), %r12
                shrq    $0x3, %r12
                imulq   $0x31, %r12, %r12
                leaq    0x8(%rsi), %r13
                shrq    $0x3, %r13
                imulq   $0x31, %r13, %r13
                vmovdqu (%rbx,%r13), %xmm4
                vinserti128     $0x1, 0xc(%rbx,%r12), %ymm4, %ymm6
                leaq    0xe(%rsi), %r12
                shrq    $0x3, %r12
                imulq   $0x31, %r12, %r12
                leaq    0xc(%rsi), %r13
                shrq    $0x3, %r13
                imulq   $0x31, %r13, %r13
                vmovdqu 0x18(%rbx,%r13), %xmm4
                vinserti128     $0x1, 0x20(%rbx,%r12), %ymm4, %ymm4
                vpshufb %ymm0, %ymm8, %ymm8
                movzbl  0x61(%r14,%rdx), %r12d
                imulq   %r11, %r12
                movzbl  0x30(%r14,%rdx), %edx
                imulq   %r11, %rdx
                vmovq   %r12, %xmm5
                vmovq   %rdx, %xmm9
                vpunpcklqdq     %xmm5, %xmm9, %xmm5 # xmm5 = xmm9[0],xmm5[0]
                vpand   %xmm2, %xmm5, %xmm5
                vpcmpeqb        %xmm2, %xmm5, %xmm5
                vpand   %xmm3, %xmm5, %xmm5
                vpmovzxbq       %xmm5, %ymm9    # ymm9 = xmm5[0],zero,zero,zero,zero,zero,zero,zero,xmm5[1],zero,zero,zero,zero,zero,zero,zero,xmm5[2],zero,zero,zero,zero,zero,zero,zero,xmm5[3],zero,zero,zero,zero,zero,zero,zero
                vpaddq  %ymm8, %ymm8, %ymm8
                vpor    %ymm9, %ymm8, %ymm8
                vmovq   %xmm8, %rdx
                movl    %esi, %r12d
                andl    %r15d, %r12d
                movzbl  (%rcx,%r12), %r12d
                mulxq   %r12, %r12, %r13
                vpshufb %ymm1, %ymm7, %ymm7
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm8, %rdx
                leal    0x1(%rsi), %eax
                andl    %r15d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                vpshufd $0x55, %xmm5, %xmm9     # xmm9 = xmm5[1,1,1,1]
                addq    %r12, %rax
                adcq    %r13, %r10
                vextracti128    $0x1, %ymm8, %xmm8
                vmovq   %xmm8, %rdx
                leal    0x2(%rsi), %r12d
                andl    %r15d, %r12d
                movzbl  (%rcx,%r12), %r12d
                mulxq   %r12, %r12, %r13
                vpmovzxbq       %xmm9, %ymm9    # ymm9 = xmm9[0],zero,zero,zero,zero,zero,zero,zero,xmm9[1],zero,zero,zero,zero,zero,zero,zero,xmm9[2],zero,zero,zero,zero,zero,zero,zero,xmm9[3],zero,zero,zero,zero,zero,zero,zero
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm8, %rdx
                leal    0x3(%rsi), %eax
                andl    %r15d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                vpaddq  %ymm7, %ymm7, %ymm7
                addq    %r12, %rax
                adcq    %r13, %r10
                vpor    %ymm7, %ymm9, %ymm7
                vmovq   %xmm7, %rdx
                leal    0x4(%rsi), %r12d
                andl    %r15d, %r12d
                movzbl  (%rcx,%r12), %r12d
                mulxq   %r12, %r12, %r13
                vpshufb %ymm0, %ymm6, %ymm6
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm7, %rdx
                leal    0x5(%rsi), %eax
                andl    %r15d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                vpshufd $0xee, %xmm5, %xmm8     # xmm8 = xmm5[2,3,2,3]
                addq    %r12, %rax
                adcq    %r13, %r10
                vextracti128    $0x1, %ymm7, %xmm7
                vmovq   %xmm7, %rdx
                leal    0x6(%rsi), %r12d
                andl    %r15d, %r12d
                movzbl  (%rcx,%r12), %r12d
                mulxq   %r12, %r12, %r13
                vpmovzxbq       %xmm8, %ymm8    # ymm8 = xmm8[0],zero,zero,zero,zero,zero,zero,zero,xmm8[1],zero,zero,zero,zero,zero,zero,zero,xmm8[2],zero,zero,zero,zero,zero,zero,zero,xmm8[3],zero,zero,zero,zero,zero,zero,zero
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm7, %rdx
                leal    0x7(%rsi), %eax
                andl    %r15d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                vpaddq  %ymm6, %ymm6, %ymm6
                addq    %r12, %rax
                adcq    %r13, %r10
                vpor    %ymm6, %ymm8, %ymm6
                vmovq   %xmm6, %rdx
                leal    0x8(%rsi), %r12d
                andl    %r15d, %r12d
                movzbl  (%rcx,%r12), %r12d
                mulxq   %r12, %r12, %r13
                vpshufb %ymm1, %ymm4, %ymm4
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm6, %rdx
                leal    0x9(%rsi), %eax
                andl    %r15d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                vpshufd $0xff, %xmm5, %xmm5     # xmm5 = xmm5[3,3,3,3]
                addq    %r12, %rax
                adcq    %r13, %r10
                vextracti128    $0x1, %ymm6, %xmm6
                vmovq   %xmm6, %rdx
                leal    0xa(%rsi), %r12d
                andl    %r15d, %r12d
                movzbl  (%rcx,%r12), %r12d
                mulxq   %r12, %r12, %r13
                vpmovzxbq       %xmm5, %ymm5    # ymm5 = xmm5[0],zero,zero,zero,zero,zero,zero,zero,xmm5[1],zero,zero,zero,zero,zero,zero,zero,xmm5[2],zero,zero,zero,zero,zero,zero,zero,xmm5[3],zero,zero,zero,zero,zero,zero,zero
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm6, %rax
                leal    0xb(%rsi), %edx
                andl    %r15d, %edx
                movzbl  (%rcx,%rdx), %edx
                mulxq   %rax, %rax, %r10
                vpaddq  %ymm4, %ymm4, %ymm4
                addq    %r12, %rax
                adcq    %r13, %r10
                vpor    %ymm5, %ymm4, %ymm4
                vmovq   %xmm4, %r12
                leal    0xc(%rsi), %edx
                andl    %r15d, %edx
                movzbl  (%rcx,%rdx), %edx
                mulxq   %r12, %r12, %r13
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm4, %rax
                leal    0xd(%rsi), %edx
                andl    %r15d, %edx
                movzbl  (%rcx,%rdx), %edx
                mulxq   %rax, %rax, %r10
                addq    %r12, %rax
                adcq    %r13, %r10
                vextracti128    $0x1, %ymm4, %xmm4
                vmovq   %xmm4, %r12
                leal    0xe(%rsi), %edx
                andl    %r15d, %edx
                movzbl  (%rcx,%rdx), %edx
                mulxq   %r12, %r12, %r13
                addq    %rax, %r12
                adcq    %r10, %r13
                vpextrq $0x1, %xmm4, %rax
                leal    0xf(%rsi), %edx
                andl    %r15d, %edx
                movzbl  (%rcx,%rdx), %edx
                mulxq   %rax, %rax, %r10
                addq    %r12, %rax
                adcq    %r13, %r10
                addq    $0x10, %rsi
                addq    $-0x10, %r9
                cmpq    $0xf, %r9
                ja      0x13d90 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x110>
                testq   %r9, %r9
                je      0x141e8 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x568>
                movq    (%rdi), %rdx
                movq    0x8(%rdi), %r11
                movq    (%rdx), %rdi
                movq    (%r11), %r11
                decl    %r8d
                testb   $0x1, %r9b
                jne     0x140c0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x440>
                movq    %r9, %rbx
                cmpq    $0x1, %r9
                jne     0x14130 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x4b0>
                jmp     0x141e8 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x568>
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                movl    %esi, %ebx
                andl    $0x7, %ebx
                movq    %rsi, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %r14
                leaq    (%rdi,%r14), %r15
                leaq    (%rbx,%rbx,2), %r12
                movl    (%r15,%r12,2), %edx
                movl    0x2(%r15,%r12,2), %r15d
                movzbl  0x30(%r11,%r14), %r14d
                shrxq   %rbx, %r14, %rbx
                andl    $0x1, %ebx
                shlq    $0x11, %r15
                addq    %rdx, %rdx
                orq     %r15, %rdx
                orq     %rbx, %rdx
                movl    %r8d, %ebx
                andl    %esi, %ebx
                movzbl  (%rcx,%rbx), %ebx
                mulxq   %rbx, %rbx, %rdx
                addq    %rbx, %rax
                adcq    %rdx, %r10
                incq    %rsi
                leaq    -0x1(%r9), %rbx
                cmpq    $0x1, %r9
                je      0x141e8 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x568>
                nopw    %cs:(%rax,%rax)
                movl    %esi, %r9d
                andl    $0x7, %r9d
                movq    %rsi, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %r14
                leaq    (%rdi,%r14), %r15
                leaq    (%r9,%r9,2), %r12
                movl    (%r15,%r12,2), %edx
                movl    0x2(%r15,%r12,2), %r15d
                movzbl  0x30(%r11,%r14), %r14d
                shrxq   %r9, %r14, %r9
                andl    $0x1, %r9d
                shlq    $0x11, %r15
                addq    %rdx, %rdx
                orq     %r15, %rdx
                orq     %r9, %rdx
                movl    %esi, %r9d
                andl    %r8d, %r9d
                movzbl  (%rcx,%r9), %r9d
                mulxq   %r9, %r14, %r9
                addq    %rax, %r14
                adcq    %r10, %r9
                leaq    0x1(%rsi), %rax
                movl    %eax, %r10d
                andl    $0x7, %r10d
                movq    %rax, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %r15
                leaq    (%rdi,%r15), %r12
                leaq    (%r10,%r10,2), %r13
                movl    (%r12,%r13,2), %edx
                movl    0x2(%r12,%r13,2), %r12d
                movzbl  0x30(%r11,%r15), %r15d
                shrxq   %r10, %r15, %r10
                andl    $0x1, %r10d
                shlq    $0x11, %r12
                addq    %rdx, %rdx
                orq     %r12, %rdx
                orq     %r10, %rdx
                andl    %r8d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %r10
                addq    %r14, %rax
                adcq    %r9, %r10
                addq    $0x2, %rsi
                addq    $-0x2, %rbx
                jne     0x14130 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x4b0>
                movq    %r10, %rdx
                popq    %rbx
                popq    %r12
                popq    %r13
                popq    %r14
                popq    %r15
                popq    %rbp
                vzeroupper
                retq
                nopl    (%rax)
