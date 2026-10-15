Native zen5/count: void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)
Source-qualified phase value_width_bench.cpp:194, timing source:124.
Complete CFG qualification reused from historical_bridge; all CFG instructions matched original archived assembly by address/opcode.

producer_and_primary_permutation
2422e: movl (%r15), %eax
24231: movabsq $-0x2917014799a6026d, %rdx # imm = 0xD6E8FEB86659FD93
2423b: imulq %rdx, %rax
2423f: movl $0x28, %edx
24244: bzhiq %rdx, %rax, %rbx
24249: movq %rsi, %rax
2424c: xorq %rbx, %rax
2424f: movl $0x28, %edx
24254: bzhiq %rdx, %rax, %r11
24259: movq %r11, %r10

primary_count_selector_and_success
24273: movzwl %dx, %eax
24276: leaq (%r8,%rax), %r13
2427a: movzbl (%r8,%rax), %eax
2427f: testl %eax, %eax
24281: je 0x242f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2f50>
24283: movq 0x8(%rsp), %rdx
24288: shrq $0x11, %r11
2428c: xorq %r11, %rdx
2428f: mulxq %rdi, %r14, %rdx
24294: xorl %r14d, %r10d
24297: xorl %edx, %r10d
2429a: vmovd %r10d, %xmm0
2429f: vpbroadcastb %xmm0, %ymm0
242a4: vpcmpeqb 0x5(%r13), %ymm0, %ymm0
242aa: vpmovmskb %ymm0, %edx
242ae: bzhil %eax, %edx, %eax
242b3: nopw %cs:(%rax,%rax)
242c0: testq %rax, %rax
242c3: je 0x242f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2f50>
242c5: tzcntq %rax, %rdx
242ca: leaq (%rdx,%rdx,2), %r10
242ce: btrq %rdx, %rax
242d2: movl 0x19(%r13,%r10), %r10d
242d7: andl $0xffffff, %r10d        # imm = 0xFFFFFF
242de: cmpq %r10, %r11
242e1: jne 0x242c0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2f20>
242e3: leaq 0x55(%r13,%rdx,2), %r13
242e8: jmp 0x24480 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x30e0>

secondary_permutation
24303: movl $0x28, %edx
24308: bzhiq %rdx, %rax, %r11
2430d: movq %r11, %r10

secondary_count_selector_and_success
2432c: movzbl (%rbp,%rdx), %eax
24331: leaq (%rbp,%rdx), %r13
24336: testl %eax, %eax
24338: je 0x243a0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3000>
2433a: movq 0x58(%rsp), %rdx
2433f: shrq $0xd, %r11
24343: xorq %r11, %rdx
24346: mulxq %rdi, %r14, %rdx
2434b: xorl %r14d, %r10d
2434e: xorl %edx, %r10d
24351: vmovd %r10d, %xmm0
24356: vpbroadcastb %xmm0, %ymm0
2435b: vpcmpeqb 0x9(%r13), %ymm0, %ymm0
24361: vpmovmskb %ymm0, %edx
24365: bzhil %eax, %edx, %eax
2436a: nopw (%rax,%rax)
24370: testq %rax, %rax
24373: je 0x243a0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3000>
24375: tzcntq %rax, %rdx
2437a: movl 0x1a(%r13,%rdx,4), %r10d
2437f: btrq %rdx, %rax
24383: cmpq %r10, %r11
24386: jne 0x24370 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2fd0>
24388: leaq 0x5e(%r13,%rdx,2), %r13
2438d: jmp 0x24480 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x30e0>

payload_callback
24480: movzwl (%r13), %eax
24485: addq %rax, %r12
24488: movl $0x1, %eax
2448d: jmp 0x24222 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x2e82>
