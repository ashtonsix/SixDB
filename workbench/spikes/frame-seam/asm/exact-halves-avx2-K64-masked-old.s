
build/seam-leaf/avx2/old:       file format elf64-x86-64

Disassembly of section .text:

<_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_>:
                pushq   %rbx
                subq    $0x80, %rsp
                movq    %rdi, %rbx
                vmovaps (%rdx), %ymm0
                vmovups %ymm0, (%rsp)
                vmovaps 0x20(%rdx), %ymm0
                vmovups %ymm0, 0x20(%rsp)
                vmovaps 0x40(%rdx), %ymm0
                vmovups %ymm0, 0x40(%rsp)
                vmovaps 0x60(%rdx), %ymm0
                vmovups %ymm0, 0x60(%rsp)
                movq    (%rdi), %rax
                movq    %rsi, %rdi
                vzeroupper
                callq   *(%rax)
                vmovdqu (%rsp), %ymm0
                movzwl  %ax, %edx
                movq    0x8(%rbx), %rcx
                testb   $0x1, %dl
                jne     0x19900 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x140>
                testb   $0x2, %dl
                jne     0x19916 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x156>
                vextracti128    $0x1, %ymm0, %xmm0
                testb   $0x4, %dl
                vmovdqu 0x60(%rsp), %ymm1
                jne     0x19939 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x179>
                testb   $0x8, %dl
                jne     0x1994f <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x18f>
                testb   $0x10, %dl
                vmovdqu 0x20(%rsp), %ymm0
                jne     0x1996c <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x1ac>
                testb   $0x20, %dl
                jne     0x19982 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x1c2>
                vextracti128    $0x1, %ymm0, %xmm0
                testb   $0x40, %dl
                jne     0x1999f <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x1df>
                testb   %dl, %dl
                js      0x199b4 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x1f4>
                testl   $0x100, %edx            # imm = 0x100
                vmovdqu 0x40(%rsp), %ymm0
                jne     0x199d4 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x214>
                testl   $0x200, %edx            # imm = 0x200
                jne     0x199ed <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x22d>
                vextracti128    $0x1, %ymm0, %xmm0
                testl   $0x400, %edx            # imm = 0x400
                jne     0x19a0d <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x24d>
                testl   $0x800, %edx            # imm = 0x800
                jne     0x19a26 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x266>
                testl   $0x1000, %edx           # imm = 0x1000
                jne     0x19a40 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x280>
                testl   $0x2000, %edx           # imm = 0x2000
                jne     0x19a59 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x299>
                testl   $0x4000, %edx           # imm = 0x4000
                jne     0x19a73 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x2b3>
                testw   %ax, %ax
                js      0x19a8f <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x2cf>
                addq    $0x80, %rsp
                popq    %rbx
                vzeroupper
                retq
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nop
                vmovq   %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testb   $0x2, %dl
                je      0x1981e <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x5e>
                vpextrq $0x1, %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                vextracti128    $0x1, %ymm0, %xmm0
                testb   $0x4, %dl
                vmovdqu 0x60(%rsp), %ymm1
                je      0x19833 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x73>
                vmovq   %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testb   $0x8, %dl
                je      0x1983c <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x7c>
                vpextrq $0x1, %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testb   $0x10, %dl
                vmovdqu 0x20(%rsp), %ymm0
                je      0x1984b <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x8b>
                vmovq   %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testb   $0x20, %dl
                je      0x19854 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x94>
                vpextrq $0x1, %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                vextracti128    $0x1, %ymm0, %xmm0
                testb   $0x40, %dl
                je      0x19863 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xa3>
                vmovq   %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testb   %dl, %dl
                jns     0x1986b <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xab>
                vpextrq $0x1, %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testl   $0x100, %edx            # imm = 0x100
                vmovdqu 0x40(%rsp), %ymm0
                je      0x1987d <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xbd>
                vmovq   %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testl   $0x200, %edx            # imm = 0x200
                je      0x19889 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xc9>
                vpextrq $0x1, %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                vextracti128    $0x1, %ymm0, %xmm0
                testl   $0x400, %edx            # imm = 0x400
                je      0x1989b <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xdb>
                vmovq   %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testl   $0x800, %edx            # imm = 0x800
                je      0x198a7 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xe7>
                vpextrq $0x1, %xmm0, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testl   $0x1000, %edx           # imm = 0x1000
                je      0x198b3 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xf3>
                vmovq   %xmm1, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testl   $0x2000, %edx           # imm = 0x2000
                je      0x198bf <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0xff>
                vpextrq $0x1, %xmm1, %rsi
                addq    %rsi, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testl   $0x4000, %edx           # imm = 0x4000
                je      0x198cb <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x10b>
                vextracti128    $0x1, %ymm1, %xmm0
                vmovq   %xmm0, %rdx
                addq    %rdx, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                testw   %ax, %ax
                jns     0x198d4 <_ZZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_ENKUlmRKT_E_clINS0_3x866valuesILj64ELj16EEEEEDamSV_+0x114>
                vextracti128    $0x1, %ymm1, %xmm0
                vpextrq $0x1, %xmm0, %rax
                addq    %rax, (%rcx)
                adcq    $0x0, 0x8(%rcx)
                addq    $0x80, %rsp
                popq    %rbx
                vzeroupper
                retq
                nopw    %cs:(%rax,%rax)
                nopl    (%rax)
