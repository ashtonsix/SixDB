Phase resolved from ['./build/hashtable-design-reduction-paired-avx2/./workbench/spikes/hashtable-design/formats.cpp:67', './build/hashtable-design-reduction-paired-avx2/./workbench/spikes/hashtable-design/formats.cpp:154']
Complete timed CFG, address order.
9c8a04: movq %rax, 0x90(%rsp)
9c8a0c: movq 0x70(%rsp), %rax
9c8a11: movq 0x78(%rsp), %rcx
9c8a16: movq $-0x1, %rdx
9c8a1d: movl $0x1c00, %r10d          # imm = 0x1C00
9c8a23: movb $0x28, %r11b
9c8a26: xorl %r9d, %r9d
9c8a29: shlxq %rax, %rdx, %rsi
9c8a2e: movq %rcx, %rdi
9c8a31: movl %eax, %r8d
9c8a34: andl $0x7c0, %r8d            # imm = 0x7C0
9c8a3b: subq %rsi, %rdi
9c8a3e: notq %rsi
9c8a41: addq $0xf, %rdi
9c8a45: jmp 0x9c8a5d <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x210d>
9c8a50: incl %r9d
9c8a53: cmpl $0x10, %r9d
9c8a57: je 0x9c8d2f <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x23df>
9c8a5d: testb $0x3e, %al
9c8a5f: je 0x9c8b60 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2210>
9c8a65: movq %rbx, %r15
9c8a68: xorl %r14d, %r14d
9c8a6b: jmp 0x9c8af3 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x21a3>
9c8a70: andq %rsi, %rdx
9c8a73: movq %rdx, %r13
9c8a76: shlq $0x4, %r13
9c8a7a: prefetcht0 (%rdi,%r13)
9c8a7f: vmovdqu (%rcx,%rdx), %xmm1
9c8a84: vpcmpeqb %xmm1, %xmm0, %xmm2
9c8a88: vpmovmskb %xmm2, %ebp
9c8a8c: testl %ebp, %ebp
9c8a8e: je 0x9c8ab0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2160>
9c8a90: tzcntl %ebp, %r13d
9c8a95: addq %rdx, %r13
9c8a98: andq %rsi, %r13
9c8a9b: shlq $0x4, %r13
9c8a9f: cmpq %rbx, (%rdi,%r13)
9c8aa3: je 0x9c8ad0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2180>
9c8aa5: blsrl %ebp, %ebp
9c8aaa: jne 0x9c8a90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2140>
9c8aac: nopl (%rax)
9c8ab0: vpsignb %xmm1, %xmm1, %xmm1
9c8ab5: vpmovmskb %xmm1, %ebp
9c8ab9: testl %ebp, %ebp
9c8abb: jne 0x9c8b50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2200>
9c8ac1: leaq 0x10(%r12,%rdx), %rdx
9c8ac6: addq $0x10, %r12
9c8aca: jmp 0x9c8a70 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2120>
9c8ad0: addq %rdi, %r13
9c8ad3: movl $0x1, %ebx
9c8ad8: addq 0x8(%r13), %r15
9c8adc: addq %r15, %rbx
9c8adf: addq $0x4, %r14
9c8ae3: movq %rbx, %r15
9c8ae6: cmpq $0x7000, %r14           # imm = 0x7000
9c8aed: je 0x9c8a50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2100>
9c8af3: movq 0x60(%rsp), %rdx
9c8af8: movabsq $0xb86659fd93, %rbx     # imm = 0xB86659FD93
9c8b02: movabsq $0x79d5f9e0de1e8cf5, %r12 # imm = 0x79D5F9E0DE1E8CF5
9c8b0c: movl (%rdx,%r14), %edx
9c8b10: prefetcht2 (%rcx)
9c8b13: addl %r10d, %edx
9c8b16: imulq %rbx, %rdx
9c8b1a: bzhiq %r11, %rdx, %rbx
9c8b1f: movq %rbx, %rdx
9c8b22: xorq %r8, %rdx
9c8b25: mulxq %r12, %r12, %rdx
9c8b2a: xorq %r12, %rdx
9c8b2d: shrq $0x39, %r12
9c8b31: vmovd %r12d, %xmm0
9c8b36: xorl %r12d, %r12d
9c8b39: vpbroadcastb %xmm0, %xmm0
9c8b3e: jmp 0x9c8a70 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2120>
9c8b50: xorl %ebx, %ebx
9c8b52: jmp 0x9c8adc <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x218c>
9c8b60: cmpq $0x8000, %rax           # imm = 0x8000
9c8b66: jb 0x9c8a50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2100>
9c8b6c: movq (%rcx), %rdx
9c8b6f: movl $0x1c, %r14d
9c8b75: jmp 0x9c8b9d <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x224d>
9c8b80: addq 0x8(%rcx), %r15
9c8b84: movl $0x1, %ebx
9c8b89: addq %r15, %rbx
9c8b8c: addq $0x20, %r14
9c8b90: cmpq $0x701c, %r14           # imm = 0x701C
9c8b97: je 0x9c8a50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2100>
9c8b9d: movq 0x60(%rsp), %r15
9c8ba2: movabsq $0xb86659fd93, %r12     # imm = 0xB86659FD93
9c8bac: movl -0x1c(%r15,%r14), %r15d
9c8bb1: addl %r10d, %r15d
9c8bb4: imulq %r12, %r15
9c8bb8: bzhiq %r11, %r15, %r15
9c8bbd: cmpq %r15, %rdx
9c8bc0: jne 0x9c8bd0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2280>
9c8bc2: addq 0x8(%rcx), %rbx
9c8bc6: movl $0x1, %r15d
9c8bcc: jmp 0x9c8bd3 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2283>
9c8bd0: xorl %r15d, %r15d
9c8bd3: addq %rbx, %r15
9c8bd6: movq 0x60(%rsp), %rbx
9c8bdb: movl -0x18(%rbx,%r14), %ebx
9c8be0: addl %r10d, %ebx
9c8be3: imulq %r12, %rbx
9c8be7: bzhiq %r11, %rbx, %rbx
9c8bec: cmpq %rbx, %rdx
9c8bef: jne 0x9c8c00 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x22b0>
9c8bf1: addq 0x8(%rcx), %r15
9c8bf5: movl $0x1, %ebx
9c8bfa: jmp 0x9c8c02 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x22b2>
9c8c00: xorl %ebx, %ebx
9c8c02: addq %r15, %rbx
9c8c05: movq 0x60(%rsp), %r15
9c8c0a: movl -0x14(%r15,%r14), %r15d
9c8c0f: addl %r10d, %r15d
9c8c12: imulq %r12, %r15
9c8c16: bzhiq %r11, %r15, %r15
9c8c1b: cmpq %r15, %rdx
9c8c1e: jne 0x9c8c30 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x22e0>
9c8c20: addq 0x8(%rcx), %rbx
9c8c24: movl $0x1, %r15d
9c8c2a: jmp 0x9c8c33 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x22e3>
9c8c30: xorl %r15d, %r15d
9c8c33: addq %rbx, %r15
9c8c36: movq 0x60(%rsp), %rbx
9c8c3b: movl -0x10(%rbx,%r14), %ebx
9c8c40: addl %r10d, %ebx
9c8c43: imulq %r12, %rbx
9c8c47: bzhiq %r11, %rbx, %rbx
9c8c4c: cmpq %rbx, %rdx
9c8c4f: jne 0x9c8c60 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2310>
9c8c51: addq 0x8(%rcx), %r15
9c8c55: movl $0x1, %ebx
9c8c5a: jmp 0x9c8c62 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2312>
9c8c60: xorl %ebx, %ebx
9c8c62: addq %r15, %rbx
9c8c65: movq 0x60(%rsp), %r15
9c8c6a: movl -0xc(%r15,%r14), %r15d
9c8c6f: addl %r10d, %r15d
9c8c72: imulq %r12, %r15
9c8c76: bzhiq %r11, %r15, %r15
9c8c7b: cmpq %r15, %rdx
9c8c7e: jne 0x9c8c90 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2340>
9c8c80: addq 0x8(%rcx), %rbx
9c8c84: movl $0x1, %r15d
9c8c8a: jmp 0x9c8c93 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2343>
9c8c90: xorl %r15d, %r15d
9c8c93: addq %rbx, %r15
9c8c96: movq 0x60(%rsp), %rbx
9c8c9b: movl -0x8(%rbx,%r14), %ebx
9c8ca0: addl %r10d, %ebx
9c8ca3: imulq %r12, %rbx
9c8ca7: bzhiq %r11, %rbx, %rbx
9c8cac: cmpq %rbx, %rdx
9c8caf: jne 0x9c8cc0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2370>
9c8cb1: addq 0x8(%rcx), %r15
9c8cb5: movl $0x1, %ebx
9c8cba: jmp 0x9c8cc2 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2372>
9c8cc0: xorl %ebx, %ebx
9c8cc2: addq %r15, %rbx
9c8cc5: movq 0x60(%rsp), %r15
9c8cca: movl -0x4(%r15,%r14), %r15d
9c8ccf: addl %r10d, %r15d
9c8cd2: imulq %r12, %r15
9c8cd6: bzhiq %r11, %r15, %r15
9c8cdb: cmpq %r15, %rdx
9c8cde: jne 0x9c8cf0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x23a0>
9c8ce0: addq 0x8(%rcx), %rbx
9c8ce4: movl $0x1, %r15d
9c8cea: jmp 0x9c8cf3 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x23a3>
9c8cf0: xorl %r15d, %r15d
9c8cf3: addq %rbx, %r15
9c8cf6: movq 0x60(%rsp), %rbx
9c8cfb: movl (%rbx,%r14), %ebx
9c8cff: addl %r10d, %ebx
9c8d02: imulq %r12, %rbx
9c8d06: bzhiq %r11, %rbx, %rbx
9c8d0b: cmpq %rbx, %rdx
9c8d0e: je 0x9c8b80 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2230>
9c8d14: xorl %ebx, %ebx
9c8d16: addq %r15, %rbx
9c8d19: addq $0x20, %r14
9c8d1d: cmpq $0x701c, %r14           # imm = 0x701C
9c8d24: jne 0x9c8b9d <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x224d>
9c8d2a: jmp 0x9c8a50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 8u>(orbital::allocation::Pool&, unsigned int)+0x2100>
