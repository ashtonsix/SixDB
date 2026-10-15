Native granite/inactive: void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)
Source-qualified phase value_width_bench.cpp:194, timing source:124.
Complete CFG qualification reused from historical_bridge; all CFG instructions matched original archived assembly by address/opcode.

producer_and_primary_permutation
25b3e: movl (%r10), %eax
25b41: movabsq $-0x2917014799a6026d, %rdx # imm = 0xD6E8FEB86659FD93
25b4b: imulq %rdx, %rax
25b4f: movl $0x28, %edx
25b54: bzhiq %rdx, %rax, %r15
25b59: movq %rsi, %rax
25b5c: xorq %r15, %rax
25b5f: movl $0x28, %edx
25b64: bzhiq %rdx, %rax, %rax
25b69: movq %rax, %rbx

primary_non255_selector
25b9a: movzwl %ax, %edx
25b9d: addq %r9, %rdx
25ba0: cmpb $-0x1, %bl
25ba3: je 0x25c00 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3020>
25ba5: vpbroadcastb %ebx, %ymm1
25bab: vpcmpeqb 0x5(%rdx), %ymm1, %k0
25bb5: kmovd %k0, %eax
25bb9: movl $0x14, %ebx
25bbe: bzhil %ebx, %eax, %ebx
25bc3: nopw %cs:(%rax,%rax)
25bd0: testq %rbx, %rbx
25bd3: je 0x25c50 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3070>
25bd5: tzcntq %rbx, %rax
25bda: btrq %rax, %rbx
25bde: leaq (%rax,%rax,2), %r12
25be2: movl 0x19(%rdx,%r12), %r12d
25be7: andl $0xffffff, %r12d        # imm = 0xFFFFFF
25bee: cmpq %r12, %r11
25bf1: jne 0x25bd0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2ff0>
25bf3: jmp 0x25c43 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3063>

primary_255_count_fallback_and_common_success
25c00: movzbl (%rdx), %eax
25c03: testl %eax, %eax
25c05: je 0x25c50 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3070>
25c07: vpcmpeqb 0x5(%rdx), %ymm0, %k0
25c11: kmovd %k0, %ebx
25c15: bzhil %eax, %ebx, %ebx
25c1a: nopw (%rax,%rax)
25c20: testq %rbx, %rbx
25c23: je 0x25c50 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3070>
25c25: tzcntq %rbx, %rax
25c2a: btrq %rax, %rbx
25c2e: leaq (%rax,%rax,2), %r12
25c32: movl 0x19(%rdx,%r12), %r12d
25c37: andl $0xffffff, %r12d        # imm = 0xFFFFFF
25c3e: cmpq %r12, %r11
25c41: jne 0x25c20 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3040>
25c43: leaq 0x55(%rdx,%rax,2), %rbx
25c48: jmp 0x25e20 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3240>

secondary_permutation
25c62: movl $0x28, %edx
25c67: bzhiq %rdx, %rax, %r11
25c6c: movq %r11, %r12

secondary_non255_selector
25cab: cmpb $-0x1, %r12b
25caf: je 0x25cea <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x310a>
25cb1: vpbroadcastb %r12d, %ymm1
25cb7: vpcmpeqb 0x9(%r11), %ymm1, %k0
25cc1: kmovd %k0, %eax
25cc5: movl $0x11, %edx
25cca: bzhil %edx, %eax, %edx
25ccf: nop 
25cd0: testq %rdx, %rdx
25cd3: je 0x25d40 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3160>
25cd5: tzcntq %rdx, %rax
25cda: btrq %rax, %rdx
25cde: movl 0x1a(%r11,%rax,4), %r12d
25ce3: cmpq %r12, %rbx
25ce6: jne 0x25cd0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x30f0>
25ce8: jmp 0x25d28 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3148>

secondary_255_count_fallback_and_common_success
25cea: movzbl (%r11), %eax
25cee: testl %eax, %eax
25cf0: je 0x25d40 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3160>
25cf2: vpcmpeqb 0x9(%r11), %ymm0, %k0
25cfc: kmovd %k0, %edx
25d00: bzhil %eax, %edx, %edx
25d05: nopw %cs:(%rax,%rax)
25d10: testq %rdx, %rdx
25d13: je 0x25d40 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3160>
25d15: tzcntq %rdx, %rax
25d1a: btrq %rax, %rdx
25d1e: movl 0x1a(%r11,%rax,4), %r12d
25d23: cmpq %r12, %rbx
25d26: jne 0x25d10 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3130>
25d28: leaq 0x5e(%r11,%rax,2), %rbx
25d2d: jmp 0x25e20 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3240>

payload_callback
25e20: movzwl (%rbx), %eax
25e23: addq %rax, %r14
25e26: movl $0x1, %eax
25e2b: jmp 0x25b32 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2f52>
