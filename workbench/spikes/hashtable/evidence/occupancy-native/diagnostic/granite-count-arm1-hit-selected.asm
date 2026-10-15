Native granite/count: void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)
Source-qualified phase value_width_bench.cpp:194, timing source:124.
Complete CFG qualification reused from historical_bridge; all CFG instructions matched original archived assembly by address/opcode.

producer_and_primary_permutation
23b8e: movl (%r15), %eax
23b91: movabsq $-0x2917014799a6026d, %rdx # imm = 0xD6E8FEB86659FD93
23b9b: imulq %rdx, %rax
23b9f: movl $0x28, %edx
23ba4: bzhiq %rdx, %rax, %rbx
23ba9: movq %rsi, %rax
23bac: xorq %rbx, %rax
23baf: movl $0x28, %edx
23bb4: bzhiq %rdx, %rax, %r11
23bb9: movq %r11, %r10

primary_count_selector_and_success
23bd3: movzwl %ax, %eax
23bd6: leaq (%r8,%rax), %r13
23bda: movzbl (%r8,%rax), %eax
23bdf: testl %eax, %eax
23be1: je 0x23c50 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2db0>
23be3: shrq $0x11, %r11
23be7: movq 0x8(%rsp), %rdx
23bec: xorq %r11, %rdx
23bef: mulxq %rdi, %r14, %rdx
23bf4: xorl %edx, %r10d
23bf7: xorl %r14d, %r10d
23bfa: vpbroadcastb %r10d, %ymm0
23c00: vpcmpeqb 0x5(%r13), %ymm0, %k0
23c0a: kmovd %k0, %edx
23c0e: bzhil %eax, %edx, %eax
23c13: nopw %cs:(%rax,%rax)
23c20: testq %rax, %rax
23c23: je 0x23c50 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2db0>
23c25: tzcntq %rax, %rdx
23c2a: btrq %rdx, %rax
23c2e: leaq (%rdx,%rdx,2), %r10
23c32: movl 0x19(%r13,%r10), %r10d
23c37: andl $0xffffff, %r10d        # imm = 0xFFFFFF
23c3e: cmpq %r10, %r11
23c41: jne 0x23c20 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2d80>
23c43: leaq 0x55(%r13,%rdx,2), %r13
23c48: jmp 0x23de0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2f40>

secondary_permutation
23c63: movl $0x28, %edx
23c68: bzhiq %rdx, %rax, %r11
23c6d: movq %r11, %r10

secondary_count_selector_and_success
23c8c: leaq (%rbp,%rdx), %r13
23c91: movzbl (%rbp,%rdx), %eax
23c96: testl %eax, %eax
23c98: je 0x23d00 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2e60>
23c9a: shrq $0xd, %r11
23c9e: movq 0x50(%rsp), %rdx
23ca3: xorq %r11, %rdx
23ca6: mulxq %rdi, %r14, %rdx
23cab: xorl %edx, %r10d
23cae: xorl %r14d, %r10d
23cb1: vpbroadcastb %r10d, %ymm0
23cb7: vpcmpeqb 0x9(%r13), %ymm0, %k0
23cc1: kmovd %k0, %edx
23cc5: bzhil %eax, %edx, %eax
23cca: nopw (%rax,%rax)
23cd0: testq %rax, %rax
23cd3: je 0x23d00 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2e60>
23cd5: tzcntq %rax, %rdx
23cda: btrq %rdx, %rax
23cde: movl 0x1a(%r13,%rdx,4), %r10d
23ce3: cmpq %r10, %r11
23ce6: jne 0x23cd0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2e30>
23ce8: leaq 0x5e(%r13,%rdx,2), %r13
23ced: jmp 0x23de0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2f40>

payload_callback
23de0: movzwl (%r13), %eax
23de5: addq %rax, %r12
23de8: movl $0x1, %eax
23ded: jmp 0x23b82 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2ce2>
