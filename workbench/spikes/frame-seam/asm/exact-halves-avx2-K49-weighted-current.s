
build/seam-leaf/avx2/current:   file format elf64-x86-64

Disassembly of section .text:

<_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_>:
                pushq   %rbp
                pushq   %r15
                pushq   %r14
                pushq   %r13
                pushq   %r12
                pushq   %rbx
                movq    %rsi, -0x10(%rsp)
                testq   %rdx, %rdx
                je      0x19340 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x500>
                movabsq $0x101010101010101, %r10 # imm = 0x101010101010101
                movq    (%rdi), %rax
                movq    %rax, -0x20(%rsp)
                movq    0x8(%rdi), %rax
                movq    %rax, -0x28(%rsp)
                decl    %r8d
                xorl    %esi, %esi
                vbroadcasti128  0x733f(%rip), %ymm0 # ymm0 = mem[0,1,0,1]
                                                # 0x201c0 <_IO_stdin_used+0x1c0>
                vmovdqa 0x7297(%rip), %ymm1     # 0x20120 <_IO_stdin_used+0x120>
                vpbroadcastq    0x747e(%rip), %xmm2 # 0x20310 <_IO_stdin_used+0x310>
                vpbroadcastd    0x7189(%rip), %xmm3 # 0x20024 <_IO_stdin_used+0x24>
                xorl    %eax, %eax
                xorl    %edi, %edi
                movq    %rdx, -0x18(%rsp)
                jmp     0x18f28 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0xe8>
                nopw    %cs:(%rax,%rax)
                nopl    (%rax,%rax)
                movq    %rdi, %rdx
                movq    %rax, %rsi
                vextracti128    $0x1, %ymm5, %xmm6
                vpaddq  %xmm6, %xmm5, %xmm5
                vpshufd $0xee, %xmm5, %xmm6     # xmm6 = xmm5[2,3,2,3]
                vpaddq  %xmm6, %xmm5, %xmm5
                vmovq   %xmm5, %r9
                vextracti128    $0x1, %ymm4, %xmm5
                vpaddq  %xmm5, %xmm4, %xmm4
                vpshufd $0xee, %xmm4, %xmm5     # xmm5 = xmm4[2,3,2,3]
                vpaddq  %xmm5, %xmm4, %xmm4
                vmovq   %xmm4, %rax
                movq    %rax, %rdi
                shrq    $0x20, %rdi
                shlq    $0x20, %rax
                addq    %r9, %rax
                adcq    $0x0, %rdi
                addq    %rsi, %rax
                adcq    %rdx, %rdi
                movq    -0x8(%rsp), %rsi
                addq    $0x100000, %rsi         # imm = 0x100000
                movq    -0x18(%rsp), %rdx
                cmpq    %rdx, %rsi
                jae     0x19344 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x504>
                movq    %rdx, %r13
                subq    %rsi, %r13
                cmpq    $0x100000, %r13         # imm = 0x100000
                movl    $0x100000, %edx         # imm = 0x100000
                cmovaeq %rdx, %r13
                movq    -0x10(%rsp), %rdx
                movq    %rsi, -0x8(%rsp)
                leaq    (%rsi,%rdx), %r12
                testb   $0xf, %dl
                je      0x18fdd <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x19d>
                movq    -0x20(%rsp), %rdx
                movq    (%rdx), %r11
                movq    -0x28(%rsp), %rdx
                movq    (%rdx), %r15
                nopw    %cs:(%rax,%rax)
                movq    %rdi, %rbp
                movq    %rax, %rbx
                movl    %r12d, %eax
                andl    $0x7, %eax
                movq    %r12, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %rdi
                leaq    (%r11,%rdi), %r9
                leaq    (%rax,%rax,2), %r14
                movl    (%r9,%r14,2), %edx
                movl    0x2(%r9,%r14,2), %r9d
                movzbl  0x30(%r15,%rdi), %edi
                shrxq   %rax, %rdi, %rax
                andl    $0x1, %eax
                shlq    $0x11, %r9
                addq    %rdx, %rdx
                orq     %r9, %rdx
                orq     %rax, %rdx
                movl    %r8d, %eax
                andl    %r12d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %rdi
                addq    %rbx, %rax
                adcq    %rbp, %rdi
                incq    %r12
                cmpq    $0x1, %r13
                leaq    -0x1(%r13), %r13
                je      0x18fdd <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x19d>
                movl    %r12d, %edx
                andl    $0xf, %edx
                jne     0x18f70 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x130>
                vpxor   %xmm5, %xmm5, %xmm5
                cmpq    $0x10, %r13
                jb      0x191c0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x380>
                movq    -0x20(%rsp), %rdx
                movq    (%rdx), %rdx
                movq    -0x28(%rsp), %rsi
                movq    (%rsi), %r11
                vpxor   %xmm4, %xmm4, %xmm4
                nop
                movq    %r12, %r9
                shrq    $0x3, %r9
                imulq   $0x31, %r9, %r9
                vmovdqu (%rdx,%r9), %xmm6
                vmovdqu 0x18(%rdx,%r9), %xmm7
                vinserti128     $0x1, 0xc(%rdx,%r9), %ymm6, %ymm6
                vpshufb %ymm0, %ymm6, %ymm8
                vinserti128     $0x1, 0x20(%rdx,%r9), %ymm7, %ymm6
                leaq    0x8(%r12), %rbx
                shrq    $0x3, %rbx
                imulq   $0x31, %rbx, %rbx
                vmovdqu (%rdx,%rbx), %xmm7
                vinserti128     $0x1, 0x3d(%rdx,%r9), %ymm7, %ymm7
                vpshufb %ymm1, %ymm6, %ymm9
                vpshufb %ymm0, %ymm7, %ymm7
                leaq    0xc(%r12), %rbx
                shrq    $0x3, %rbx
                imulq   $0x31, %rbx, %rbx
                vmovdqu 0x18(%rdx,%rbx), %xmm6
                vinserti128     $0x1, 0x51(%rdx,%r9), %ymm6, %ymm6
                vpshufb %ymm1, %ymm6, %ymm6
                movzbl  0x61(%r11,%r9), %ebx
                imulq   %r10, %rbx
                movzbl  0x30(%r11,%r9), %r9d
                imulq   %r10, %r9
                vmovq   %rbx, %xmm10
                vmovq   %r9, %xmm11
                vpunpcklqdq     %xmm10, %xmm11, %xmm10 # xmm10 = xmm11[0],xmm10[0]
                vpand   %xmm2, %xmm10, %xmm10
                vpcmpeqb        %xmm2, %xmm10, %xmm10
                vpand   %xmm3, %xmm10, %xmm10
                vpmovzxbq       %xmm10, %ymm11  # ymm11 = xmm10[0],zero,zero,zero,zero,zero,zero,zero,xmm10[1],zero,zero,zero,zero,zero,zero,zero,xmm10[2],zero,zero,zero,zero,zero,zero,zero,xmm10[3],zero,zero,zero,zero,zero,zero,zero
                vpshufd $0x55, %xmm10, %xmm12   # xmm12 = xmm10[1,1,1,1]
                vpmovzxbq       %xmm12, %ymm12  # ymm12 = xmm12[0],zero,zero,zero,zero,zero,zero,zero,xmm12[1],zero,zero,zero,zero,zero,zero,zero,xmm12[2],zero,zero,zero,zero,zero,zero,zero,xmm12[3],zero,zero,zero,zero,zero,zero,zero
                vpshufd $0xee, %xmm10, %xmm13   # xmm13 = xmm10[2,3,2,3]
                vpmovzxbq       %xmm13, %ymm13  # ymm13 = xmm13[0],zero,zero,zero,zero,zero,zero,zero,xmm13[1],zero,zero,zero,zero,zero,zero,zero,xmm13[2],zero,zero,zero,zero,zero,zero,zero,xmm13[3],zero,zero,zero,zero,zero,zero,zero
                vpshufd $0xff, %xmm10, %xmm10   # xmm10 = xmm10[3,3,3,3]
                vpmovzxbq       %xmm10, %ymm10  # ymm10 = xmm10[0],zero,zero,zero,zero,zero,zero,zero,xmm10[1],zero,zero,zero,zero,zero,zero,zero,xmm10[2],zero,zero,zero,zero,zero,zero,zero,xmm10[3],zero,zero,zero,zero,zero,zero,zero
                vpaddq  %ymm8, %ymm8, %ymm14
                vpor    %ymm11, %ymm14, %ymm11
                vpaddq  %ymm9, %ymm9, %ymm14
                vpor    %ymm12, %ymm14, %ymm12
                vpaddq  %ymm7, %ymm7, %ymm14
                vpor    %ymm13, %ymm14, %ymm13
                vpaddq  %ymm6, %ymm6, %ymm14
                movl    %r12d, %r9d
                andl    %r8d, %r9d
                vpmovzxbq       (%rcx,%r9), %ymm15
                vpor    %ymm10, %ymm14, %ymm10
                vpmuludq        %ymm15, %ymm11, %ymm11
                vpaddq  %ymm5, %ymm11, %ymm5
                vpsrlq  $0x1f, %ymm8, %ymm8
                vpmuldq %ymm15, %ymm8, %ymm8
                vpaddq  %ymm4, %ymm8, %ymm4
                leal    0x4(%r12), %r9d
                andl    %r8d, %r9d
                vpmovzxbq       (%rcx,%r9), %ymm8
                vpmuludq        %ymm8, %ymm12, %ymm11
                vpaddq  %ymm5, %ymm11, %ymm5
                vpsrlq  $0x1f, %ymm9, %ymm9
                vpmuldq %ymm8, %ymm9, %ymm8
                vpaddq  %ymm4, %ymm8, %ymm4
                leal    0x8(%r12), %r9d
                andl    %r8d, %r9d
                vpmovzxbq       (%rcx,%r9), %ymm8
                vpmuludq        %ymm8, %ymm13, %ymm9
                vpsrlq  $0x1f, %ymm7, %ymm7
                vpmuldq %ymm8, %ymm7, %ymm7
                leal    0xc(%r12), %r9d
                andl    %r8d, %r9d
                vpmovzxbq       (%rcx,%r9), %ymm8
                vpmuludq        %ymm8, %ymm10, %ymm10
                vpaddq  %ymm10, %ymm9, %ymm9
                vpaddq  %ymm5, %ymm9, %ymm5
                vpsrlq  $0x1f, %ymm6, %ymm6
                vpmuldq %ymm8, %ymm6, %ymm6
                vpaddq  %ymm6, %ymm7, %ymm6
                vpaddq  %ymm6, %ymm4, %ymm4
                addq    $0x10, %r12
                addq    $-0x10, %r13
                cmpq    $0xf, %r13
                ja      0x19000 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x1c0>
                testq   %r13, %r13
                jne     0x191cd <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x38d>
                jmp     0x18ec0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x80>
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopl    (%rax,%rax)
                vpxor   %xmm4, %xmm4, %xmm4
                testq   %r13, %r13
                je      0x18ec0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x80>
                movq    -0x20(%rsp), %rdx
                movq    (%rdx), %rbp
                movq    -0x28(%rsp), %rdx
                movq    (%rdx), %r15
                testb   $0x1, %r13b
                jne     0x19200 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x3c0>
                movq    %r13, %r11
                cmpq    $0x1, %r13
                jne     0x19270 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x430>
                jmp     0x18ec0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x80>
                nopw    %cs:(%rax,%rax)
                movl    %r12d, %r9d
                andl    $0x7, %r9d
                movq    %r12, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %r11
                leaq    (%r11,%rbp), %rbx
                leaq    (%r9,%r9,2), %r14
                movl    (%rbx,%r14,2), %edx
                movl    0x2(%rbx,%r14,2), %ebx
                movzbl  0x30(%r15,%r11), %r11d
                shrxq   %r9, %r11, %r9
                andl    $0x1, %r9d
                shlq    $0x11, %rbx
                addq    %rdx, %rdx
                orq     %rbx, %rdx
                orq     %r9, %rdx
                movl    %r8d, %r9d
                andl    %r12d, %r9d
                movzbl  (%rcx,%r9), %r9d
                mulxq   %r9, %rdx, %r9
                addq    %rdx, %rax
                adcq    %r9, %rdi
                incq    %r12
                leaq    -0x1(%r13), %r11
                cmpq    $0x1, %r13
                je      0x18ec0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x80>
                nopw    %cs:(%rax,%rax)
                movl    %r12d, %r9d
                andl    $0x7, %r9d
                movq    %r12, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %rbx
                leaq    (%rbx,%rbp), %r14
                leaq    (%r9,%r9,2), %r13
                movl    (%r14,%r13,2), %edx
                movl    0x2(%r14,%r13,2), %r14d
                movzbl  0x30(%r15,%rbx), %ebx
                shrxq   %r9, %rbx, %r9
                andl    $0x1, %r9d
                shlq    $0x11, %r14
                addq    %rdx, %rdx
                orq     %r14, %rdx
                orq     %r9, %rdx
                movl    %r12d, %r9d
                andl    %r8d, %r9d
                movzbl  (%rcx,%r9), %r9d
                mulxq   %r9, %rbx, %r13
                addq    %rax, %rbx
                adcq    %rdi, %r13
                leaq    0x1(%r12), %rax
                movl    %eax, %edi
                andl    $0x7, %edi
                movq    %rax, %rdx
                shrq    $0x3, %rdx
                imulq   $0x31, %rdx, %r9
                leaq    (%r9,%rbp), %r14
                leaq    (%rdi,%rdi,2), %rsi
                movl    (%r14,%rsi,2), %edx
                movl    0x2(%r14,%rsi,2), %esi
                movzbl  0x30(%r15,%r9), %r9d
                shrxq   %rdi, %r9, %rdi
                andl    $0x1, %edi
                shlq    $0x11, %rsi
                addq    %rdx, %rdx
                orq     %rsi, %rdx
                orq     %rdi, %rdx
                andl    %r8d, %eax
                movzbl  (%rcx,%rax), %eax
                mulxq   %rax, %rax, %rdi
                addq    %rbx, %rax
                adcq    %r13, %rdi
                addq    $0x2, %r12
                addq    $-0x2, %r11
                jne     0x19270 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x430>
                jmp     0x18ec0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb1ENS1_16value_expressionILj49ELj0ENS1_18payload_expressionILj49ENS1_4leafINS0_12dense_sourceINS0_6layoutILj49ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERZZNKS0_6readerISH_E22weighted_sum_uncheckedEmmPKhjENKUlRKT_E_clISH_EEDaSO_EUlmE_EEoRKT0_mmSL_jOT1_+0x80>
                nopw    %cs:(%rax,%rax)
                nopw    (%rax,%rax)
                xorl    %eax, %eax
                xorl    %edi, %edi
                movq    %rdi, %rdx
                popq    %rbx
                popq    %r12
                popq    %r13
                popq    %r14
                popq    %r15
                popq    %rbp
                vzeroupper
                retq
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopl    (%rax)
