
build/seam-leaf/avx2/current:   file format elf64-x86-64

Disassembly of section .text:

<_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_>:
                pushq   %rbp
                pushq   %r15
                pushq   %r14
                pushq   %r13
                pushq   %r12
                pushq   %rbx
                subq    $0x108, %rsp            # imm = 0x108
                testq   %rdx, %rdx
                je      0x1ec40 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x440>
                movq    %rdx, %r15
                movq    %rdi, %r12
                leaq    (,%rsi,8), %rax
                movl    %esi, %ecx
                andl    $0xf, %ecx
                movl    %ecx, 0x1c(%rsp)
                xorl    %ecx, %ecx
                xorl    %ebp, %ebp
                xorl    %edi, %edi
                movq    %r9, 0x10(%rsp)
                movq    %rdx, 0x28(%rsp)
                movq    %rsi, 0x20(%rsp)
                movq    %r12, (%rsp)
                jmp     0x1e906 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x106>
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopl    (%rax)
                movq    0x8(%rsp), %r13
                movq    %rbp, %rcx
                vmovdqu 0x60(%rsp), %ymm1
                vextracti128    $0x1, %ymm1, %xmm0
                vpaddq  %xmm0, %xmm1, %xmm0
                vpshufd $0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
                vpaddq  %xmm1, %xmm0, %xmm0
                vmovq   %xmm0, %rdx
                vmovdqu 0x40(%rsp), %ymm1
                vextracti128    $0x1, %ymm1, %xmm0
                vpaddq  %xmm0, %xmm1, %xmm0
                vpshufd $0xee, %xmm0, %xmm1     # xmm1 = xmm0[2,3,2,3]
                vpaddq  %xmm1, %xmm0, %xmm0
                vmovq   %xmm0, %rbp
                movq    %rbp, %rdi
                shrq    $0x20, %rdi
                shlq    $0x20, %rbp
                addq    %rdx, %rbp
                adcq    $0x0, %rdi
                addq    %rcx, %rbp
                adcq    %r13, %rdi
                movq    0x30(%rsp), %rcx
                addq    $0x100000, %rcx         # imm = 0x100000
                movq    0x38(%rsp), %rax
                addq    $0x800000, %rax         # imm = 0x800000
                movq    0x28(%rsp), %r15
                cmpq    %r15, %rcx
                movq    0x20(%rsp), %rsi
                jae     0x1ec44 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x444>
                movq    %rdi, 0x8(%rsp)
                subq    %rcx, %r15
                cmpq    $0x100000, %r15         # imm = 0x100000
                movl    $0x100000, %edx         # imm = 0x100000
                cmovaeq %rdx, %r15
                movq    %rcx, 0x30(%rsp)
                leaq    (%rcx,%rsi), %r14
                movl    0x1c(%rsp), %ebx
                movq    %rax, 0x38(%rsp)
                movq    %rax, %r12
                testb   $0xf, %sil
                je      0x1e9ac <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x1ac>
                nopl    (%rax)
                movq    (%rsp), %rax
                movq    (%rax), %rax
                movq    %r12, %rcx
                andq    $-0x40, %rcx
                addq    (%rax), %rcx
                movq    %rbp, 0x40(%rsp)
                movq    %r15, %r13
                movl    %r12d, %eax
                andl    $0x38, %eax
                movq    (%rax,%rcx), %r15
                movq    %r14, %rdi
                andq    $-0x10, %rdi
                movq    0x10(%rsp), %rax
                vzeroupper
                callq   *(%rax)
                movzwl  %ax, %eax
                btl     %ebx, %eax
                movl    $0x0, %ebp
                cmovbq  %r15, %rbp
                addq    0x40(%rsp), %rbp
                adcq    $0x0, 0x8(%rsp)
                incq    %r14
                leaq    -0x1(%r13), %r15
                cmpq    $0x1, %r13
                je      0x1e9ac <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x1ac>
                movl    %r14d, %eax
                andl    $0xf, %eax
                addq    $0x8, %r12
                incl    %ebx
                testq   %rax, %rax
                jne     0x1e940 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x140>
                cmpq    $0x10, %r15
                jb      0x1eb80 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x380>
                vxorps  %xmm0, %xmm0, %xmm0
                vxorps  %xmm3, %xmm3, %xmm3
                movq    0x10(%rsp), %rbx
                movq    (%rsp), %r12
                nopw    (%rax,%rax)
                vmovups %ymm3, 0x60(%rsp)
                vmovups %ymm0, 0x40(%rsp)
                movq    (%r12), %rax
                movq    (%rax), %rax
                vmovups (%rax,%r14,8), %xmm0
                vmovups 0x20(%rax,%r14,8), %xmm1
                vinsertf128     $0x1, 0x10(%rax,%r14,8), %ymm0, %ymm0
                vmovups %ymm0, 0x80(%rsp)
                vinsertf128     $0x1, 0x30(%rax,%r14,8), %ymm1, %ymm0
                vmovups %ymm0, 0xa0(%rsp)
                vmovups 0x40(%rax,%r14,8), %xmm0
                vinsertf128     $0x1, 0x50(%rax,%r14,8), %ymm0, %ymm0
                vmovups %ymm0, 0xc0(%rsp)
                vmovups 0x60(%rax,%r14,8), %xmm0
                vinsertf128     $0x1, 0x70(%rax,%r14,8), %ymm0, %ymm0
                vmovups %ymm0, 0xe0(%rsp)
                movq    %r14, %rdi
                vzeroupper
                callq   *(%rbx)
                movzwl  %ax, %eax
                vmovq   %rax, %xmm0
                vpbroadcastq    %xmm0, %ymm0
                vmovdqa 0x1661(%rip), %ymm6     # 0x200c0 <_IO_stdin_used+0xc0>
                vpsllvq %ymm6, %ymm0, %ymm0
                vpxor   %xmm7, %xmm7, %xmm7
                vpcmpgtq        %ymm0, %ymm7, %ymm0
                vpand   0x80(%rsp), %ymm0, %ymm0
                vpblendd        $0xaa, %ymm7, %ymm0, %ymm1 # ymm1 = ymm0[0],ymm7[1],ymm0[2],ymm7[3],ymm0[4],ymm7[5],ymm0[6],ymm7[7]
                vpaddq  0x60(%rsp), %ymm1, %ymm1
                vpsrlq  $0x20, %ymm0, %ymm0
                vpaddq  0x40(%rsp), %ymm0, %ymm0
                movl    %eax, %ecx
                shrl    $0x4, %ecx
                vmovq   %rcx, %xmm2
                vpbroadcastq    %xmm2, %ymm2
                vpsllvq %ymm6, %ymm2, %ymm2
                vpcmpgtq        %ymm2, %ymm7, %ymm2
                vpand   0xa0(%rsp), %ymm2, %ymm2
                vpblendd        $0xaa, %ymm7, %ymm2, %ymm3 # ymm3 = ymm2[0],ymm7[1],ymm2[2],ymm7[3],ymm2[4],ymm7[5],ymm2[6],ymm7[7]
                vpsrlq  $0x20, %ymm2, %ymm2
                movl    %eax, %ecx
                shrl    $0x8, %ecx
                vmovq   %rcx, %xmm4
                vpbroadcastq    %xmm4, %ymm4
                vpsllvq %ymm6, %ymm4, %ymm4
                vpcmpgtq        %ymm4, %ymm7, %ymm4
                vpand   0xc0(%rsp), %ymm4, %ymm4
                vpblendd        $0xaa, %ymm7, %ymm4, %ymm5 # ymm5 = ymm4[0],ymm7[1],ymm4[2],ymm7[3],ymm4[4],ymm7[5],ymm4[6],ymm7[7]
                vpaddq  %ymm5, %ymm3, %ymm3
                vpaddq  %ymm3, %ymm1, %ymm1
                vpsrlq  $0x20, %ymm4, %ymm3
                vpaddq  %ymm3, %ymm2, %ymm2
                vpaddq  %ymm2, %ymm0, %ymm0
                shrl    $0xc, %eax
                vmovq   %rax, %xmm2
                vpbroadcastq    %xmm2, %ymm2
                vpsllvq %ymm6, %ymm2, %ymm2
                vpcmpgtq        %ymm2, %ymm7, %ymm2
                vpand   0xe0(%rsp), %ymm2, %ymm2
                vpblendd        $0xaa, %ymm7, %ymm2, %ymm3 # ymm3 = ymm2[0],ymm7[1],ymm2[2],ymm7[3],ymm2[4],ymm7[5],ymm2[6],ymm7[7]
                vpaddq  %ymm3, %ymm1, %ymm3
                vpsrlq  $0x20, %ymm2, %ymm1
                vpaddq  %ymm1, %ymm0, %ymm0
                addq    $0x10, %r14
                addq    $-0x10, %r15
                cmpq    $0xf, %r15
                ja      0x1e9d0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x1d0>
                vmovdqu %ymm3, 0x60(%rsp)
                testq   %r15, %r15
                vmovdqu %ymm0, 0x40(%rsp)
                jne     0x1eba1 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x3a1>
                jmp     0x1e880 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x80>
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nop
                vpxor   %xmm3, %xmm3, %xmm3
                vpxor   %xmm0, %xmm0, %xmm0
                movq    (%rsp), %r12
                vmovdqu %ymm3, 0x60(%rsp)
                testq   %r15, %r15
                vmovdqu %ymm0, 0x40(%rsp)
                je      0x1e880 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x80>
                leaq    (,%r14,8), %rbx
                movq    0x8(%rsp), %r13
                nop
                movq    (%r12), %rax
                movq    %rbx, %rcx
                andq    $-0x40, %rcx
                addq    (%rax), %rcx
                movl    %ebx, %eax
                andl    $0x38, %eax
                movq    (%rax,%rcx), %r12
                movq    %r14, %rdi
                andq    $-0x10, %rdi
                movq    0x10(%rsp), %rax
                vzeroupper
                callq   *(%rax)
                movzwl  %ax, %eax
                movl    %r14d, %ecx
                andl    $0xf, %ecx
                btl     %ecx, %eax
                movl    $0x0, %eax
                cmovbq  %r12, %rax
                movq    (%rsp), %r12
                addq    %rax, %rbp
                adcq    $0x0, %r13
                incq    %r14
                addq    $0x8, %rbx
                decq    %r15
                jne     0x1ebb0 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x3b0>
                jmp     0x1e885 <_ZN4ikea10seriespack6author20exact_halves_regionsILb0ENS1_16value_expressionILj64ELj0ENS1_18payload_expressionILj64ENS1_4leafINS0_12dense_sourceINS0_6layoutILj64ELNS0_8geometryE0ELj0EEEEELNS1_10field_kindE0EEENS5_ISA_LSB_1EEEEENS5_ISA_LSB_2EEENS5_ISA_LSB_3EEEEERKPFtmEEEoRKT0_mmPKhjOT1_+0x85>
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                nopw    %cs:(%rax,%rax)
                xorl    %ebp, %ebp
                xorl    %edi, %edi
                movq    %rbp, %rax
                movq    %rdi, %rdx
                addq    $0x108, %rsp            # imm = 0x108
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
                nopl    (%rax)
