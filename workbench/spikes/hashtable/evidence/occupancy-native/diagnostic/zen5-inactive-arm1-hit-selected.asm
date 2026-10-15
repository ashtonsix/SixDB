Native zen5/inactive: void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)
Source-qualified phase value_width_bench.cpp:194, timing source:124.
Complete CFG qualification reused from historical_bridge; all CFG instructions matched original archived assembly by address/opcode.

producer_and_primary_permutation
262ee: movl (%r10), %eax
262f1: imulq %r13, %rax
262f5: movl $0x28, %edx
262fa: bzhiq %rdx, %rax, %r15
262ff: movq %rsi, %rax
26302: xorq %r15, %rax
26305: movl $0x28, %edx
2630a: bzhiq %rdx, %rax, %rax
2630f: movq %rax, %rbx

primary_non255_selector
26340: movzwl %ax, %edx
26343: addq %r9, %rdx
26346: cmpb $-0x1, %bl
26349: je 0x263a0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3190>
2634b: vmovd %ebx, %xmm1
2634f: movl $0x14, %ebx
26354: vpbroadcastb %xmm1, %ymm1
26359: vpcmpeqb 0x5(%rdx), %ymm1, %ymm1
2635e: vpmovmskb %ymm1, %eax
26362: bzhil %ebx, %eax, %ebx
26367: nopw (%rax,%rax)
26370: testq %rbx, %rbx
26373: je 0x263f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x31e0>
26375: tzcntq %rbx, %rax
2637a: leaq (%rax,%rax,2), %r12
2637e: btrq %rax, %rbx
26382: movl 0x19(%rdx,%r12), %r12d
26387: andl $0xffffff, %r12d        # imm = 0xFFFFFF
2638e: cmpq %r12, %r11
26391: jne 0x26370 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3160>
26393: jmp 0x263e3 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x31d3>

primary_255_count_fallback_and_common_success
263a0: movzbl (%rdx), %eax
263a3: testl %eax, %eax
263a5: je 0x263f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x31e0>
263a7: vpcmpeqb 0x5(%rdx), %ymm0, %ymm1
263ac: vpmovmskb %ymm1, %ebx
263b0: bzhil %eax, %ebx, %ebx
263b5: nopw %cs:(%rax,%rax)
263c0: testq %rbx, %rbx
263c3: je 0x263f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x31e0>
263c5: tzcntq %rbx, %rax
263ca: leaq (%rax,%rax,2), %r12
263ce: btrq %rax, %rbx
263d2: movl 0x19(%rdx,%r12), %r12d
263d7: andl $0xffffff, %r12d        # imm = 0xFFFFFF
263de: cmpq %r12, %r11
263e1: jne 0x263c0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x31b0>
263e3: leaq 0x55(%rdx,%rax,2), %rbx
263e8: jmp 0x265d0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x33c0>

secondary_permutation
26402: movl $0x28, %edx
26407: bzhiq %rdx, %rax, %r11
2640c: movq %r11, %r12

secondary_non255_selector
2644d: cmpb $-0x1, %r12b
26451: je 0x2649a <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x328a>
26453: vmovd %r12d, %xmm1
26458: movl $0x11, %edx
2645d: vpbroadcastb %xmm1, %ymm1
26462: vpcmpeqb 0x9(%r11), %ymm1, %ymm1
26468: vpmovmskb %ymm1, %eax
2646c: bzhil %edx, %eax, %edx
26471: nopw %cs:(%rax,%rax)
26480: testq %rdx, %rdx
26483: je 0x264f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x32e0>
26485: tzcntq %rdx, %rax
2648a: movl 0x1a(%r11,%rax,4), %r12d
2648f: btrq %rax, %rdx
26493: cmpq %r12, %rbx
26496: jne 0x26480 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x3270>
26498: jmp 0x264d8 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x32c8>

secondary_255_count_fallback_and_common_success
2649a: movzbl (%r11), %eax
2649e: testl %eax, %eax
264a0: je 0x264f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x32e0>
264a2: vpcmpeqb 0x9(%r11), %ymm0, %ymm1
264a8: vpmovmskb %ymm1, %edx
264ac: bzhil %eax, %edx, %edx
264b1: nopw %cs:(%rax,%rax)
264c0: testq %rdx, %rdx
264c3: je 0x264f0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x32e0>
264c5: tzcntq %rdx, %rax
264ca: movl 0x1a(%r11,%rax,4), %r12d
264cf: btrq %rax, %rdx
264d3: cmpq %r12, %rbx
264d6: jne 0x264c0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x32b0>
264d8: leaq 0x5e(%r11,%rax,2), %rbx
264dd: jmp 0x265d0 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x33c0>

payload_callback
265d0: movzwl (%rbx), %eax
265d3: addq %rax, %r14
265d6: movl $0x1, %eax
265db: jmp 0x262e2 <void ht::trial<(ht::layout_solver::container)1, 10u, 1u>(orbital::allocation::Pool&, unsigned int, unsigned int)+0x30d2>
